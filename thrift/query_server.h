#include "NetPlayQueryService.h"
#include "packetstore.h"

namespace netplay {

class query_handler : virtual public thrift::NetPlayQueryServiceIf {
 public:
  query_handler(netplay::packet_store::handle* handle) {
    handle_ = handle;
  }

  void filter(std::set<int64_t>& _return, const std::string& query) {
    // Your implementation goes here
    printf("filter\n");
  }

  void get(std::string& _return, const int64_t record_id) {
    char buf[256];
    handle_->get(buf, record_id);
    _return.assign(buf);
  }

  void extract(std::string& _return, const int64_t record_id, const int16_t off, const int16_t len) {
    char buf[256];
    handle_->extract(buf, record_id, off, len);
    _return.assign(buf);
  }

  int64_t numRecords() {
    handle_->num_records();
  }

  void storageFootprint(Storage& _return) {
    slog::logstore_storage storage;
    handle_->storage_footprint(storage);
    _return.dlog_size = storage.dlog_size;
    _return.olog_size = storage.olog_size;
    _return.idx_sizes = storage.idx_sizes;
    _return.stream_sizes = storage.stream_sizes;
  }

 private:
  netplay::packet_store::handle* handle_;
};

}