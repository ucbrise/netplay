#ifndef NETPLAY_WRITER_H_
#define NETPLAY_WRITER_H_

#include <sys/time.h>

#include <ctime>
#include <chrono>

#include <confluo_store.h>
#include <rte_mbuf.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>
#include <rte_lpm.h>

#include "tokens.h"

namespace netplay {

#define BATCH_SIZE        32

template<typename vport_init>
class netplay_writer {
 /**
  * Initialize instance of netplay writer.
  *
  * @param core: Number of cores of device
  * @param vport: Virtual port instance from DPDK/include directory for sending/receiving packets
  * @param mlog: Confluo atomic multilog instance for packet analysisleldldkdkd
  */
 public:
  netplay_writer(int core, dpdk::virtual_port<vport_init>* vport, confluo::atomic_multilog* mlog) {
    rec_pkts_ = 0;
    core_ = core;
    vport_ = vport;
    mlog_ = mlog;
  }

  /**
   * Begin receiving and forwarding packets to confluo multilog instance
   */
  void start() {
    struct rte_mbuf* pkts[BATCH_SIZE];
    while (1) {
      uint16_t recv = vport_->recv_pkts(pkts, BATCH_SIZE);
      // TODO(john-b-yang): Replace pktburst functionality with atomic multilog
      // Refer to packetstore code and reimplement with confluo code.
      // handle_->insert_pktburst(pkts, recv);

      rec_pkts_ += recv;
    }
  }

  /**
   * Return number of cores.
   */
  int core() {
    return core_;
  }

  /**
   * Return number of packets received since calling start method.
   */
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
  confluo::atomic_multilog* mlog;
};

}

#endif  // NETPLAY_WRITER_H_
