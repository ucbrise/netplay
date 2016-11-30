#ifndef FILTEROPS_H_
#define FILTEROPS_H_

#include <inttypes.h>

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
    uint32_t _ts = *((uint32_t*) ts);
    return _ts >= min && _ts <= max;
  }
};

struct basic_filter {
 public:
  basic_filter() {
    index_id_ = UINT32_MAX;
    token_beg_ = UINT64_MAX;
    token_end_ = UINT64_MAX;
  }

  basic_filter(uint32_t index_id, unsigned char* token_beg,
               unsigned char* token_end) {
    index_id_ = index_id;
    token_beg_ = *(uint64_t*) token_beg;
    token_end_ = *(uint64_t*) token_end;
  }

  basic_filter(uint32_t index_id, uint64_t token_beg, uint64_t token_end) {
    index_id_ = index_id;
    token_beg_ = token_beg;
    token_end_ = token_end;
  }

  basic_filter(uint32_t index_id, unsigned char* token)
    : basic_filter(index_id, token, token) {
  }

  basic_filter(uint32_t index_id, uint64_t token)
    : basic_filter(index_id, token, token) {
  }

  basic_filter& operator=(const basic_filter& filter) {
    index_id_ = filter.index_id_;
    token_beg_ = filter.token_beg_;
    token_end_ = filter.token_end_;
    return *this;
  }

  bool operator==(const basic_filter& other) {
    return index_id_ == other.index_id_ && token_beg_ == other.token_beg_ &&
           token_end_ == other.token_end_;
  }

  uint32_t index_id() const {
    return index_id_;
  }

  uint64_t token_beg() const {
    return token_beg_;
  }

  uint64_t token_end() const {
    return token_end_;
  }

  void token_beg(uint64_t val) {
    token_beg_ = val;
  }

  void token_end(uint64_t val) {
    token_end_ = val;
  }

 protected:
  uint32_t index_id_;
  uint64_t token_beg_;
  uint64_t token_end_;
  /* TODO: Add negation */
};

typedef std::vector<basic_filter> filter_conjunction;
typedef std::vector<filter_conjunction> filter_query;

void print_filter_query(const filter_query& query) {
  fprintf(stderr, "OR(");
  for (size_t i = 0; i < query.size(); i++) {
    fprintf(stderr, "AND(");
    filter_conjunction conj = query[i];
    for (size_t j = 0; j < conj.size(); j++) {
      basic_filter f = conj[j];
      fprintf(stderr, "BasicFilter(%" PRIu32 ": %" PRIu64 ", %" PRIu64 ")",
              f.index_id(), f.token_beg(), f.token_end());
      if (j != conj.size() - 1)
        fprintf(stderr, ", ");
    }
    fprintf(stderr, ")");
    if (i != query.size() - 1)
      fprintf(stderr, ", ");
  }
  fprintf(stderr, ")");
}

}

#endif /* FILTEROPS_H_ */
