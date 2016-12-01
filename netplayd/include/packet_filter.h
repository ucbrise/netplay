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

#include "filterresult.h"

namespace netplay {

struct index_filter {
  typedef std::pair<uint64_t, uint64_t> range;
  uint32_t index_id;
  range tok_range;
};

struct packet_filter {
  inline bool apply(void *pkt, uint32_t ts) const {
    struct ether_hdr *eth = (struct ether_hdr *) pkt;
    struct ipv4_hdr *ip = (struct ipv4_hdr *) (eth + 1);
    if (ip->next_proto_id == IPPROTO_TCP) {
      struct tcp_hdr *tcp = (struct tcp_hdr *) (ip + 1);
      return (ip->src_addr >= src_addr.first && ip->src_addr <= src_addr.second)
             && (ip->dst_addr >= dst_addr.first && ip->dst_addr <= dst_addr.second)
             && (tcp->src_port >= src_port.first && tcp->src_port <= src_port.second)
             && (tcp->dst_port >= dst_port.first && tcp->dst_port <= dst_port.second)
             && (ts >= timestamp.first && ts <= timestamp.second);
    } else if (ip->next_proto_id == IPPROTO_UDP) {
      struct udp_hdr *udp = (struct udp_hdr *) (ip + 1);
      return (ip->src_addr >= src_addr.first && ip->src_addr <= src_addr.second)
             && (ip->dst_addr >= dst_addr.first && ip->dst_addr <= dst_addr.second)
             && (udp->src_port >= src_port.first && udp->src_port <= src_port.second)
             && (udp->dst_port >= dst_port.first && udp->dst_port <= dst_port.second)
             && (ts >= timestamp.first && ts <= timestamp.second);
    }
    return (ip->src_addr >= src_addr.first && ip->src_addr <= src_addr.second)
           && (ip->dst_addr >= dst_addr.first && ip->dst_addr <= dst_addr.second)
           && (ts >= timestamp.first && ts <= timestamp.second);
  }

  typedef std::pair<uint64_t, uint64_t> range;
  range src_addr;
  range dst_addr;
  range src_port;
  range dst_port;
  range timestamp;
};

template<typename index_type>
class packet_filter_result {
 public:
  template<typename T>
  using filter_iterator = typename slog::filter_result<T>::filter_iterator;

  class packet_filter_iterator : public slog::__input_iterator {
   public:
    typedef uint64_t value_type;
    typedef uint64_t difference_type;
    typedef const uint64_t* pointer;
    typedef uint64_t reference;

    packet_filter_iterator(const packet_filter& filter,
                           filter_iterator<index_type>& it)
      : fiter_(filter), it_(it) {}

    reference operator*() const {
      return *it_;
    }

    packet_filter_iterator& operator++() {
      do {
        it_++;
      } while (!filter_.apply(*it_));
      return *this;
    }

    packet_filter_iterator operator++(int) {
      filter_iterator it = *this;
      ++(*this);
      return it;
    }

    bool operator==(filter_iterator other) const {
      return it_ == other.it_;
    }

    bool operator!=(filter_iterator other) const {
      return !(*this == other);
    }

   private:
    const packet_filter& filter_;
    filter_iterator<index_type>& it_;
  };

  packet_filter_result(slog::filter_result<index_type>& res,
                       const packet_filter& filter)
    : res_(res), filter_(filter) {}

  packet_filter_iterator begin() {
    return packet_filter_iterator(filter_, res_.begin());
  }

  packet_filter_iterator end() {
    return packet_filter_iterator(filter_, res_.end());
  }  

 private:
  const packet_filter& filter_;
  slog::filter_result<index_type>& res_;
};



}

#endif /* FILTEROPS_H_ */
