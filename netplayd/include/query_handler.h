#include <cassert>

#include "NetPlayQueryService.h"
#include "packetstore.h"
#include "netplay_types.h"

namespace netplay {

class query_handler : virtual public thrift::NetPlayQueryServiceIf {
 public:
  query_handler(netplay::packet_store::handle* handle) {
    handle_ = handle;
  }

  void filter(std::set<int64_t>& _return, const std::string& query) {
    // Your implementation goes here
    assert(_return.empty());
    printf("filter %s\n", query.c_str());
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
    return handle_->num_records();
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