#ifndef BENCH_VPORT_H_
#define BENCH_VPORT_H_

#include <functional>

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
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>
#include <rte_lpm.h>
#include <rte_mbuf.h>

#include "packetstore.h"

class pktstore_vport {
 public:
  pktstore_vport(netplay::packet_store::handle* handle) {
    handle_ = handle;
  }

  uint16_t send_pkts(struct rte_mbuf** pkts, uint16_t n_pkts) {
    handle->insert_pktburst(pkts, n_pkts);
    return n_pkts;
  }

  uint16_t recv_pkts(struct rte_mbuf** pkts, uint16_t n_pkts) {
    throw std::bad_function_call();
  }

 private:
  netplay::packet_store::handle* handle_;
};

#endif  // BENCH_VPORT_H_