#ifndef SLOG_LOGSTORE_H_
#define SLOG_LOGSTORE_H_

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <climits>
#include <cassert>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/time.h>

#include <string>
#include <unordered_set>
#include <map>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <fstream>
#include <atomic>

#include "tokens.h"
#include "tieredindex.h"
#include "streamlog.h"
#include "offsetlog.h"
#include "filterresult.h"
#include "utils.h"

#define OFFSETMIN 1024
#define OFFSET1 1024
#define OFFSET2 2048
#define OFFSET3 4096
#define OFFSET4 8192
#define OFFSET5 16384
#define OFFSET6 32768
#define OFFSET7 65536
#define OFFSET8 131072

namespace slog {

struct logstore_storage {
  size_t dlog_size;
  size_t olog_size;
  std::vector<size_t> idx_sizes;
  std::vector<size_t> stream_sizes;
};

class log_store {
 public:
  typedef std::unordered_set<uint64_t> result_type;

  class handle {
   public:
    /**
     * Constructor to initialize handle.
     *
     * @param base The base log-store for the handle.
     * @param request_batch_size The record batch size (#records) for insert queries.
     * @param data_block_size The data block size (#bytes) for insert queries.
     */
    handle(log_store& base, uint64_t request_batch_size = 32,
           uint64_t data_block_size = 32 * 40)
      : base_(base) {
      id_block_size_ = request_batch_size;
      remaining_ids_ = 0;
      cur_id_ = 0;

      data_block_size_ = data_block_size;
      remaining_bytes_ = 0;
      cur_offset_ = 0;
    }

    /**
     * Add a new index of specified token-length and prefix-length.
     *
     * @param token_length Length of the tokens.
     * @param prefix_length Length of the prefix being indexed.
     *
     * @return The id (> 0) of the newly created index. Returns zero on failure.
     */
    uint32_t add_index(uint32_t token_length) {
      return base_.add_index(token_length);
    }

    /**
     * Add a new stream with a specified filter function.
     *
     * @param fn The filter function.
     * @return The id of the newly created stream.
     */
    uint32_t add_stream(filter_function fn) {
      return base_.add_stream(fn);
    }

    /**
     * Insert a new record into the log-store.
     *
     * @param record The buffer containing record data.
     * @param record_len The length of the record.
     * @param tokens Tokens associated with the record.
     * @return The unique record id generated for the record.
     */
    uint64_t insert(const unsigned char* record, uint16_t record_len,
                    token_list& tkns) {
      if (remaining_ids_ == 0) {
        cur_id_ = base_.olog_->request_id_block(id_block_size_);
        remaining_ids_ = id_block_size_;
      }

      if (remaining_bytes_ < record_len) {
        cur_offset_ = base_.request_bytes(data_block_size_);
        remaining_bytes_ = data_block_size_;
      }

      base_.append_record(record, record_len, cur_offset_);
      base_.update_indexes(cur_id_, tkns);
      base_.update_streams(cur_id_, record, record_len, tkns);
      base_.olog_->set(cur_id_, cur_offset_, record_len);
      base_.olog_->end(cur_id_);
      remaining_ids_--;
      cur_offset_ += record_len;
      return ++cur_id_;
    }

    /**
     * Atomically fetch a record from the log-store given its recordId. The
     * record buffer must be pre-allocated with sufficient size.
     *
     * @param record The (pre-allocated) record buffer.
     * @param record_id The id of the record being requested.
     * @return true if the fetch is successful, false otherwise.
     */
    bool get(unsigned char* record, const uint64_t record_id) const {
      return base_.get(record, record_id);
    }

    /**
     * Atomically extract a portion of the record from the log-store given its
     * record-id, offset into the record and the number of bytes required. The
     * record buffer must be pre-allocated with sufficient size.
     *
     * @param record The (pre-allocated) record-buffer.
     * @param record_id The id of the record being requested.
     * @param offset The offset into the record to begin extracting.
     * @param length The number of bytes to extract. Updated with the actual
     *  number of bytes extracted.
     * @return true if the extract is successful, false otherwise.
     */
    bool extract(unsigned char* record, const uint64_t record_id,
                 uint32_t offset, uint32_t& length) const {
      return base_.extract(record, record_id, offset, length);
    }

