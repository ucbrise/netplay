#ifndef NETPLAY_WRITER_H_
#define NETPLAY_WRITER_H_

#include <sys/time.h>

#include <ctime>
#include <chrono>

#include <rte_mbuf.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>
#include <rte_lpm.h>

#include "packetstore.h"
#include "tokens.h"

namespace netplay {

#define BATCH_SIZE        32

template<typename vport_init>
class netplay_writer {
 public:
  netplay_writer(int core, dpdk::virtual_port<vport_init>* vport, packet_store::handle* handle) {
    rec_pkts_ = 0;
    core_ = core;
    vport_ = vport;
    handle_ = handle;
  }

  void start() {
    struct rte_mbuf* pkts[BATCH_SIZE];

    uint64_t start = curusec();
    uint64_t epoch = start;
    while (1) {
      rec_pkts_ = vport_->recv_pkts(pkts, BATCH_SIZE);
      handle_->insert_pktburst(pkts, recv);
    }
  }

  int core() {
    return core_;
  }

  uint64_t rec_pkts() {
    return rec_pkts_;
  }

 private:
  inline uint64_t curusec() {
    using namespace ::std::chrono;
    auto ts = steady_clock::now().time_since_epoch();
    return duration_cast<std::chrono::microseconds>(ts).count();
  }

  int core_;
  uint64_t rec_pkts_;
  dpdk::virtual_port<vport_init>* vport_;
  packet_store::handle* handle_;
};

}

#endif  // NETPLAY_WRITER_H_