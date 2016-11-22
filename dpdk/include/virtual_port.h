#ifndef VIRTUAL_PORT_H_
#define VIRTUAL_PORT_H_

#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <rte_config.h>
#include <rte_malloc.h>
#include <rte_ring.h>
#include <rte_cycles.h>
#include <rte_timer.h>
#include <rte_errno.h>
#include <rte_eth_ring.h>
#include <rte_ethdev.h>
#include <rte_eal.h>
#include <rte_ether.h>
#include <rte_mbuf.h>

#include "dpdk_utils.h"

namespace netplay {
namespace dpdk {

template<typename initializer>
class virtual_port {
 public:
  virtual_port(const char* iface, struct rte_mempool* mempool) {
    port_ = init_(iface, mempool);
  }

  ~virtual_port() {
  }

  uint16_t send_pkts(mbuf_array_t pkts, uint16_t n_pkts) {
    return rte_eth_tx_burst(port_, static_cast<uint16_t>(0), pkts, n_pkts);
  }

  uint16_t recv_pkts(mbuf_array_t pkts, uint16_t n_pkts) {
    return rte_eth_rx_burst(port_, static_cast<uint16_t>(0), pkts, n_pkts);
  }

 private:
  initializer init_;
  int port_;
};

}
}

#endif  // VIRTUAL_PORT_H_