    /**
    * Filter index-entries based on query.
    *
    * @param results The results of the filter query.
    * @param index_id The id for the index to query.
    * @param tok_min The smallest token to consider.
    * @param tok_max The largest token to consider.
    */
    void filter(result_type& results, const uint32_t index_id,
                const uint64_t tok_min, const uint64_t tok_max) const {
      base_.filter(results, index_id, tok_min, tok_max);
    }

    /**
     * Get the stream associated with a given stream id.
     *
     * @param stream_id The id of the stream.
     * @return The stream associated with the id.
     */
    entry_list* get_stream(uint32_t stream_id) const {
      return base_.get_stream(stream_id);
    }

    /* Statistics and helpers */

    /** Atomically get the number of currently readable records.
     *
     * @return The number of readable records.
     */
    uint64_t num_records() const {
      return base_.num_records();
    }

    /** Get storage statistics
     *
     * @param storage_stats The storage structure which will be populated with
     * storage statistics at the end of the call.
     */
    void storage_footprint(logstore_storage& storage_stats) const {
      base_.storage_footprint(storage_stats);
    }

   private:
    uint64_t data_block_size_;
    uint64_t id_block_size_;

    uint64_t cur_id_;
    uint64_t remaining_ids_;

    uint64_t cur_offset_;
    uint64_t remaining_bytes_;

    log_store& base_;
  };

  /**
   * Constructor to initialize the log-store.
   */
  log_store() {
    /* Initialize data log and offset log */
    dlog_ = new __monolog_linear_base <uint8_t>;
    olog_ = new offsetlog;

    /* Initialize data log tail to zero. */
    dtail_.store(0);

    /* Initialize all index classes */
    idx1_ = new monolog_linearizable<__index1 *>;
    idx2_ = new monolog_linearizable<__index2 *>;
    idx3_ = new monolog_linearizable<__index3 *>;
    idx4_ = new monolog_linearizable<__index4 *>;
    idx5_ = new monolog_linearizable<__index5 *>;
    idx6_ = new monolog_linearizable<__index6 *>;
    idx7_ = new monolog_linearizable<__index7 *>;
    idx8_ = new monolog_linearizable<__index8 *>;

    /* Initialize stream logs */
    streams_ = new monolog_linearizable<streamlog*>;
  }

  /**
   * Get a handle to the log-store.
   *
   * @return A handle to the log-store.
   */
  handle* get_handle() {
    return new handle(*this);
  }

  /**
   * Add a new index for tokens of specified length.
   *
   * @param token_length Length of the tokens to be indexed.
   *
   * @return The id (> 0) of the newly created index. Returns zero on failure.
   */
  uint32_t add_index(uint32_t token_length) {
    switch (token_length) {
    case 1:
      return OFFSET1 + idx1_->push_back(new __index1);
    case 2:
      return OFFSET2 + idx2_->push_back(new __index2);
    case 3:
      return OFFSET3 + idx3_->push_back(new __index3);
    case 4:
      return OFFSET4 + idx4_->push_back(new __index4);
    case 5:
      return OFFSET5 + idx5_->push_back(new __index5);
    case 6:
      return OFFSET6 + idx6_->push_back(new __index6);
    case 7:
      return OFFSET7 + idx7_->push_back(new __index7);
    case 8:
      return OFFSET8 + idx8_->push_back(new __index8);
    }

    return 0;
  }

  /**
   * Add a new stream with a specified filter function.
   *
   * @param fn The filter function.
   * @return The id of the newly created stream.
   */
  uint32_t add_stream(filter_function fn) {
    return streams_->push_back(new streamlog(fn));
  }

  /**
   * Insert a new record into the log-store.
   *
   * @param record The buffer containing record data.
   * @param record_len The length of the record.
   * @param tokens Tokens associated with the record.
   * @return The unique record id generated for the record.
   */
  uint64_t insert(const unsigned char* record, uint16_t record_len,
                  token_list& tokens) {
    /* Atomically request bytes at the end of data-log */
    uint64_t offset = request_bytes(record_len);

    /* Start the insertion by obtaining a record id from offset log */
    uint64_t record_id = olog_->start(offset, record_len);

    /* Append the record value to data log */
    append_record(record, record_len, offset);

    /* Add the index entries to index logs */
    update_indexes(record_id, tokens);

    /* Add the record entry to appropriate streams */
    update_streams(record_id, record, record_len, tokens);

    /* End the write operation; makes the record available for query */
    olog_->end(record_id);

    /* Return record_id */
    return record_id;
  }

