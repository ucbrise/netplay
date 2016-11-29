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
#include "filterops.h"
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
     * @param query The filter query.
     */
    void filter(std::unordered_set<uint64_t>& results,
                filter_query& query) const {
      base_.filter(results, query);
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

  typedef unsigned long long int timestamp_t;

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

  /**
   * Filter index-entries based on query.
   *
   * @param results The results of the filter query.
   * @param query The filter query.
   */
  void filter(std::unordered_set<uint64_t>& results,
              filter_query& query) const {
    uint64_t max_rid = olog_->num_ids();
    for (filter_conjunction& conjunction : query) {
      std::unordered_set<uint64_t> conjunction_results;
      std::sort(conjunction.begin(), conjunction.end(), [this](const basic_filter & lhs, const basic_filter & rhs) {
        return filter_count(lhs) < filter_count(rhs);
      });

      for (basic_filter& basic : conjunction) {
        /* Perform basic filter */
        std::unordered_set<uint64_t> filter_res;
        filter(filter_res, basic, max_rid, conjunction_results);

        /* Stop this sequence of conjunctions if filter results are empty */
        if (filter_res.empty())
          break;

        conjunction_results = filter_res;
      }
      results.insert(conjunction_results.begin(), conjunction_results.end());
    }
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

  template<typename INDEX>
  INDEX* get_index(uint32_t index_id) {
    /* Identify which index token belongs to */
    uint32_t idx = index_id / OFFSETMIN;
    uint32_t off = index_id % OFFSETMIN;

    /* Fetch the index */
    switch (idx) {
    case 1: return idx1_->at(off);
    case 2: return idx1_->at(off);
    case 4: return idx1_->at(off);
    case 8: return idx1_->at(off);
    case 16: return idx1_->at(off);
    case 32: return idx1_->at(off);
    case 64: return idx1_->at(off);
    case 128: return idx1_->at(off);
    default: return NULL;
    }
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
  uint64_t filter_count(const basic_filter& f) const {
    /* Identify which index the filter is on */
    uint32_t idx = f.index_id() / OFFSETMIN;
    uint32_t off = f.index_id() % OFFSETMIN;

    /* Query relevant index */
    switch (idx) {
    case 1:
      return filter_count(idx1_->at(off), f.token_beg(), f.token_end());
    case 2:
      return filter_count(idx2_->at(off), f.token_beg(), f.token_end());
    case 4:
      return filter_count(idx3_->at(off), f.token_beg(), f.token_end());
    case 8:
      return filter_count(idx4_->at(off), f.token_beg(), f.token_end());
    case 16:
      return filter_count(idx5_->at(off), f.token_beg(), f.token_end());
    case 32:
      return filter_count(idx6_->at(off), f.token_beg(), f.token_end());
    case 64:
      return filter_count(idx7_->at(off), f.token_beg(), f.token_end());
    case 128:
      return filter_count(idx8_->at(off), f.token_beg(), f.token_end());
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
  uint64_t filter_count(INDEX* index, uint64_t token_beg, uint64_t token_end) const {
    uint64_t count = 0;
    for (uint64_t i = token_beg; i <= token_end; i++) {
      entry_list* list = index->get(i);
      if (list != NULL)
        count += list->size();
    }
    return count;
  }

  /**
   * Atomically filter record ids for a basic filter.
   *
   * @param results The results set to be populated with matching records ids.
   * @param basic The basic filter.
   * @param max_rid Largest record-id to consider.
   * @param superset The superset to which the results must belong.
   */
  void filter(std::unordered_set<uint64_t>& results,
              const basic_filter& basic, const uint64_t max_rid,
              std::unordered_set<uint64_t> superset) const {

    /* Identify which index the filter is on */
    uint32_t idx = basic.index_id() / OFFSETMIN;
    uint32_t off = basic.index_id() % OFFSETMIN;

    switch (idx) {
    case 1: {
      filter(results, idx1_->at(off), basic.token_beg(),
             basic.token_end(), max_rid, superset);
      break;
    }
    case 2: {
      filter(results, idx2_->at(off), basic.token_beg(),
             basic.token_end(), max_rid, superset);
      break;
    }
    case 4: {
      filter(results, idx3_->at(off), basic.token_beg(),
             basic.token_end(), max_rid, superset);
      break;
    }
    case 8: {
      filter(results, idx4_->at(off), basic.token_beg(),
             basic.token_end(), max_rid, superset);
      break;
    }
    case 16: {
      filter(results, idx5_->at(off), basic.token_beg(),
             basic.token_end(), max_rid, superset);
      break;
    }
    case 32: {
      filter(results, idx6_->at(off), basic.token_beg(),
             basic.token_end(), max_rid, superset);
      break;
    }
    case 64: {
      filter(results, idx7_->at(off), basic.token_beg(),
             basic.token_end(), max_rid, superset);
      break;
    }
    case 128: {
      filter(results, idx8_->at(off), basic.token_beg(),
             basic.token_end(), max_rid, superset);
      break;
    }
    }
  }

  /**
   * Atomically filter record ids from a given index for a token range.
   *
   * @param results The results set to be populated with matching records ids.
   * @param index The index associated with the token.
   * @param token_beg The beginning of token range.
   * @param token_end The end of token range.
   * @param max_rid Largest record-id to consider.
   * @param superset The superset to which the results must belong.
   */
  template<typename INDEX>
  void filter(std::unordered_set<uint64_t>& results, INDEX * index,
              uint64_t token_beg, uint64_t token_end, const uint64_t max_rid,
              const std::unordered_set<uint64_t>& superset) const {
    fprintf(stderr, "Filter: begin=%" PRIu64 ", end=%" PRIu64 ", ", token_beg, token_end);
    timestamp_t t0 = get_timestamp();
    for (uint64_t i = token_beg; i <= token_end; i++) {
      entry_list* list = index->get(i);
      sweep_list(results, list, max_rid, superset);
    }
    timestamp_t t1 = get_timestamp();
    fprintf(stderr, "count = %zu, time taken=%llu\n", results.size(), (t1 - t0));
  }

  /**
   * Sweeps through the entry-list, adding all valid entries to the results.
   *
   *
   * @param results The set of results to be populated.
   * @param list The entry list.
   * @param max_rid The maximum permissible record id.
   * @param superset The superset to which the results must belong.
   * @param superset_check Flag which determines whether to perform superset
   *  check or not.
   */
  void sweep_list(std::unordered_set<uint64_t>& results, entry_list * list,
                  uint64_t max_rid,
                  const std::unordered_set<uint64_t>& superset) const {
    if (list == NULL)
      return;

    uint32_t size = list->size();
    for (uint32_t i = 0; i < size; i++) {
      uint64_t record_id = list->at(i);
      if (olog_->is_valid(record_id, max_rid)
          && (superset.empty() || superset.find(record_id) != superset.end()))
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

  static timestamp_t get_timestamp() {
    struct timeval now;
    gettimeofday(&now, NULL);

    return now.tv_usec + (timestamp_t) now.tv_sec * 1000000;
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
