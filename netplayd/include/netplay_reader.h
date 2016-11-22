#ifndef NETPLAY_READER_H_
#define NETPLAY_READER_H_

namespace netplay {

class netplay_reader {
 public:
  netplay_reader(int core, packet_store::handle* handle) {
    core_ = core;
    handle_ = handle;
  }

  void start() {
    while (1) {
      // TODO: Add query processing logic
      fprintf(stderr, "[Core %d] NetPlay reader not implemented, "
              "disable reader core mask.\n", core_);
    }
  }

  int core() {
    return core_;
  }

 private:
  int core_;
  packet_store::handle* handle_;
};

}

#endif  // NETPLAY_READER_H_