  /**
   * Atomically fetch a record from the log-store given its recordId. The
   * record buffer must be pre-allocated with sufficient size.
   *
   * @param record The (pre-allocated) record buffer.
   * @param record_id The id of the record being requested.
   * @return true if the fetch is successful, false otherwise.
   */
  bool get(unsigned char* record, const uint64_t record_id) const {

    /* Checks if the record_id has been written yet, returns false on failure. */
    if (!olog_->is_valid(record_id))
      return false;

    uint64_t offset;
    uint16_t length;
    olog_->lookup(record_id, offset, length);

    /* Copy data from data log to record buffer. */
    dlog_->read(offset, record, length);

    /* Return true for successful get. */
    return true;
  }

  /**
   * Atomically extract a portion of the record from the log-store given its
   * record-id, offset into the record and the number of bytes required. The
   * record buffer must be pre-allocated with sufficient size.
   *
   * @param record The (pre-allocated) record-buffer.
   * @param record_id The id of the record being requested.
   * @param offset The offset into the record to begin extracting.
   * @param length The number of bytes to extract. Updated with the actual
   *  number of bytes extracted.
   * @return true if the extract is successful, false otherwise.
   */
  bool extract(unsigned char* record, const uint64_t record_id, uint32_t offset,
               uint32_t& length) const {

    /* Checks if the record_id has been written yet, returns false on failure. */
    if (!olog_->is_valid(record_id))
      return false;

    uint64_t record_offset;
    uint16_t record_length;
    olog_->lookup(record_id, record_offset, record_length);

    /* Compute the minimum of requested length and available data */
    length = std::min(length, record_length - offset);

    /* Copy data from data log to record buffer. */
    dlog_->read(record_offset + offset, record, length);

    /* Return true for successful extract. */
    return true;
  }

  /**
   * Filter index-entries based on query.
   *
   * @param results The results of the filter query.
   * @param index_id The id for the index to query.
   * @param tok_min The smallest token to consider.
   * @param tok_max The largest token to consider.
   */
  void filter(result_type& results, const uint32_t index_id, const uint64_t tok_min,
              const uint64_t tok_max) const {
    uint64_t max_rid = olog_->num_ids();
    return filter(index_id, tok_min, tok_max, max_rid);
  }

  /**
   * Get the stream associated with a given stream id.
   *
   * @param stream_id The id of the stream.
   * @return The stream associated with the id.
   */
  entry_list* get_stream(uint32_t stream_id) const {
    return streams_->at(stream_id)->get_stream();
  }

  /* Statistics and helpers */

  /** Atomically get the number of currently readable records.
   *
   * @return The number of readable records.
   */
  uint64_t num_records() const {
    return olog_->num_ids();
  }

  /** Atomically get the size of the currently readable portion of the log-store.
   *
   * @return The size in bytes of the currently readable portion of the log-store.
   */
  uint64_t size() const {
    return dtail_.load();
  }

  /** Get storage statistics
   *
   * @param storage_stats The storage structure which will be populated with
   * storage statistics at the end of the call.
   */
  void storage_footprint(logstore_storage& storage_stats) const {
    /* Get size for data-log and offset-log */
    storage_stats.dlog_size = dlog_->storage_size();
    storage_stats.olog_size = olog_->storage_size();

    /* Get size for index-logs */
    index_size(storage_stats.idx_sizes, idx1_);
    index_size(storage_stats.idx_sizes, idx2_);
    index_size(storage_stats.idx_sizes, idx3_);
    index_size(storage_stats.idx_sizes, idx4_);
    index_size(storage_stats.idx_sizes, idx5_);
    index_size(storage_stats.idx_sizes, idx6_);
    index_size(storage_stats.idx_sizes, idx7_);
    index_size(storage_stats.idx_sizes, idx8_);

    /* Get size of stream-logs */
    stream_size(storage_stats.stream_sizes);
  }

 protected:

  /**
   * Atomically request bytes from the data-log.
   *
   * @param record_size The number of bytes being requested.
   * @return The offset within data-log of the offered bytes.
   */
  uint64_t request_bytes(uint64_t request_bytes) {
    return dtail_.fetch_add(request_bytes);
  }

