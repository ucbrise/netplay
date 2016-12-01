#include <cassert>
#include <ctime>

#include "NetPlayQueryService.h"
#include "packetstore.h"
#include "query_utils.h"
#include "query_planner.h"
#include "netplay_types.h"

namespace netplay {

uint32_t query_utils::now = std::time(NULL);

class query_handler : virtual public thrift::NetPlayQueryServiceIf {
 public:
  query_handler(netplay::packet_store::handle* handle) {
    handle_ = handle;
  }

  void filter(std::vector<int64_t>& _return, const std::string& query) {

    // Parse query
    filter_query q;
    try {
      q = query_utils::expression_to_filter_query(handle_, query);
    } catch (parse_exception& e) {
      fprintf(stderr, "Parse exception: %s\n", e.what());
      thrift::QueryException qe;
      qe.message = std::string(e.what());
      throw qe;
    } catch (std::exception& e) {
      fprintf(stderr, "Other exception: %s\n", e.what());
      thrift::QueryException qe;
      qe.message = std::string(e.what());
      throw qe;
    }

    // Compute results
    std::unordered_set<uint64_t> res;
    handle_->filter_pkts(res, q);

    // Copy into vector
    _return.reserve(res.size());
    for (uint64_t r: res) {
      _return.push_back(r);
    }
  }

  void get(std::string& _return, const int64_t record_id) {
    unsigned char buf[256];
    handle_->get(buf, record_id);
    _return.assign((char*)buf);
  }

  void extract(std::string& _return, const int64_t record_id, const int16_t off, const int16_t len) {
    unsigned char buf[256];
    uint32_t len_ = len;
    handle_->extract(buf, record_id, off, len_);
    _return.assign((char*)buf);
  }

  int64_t numRecords() {
    return handle_->num_pkts();
  }

  void storageFootprint(thrift::Storage& _return) {
    slog::logstore_storage storage;
    handle_->storage_footprint(storage);
    _return.dlog_size = storage.dlog_size;
    _return.olog_size = storage.olog_size;
    for (size_t size : storage.idx_sizes)
      _return.idx_sizes.push_back(size);
    for (size_t size : storage.stream_sizes)
      _return.stream_sizes.push_back(size);
  }

 private:
  netplay::packet_store::handle* handle_;
};

}