#ifndef FILTERS_H_
#define FILTERS_H_

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

#include "filterops.h"

namespace netplay {

struct src_ip_filter {
  static inline bool apply(void* pkt, uint64_t min, uint64_t max) {
    struct ether_hdr *eth = (struct ether_hdr *) pkt;
    struct ipv4_hdr *ip = (struct ipv4_hdr *) (eth + 1);
    return ip->src_addr >= min && ip->src_addr <= max;
  }
};

struct dst_ip_filter {
  static inline bool apply(void* pkt, uint64_t min, uint64_t max) {
    struct ether_hdr *eth = (struct ether_hdr *) pkt;
    struct ipv4_hdr *ip = (struct ipv4_hdr *) (eth + 1);
    return ip->dst_addr >= min && ip->dst_addr <= max;
  }
};

struct src_port_filter {
  static inline bool apply(void* pkt, uint64_t min, uint64_t max) {
    struct ether_hdr *eth = (struct ether_hdr *) pkt;
    struct ipv4_hdr *ip = (struct ipv4_hdr *) (eth + 1);
    if (ip->next_proto_id == IPPROTO_TCP) {
      struct tcp_hdr *tcp = (struct tcp_hdr *) (ip + 1);
      return tcp->src_port >= min && tcp->src_port <= max;
    } else if (ip->next_proto_id == IPPROTO_UDP) {
      struct udp_hdr *udp = (struct udp_hdr *) (ip + 1);
      return udp->src_port >= min && udp->src_port <= max;
    }
    return false;
  }
};

struct dst_port_filter {
  static inline bool apply(void* pkt, uint64_t min, uint64_t max) {
    struct ether_hdr *eth = (struct ether_hdr *) pkt;
    struct ipv4_hdr *ip = (struct ipv4_hdr *) (eth + 1);
    if (ip->next_proto_id == IPPROTO_TCP) {
      struct tcp_hdr *tcp = (struct tcp_hdr *) (ip + 1);
      return tcp->dst_port >= min && tcp->dst_port <= max;
    } else if (ip->next_proto_id == IPPROTO_UDP) {
      struct udp_hdr *udp = (struct udp_hdr *) (ip + 1);
      return udp->dst_port >= min && udp->dst_port <= max;
    }
    return false;
  }
};

struct timestamp_filter {
  static inline bool apply(void* ts, uint64_t min, uint64_t max) {
    uint64_t _ts = *((uint64_t*) ts);
    return _ts >= min && _ts <= max;
  }
};

}

#endif  // FILTERS_H_