  /**
   * Append a (recordId, record) pair to the log store.
   *
   * @param record The buffer containing record data.
   * @param record_len The length of the buffer.
   * @param offset The offset into the log where data should be written.
   */
  void append_record(const unsigned char* record, uint16_t record_len,
                     uint64_t offset) {

    /* We can append the value to the log without locking since this
     * thread has exclusive access to the region (offset, offset + record_len).
     */
    dlog_->write(offset, record, record_len);
  }

  /**
   * Add (token, recordId) entries to index logs.
   *
   * @param record_id The id of the record whose index entries are being added.
   * @param tokens The tokens associated with the record.
   */
  void update_indexes(uint64_t record_id, token_list& tokens) {
    for (token_t& token : tokens) {
      /* Identify which index token belongs to */
      uint32_t idx = token.index_id() / OFFSETMIN;
      uint32_t off = token.index_id() % OFFSETMIN;

      /* Update relevant index */
      uint64_t key = token.data();
      switch (idx) {
      case 1: {
        idx1_->at(off)->add_entry(key, record_id);
        break;
      }
      case 2: {
        idx2_->at(off)->add_entry(key, record_id);
        break;
      }
      case 4: {
        idx3_->at(off)->add_entry(key, record_id);
        break;
      }
      case 8: {
        idx4_->at(off)->add_entry(key, record_id);
        break;
      }
      case 16: {
        idx5_->at(off)->add_entry(key, record_id);
        break;
      }
      case 32: {
        idx6_->at(off)->add_entry(key, record_id);
        break;
      }
      case 64: {
        idx7_->at(off)->add_entry(key, record_id);
        break;
      }
      case 128: {
        idx8_->at(off)->add_entry(key, record_id);
        break;
      }
      }
    }
  }

  /**
   * Add recordId to all streams which are satisfied by the record.
   *
   * @param record_id Id of the record.
   * @param record Record data.
   * @param record_len Record data length.
   * @param tokens Tokens of the record.
   */
  void update_streams(uint64_t record_id, const unsigned char* record,
                      const uint16_t record_len, const token_list& tokens) {
    uint32_t num_streams = streams_->size();
    for (uint32_t i = 0; i < num_streams; i++) {
      streams_->at(i)->check_and_add(record_id, record, record_len, tokens);
    }
  }

  /**
   * Count index-entries for a basic filter (i.e., simple predicate).
   * Note: This does not return a consistent count. This should only be
   * used for heuristic measures, rather than the actual count. For actual
   * count, use filter() operation.
   *
   * @param f The filter query (predicate).
   % @return The count of the filter query.
   */
  uint64_t filter_count(uint32_t index_id, uint32_t min, uint32_t max) const {
    /* Identify which index the filter is on */
    uint32_t idx = index_id / OFFSETMIN;
    uint32_t off = index_id % OFFSETMIN;

    /* Query relevant index */
    switch (idx) {
    case 1:
      return filter_count(idx1_->at(off), min, max);
    case 2:
      return filter_count(idx2_->at(off), min, max);
    case 4:
      return filter_count(idx3_->at(off), min, max);
    case 8:
      return filter_count(idx4_->at(off), min, max);
    case 16:
      return filter_count(idx5_->at(off), min, max);
    case 32:
      return filter_count(idx6_->at(off), min, max);
    case 64:
      return filter_count(idx7_->at(off), min, max);
    case 128:
      return filter_count(idx8_->at(off), min, max);
    default:
      return 0;
    }
  }

  /**
   * Count index-entries for a range in an index.
   * Note: This does not return a consistent count. This should only be
   * used for heuristic measures, rather than the actual count. For actual
   * count, use filter() operation.
   *
   * @param index .
   % @return The count of the filter query.
   */
  template<typename INDEX>
  uint64_t filter_count(INDEX* index, uint64_t min, uint64_t max) const {
    uint64_t count = 0;
    for (uint64_t i = min; i <= max; i++) {
      entry_list* list = index->get(i);
      if (list != NULL)
        count += list->size();
    }
    return count;
  }

  /**
   * Atomically filter record ids for a given range of token values.
   *
   * @param results The results set to be populated with matching records ids.
   * @param index_id The id of the index corresponding to the token.
   * @param tok_min The smallest token to consider.
   * @param tok_man The largest token to consider.
   * @param max_rid Largest record-id to consider.
   * @param superset The superset to which the results must belong.
   */
  void filter(result_type& results, const uint32_t index_id, const uint64_t tok_min,
              const uint64_t tok_max, const uint64_t max_rid) const {

    /* Identify which index the filter is on */
    uint32_t idx = index_id / OFFSETMIN;
    uint32_t off = index_id % OFFSETMIN;

    switch (idx) {
    case 1: {
      populate_results(results, filter(idx1_->at(off), tok_min, tok_max, max_rid));
      break;
    }
    case 2: {
      populate_results(results, filter(idx2_->at(off), tok_min, tok_max, max_rid));
      break;
    }
    case 4: {
      populate_results(results, filter(idx3_->at(off), tok_min, tok_max, max_rid));
      break;
    }
    case 8: {
      populate_results(results, filter(idx4_->at(off), tok_min, tok_max, max_rid));
      break;
    }
    case 16: {
      populate_results(results, filter(idx5_->at(off), tok_min, tok_max, max_rid));
      break;
    }
    case 32: {
      populate_results(results, filter(idx6_->at(off), tok_min, tok_max, max_rid));
      break;
    }
    case 64: {
      populate_results(results, filter(idx7_->at(off), tok_min, tok_max, max_rid));
      break;
    }
    case 128: {
      populate_results(results, filter(idx8_->at(off), tok_min, tok_max, max_rid));
      break;
    }
    default: {
      // Do nothing
    }
    }
  }

  template<typename INDEX>
  void populate_results(result_type& results, filter_result<INDEX>& filter_res) {
    results.insert(filter_res.begin(), filter_res.end());
  }

  /**
   * Atomically filter record ids from a given index for a token range.
   *
   * @param results The results set to be populated with matching records ids.
   * @param index The index associated with the token.
   * @param min The smallest token to consider.
   * @param max The largest token to consider.
   * @param max_rid Largest record-id to consider.
   */
  template<typename INDEX>
  filter_result<INDEX> filter(result_type& results, INDEX* index, const uint64_t min,
              const uint64_t max, const uint64_t max_rid) const {
    return filter_result<INDEX>(olog_, index, tok_min, tok_max, max_rid);
  }

  /**
   * Sweeps through the entry-list, adding all valid entries to the results.
   *
   * @param results The set of results to be populated.
   * @param list The entry list.
   * @param max_rid The maximum permissible record id.
   */
  void sweep_list(result_type& results, entry_list *list, uint64_t max_rid) const {
    uint32_t size = list->size();
    for (uint32_t i = 0; i < size; i++) {
      uint64_t record_id = list->at(i);
      if (olog_->is_valid(record_id, max_rid))
        results.insert(record_id);
    }
  }

  /**
   * Compute the sizes of index-logs.
   *
   * @param sizes Vector to be populated with index sizes.
   * @param idx Monolog containing indexes.
   */
  template<typename INDEX>
  void index_size(std::vector<size_t>& sizes,
                  monolog_linearizable<INDEX*> *idx) const {
    uint32_t num_indexes = idx->size();
    for (uint32_t i = 0; i < num_indexes; i++) {
      sizes.push_back(idx->at(i)->storage_size());
    }
  }

  /**
   * Compute the sizes of all stream logs.
   *
   * @param sizes Vector to be populated with stream sizes.
   */
  void stream_size(std::vector<size_t>& sizes) const {
    uint32_t num_streams = streams_->size();
    for (uint32_t i = 0; i < num_streams; i++) {
      sizes.push_back(streams_->at(i)->get_stream()->storage_size());
    }
  }

  /* Data log and offset log */
  __monolog_linear_base <uint8_t>* dlog_;
  offsetlog* olog_;

  /* Tail for preserving atomicity */
  std::atomic<uint64_t> dtail_;

  /* Index logs */
  monolog_linearizable<__index1 *> *idx1_;
  monolog_linearizable<__index2 *> *idx2_;
  monolog_linearizable<__index3 *> *idx3_;
  monolog_linearizable<__index4 *> *idx4_;
  monolog_linearizable<__index5 *> *idx5_;
  monolog_linearizable<__index6 *> *idx6_;
  monolog_linearizable<__index7 *> *idx7_;
  monolog_linearizable<__index8 *> *idx8_;

  /* Stream logs */
  monolog_linearizable<streamlog*> *streams_;
};

}

#endif /* SLOG_LOGSTORE_H_ */
