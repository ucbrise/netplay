#ifndef PACKETSTORE_H_
#define PACKETSTORE_H_

#include <ctime>

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

#include "logstore.h"
#include "filters.h"

namespace netplay {

/**
 * A data store for packet header data.
 *
 * Stores entire packet headers, along with 'casts' and 'characters' to enable
 * efficient rich semantics. See https://cs.berkeley.edu/~anuragk/netplay.pdf
 * for details.
 */
class packet_store: public slog::log_store {
 public:

  /** Type definitions **/
  // typedef slog::log_store::handle handle;
  class handle : public slog::log_store::handle {
   public:
    handle(packet_store& store)
      : slog::log_store::handle(store),
        store_(store) {
    }

    void insert_pktburst(struct rte_mbuf** pkts, uint16_t cnt) {
      std::time_t now = std::time(nullptr);
      uint64_t id = store_.olog_->request_id_block(cnt);
      store_.timestamps_.ensure_alloc(id, id + cnt);
      uint64_t nbytes = 0;
      for (int i = 0; i < cnt; i++)
        nbytes += rte_pktmbuf_pkt_len(pkts[i]);
      uint64_t off = store_.request_bytes(nbytes);

      for (int i = 0; i < cnt; i++) {
        unsigned char* pkt = rte_pktmbuf_mtod(pkts[i], unsigned char*);
        uint16_t pkt_size = rte_pktmbuf_pkt_len(pkts[i]);
        struct ether_hdr *eth = (struct ether_hdr *) pkt;
        struct ipv4_hdr *ip = (struct ipv4_hdr *) (eth + 1);
        store_.srcip_idx_->add_entry(ip->src_addr, id);
        store_.dstip_idx_->add_entry(ip->src_addr, id);
        if (ip->next_proto_id == IPPROTO_TCP) {
          struct tcp_hdr *tcp = (struct tcp_hdr *) (ip + 1);
          store_.srcport_idx_->add_entry(tcp->src_port, id);
          store_.dstport_idx_->add_entry(tcp->dst_port, id);
        } else if (ip->next_proto_id == IPPROTO_UDP) {
          struct udp_hdr *udp = (struct udp_hdr *) (ip + 1);
          store_.srcport_idx_->add_entry(udp->src_port, id);
          store_.dstport_idx_->add_entry(udp->dst_port, id);
        }
        store_.timestamp_idx_->add_entry(now, id);
        store_.append_record(pkt, pkt_size, off);
        store_.timestamps_.set(id, now);
        store_.olog_->end(id);
        off += pkt_size;
        id++;
      }
    }

    void filter_pkts(std::unordered_set<uint64_t>& results,
                     slog::filter_query& query) {
      store_.filter_pkts(results, query);
    }

    void add_src_ip(slog::token_list& list, uint32_t src_ip) {
      list.push_back(slog::token_t(store_.srcip_idx_id_, src_ip));
    }

    void add_dst_ip(slog::token_list& list, uint32_t dst_ip) {
      list.push_back(slog::token_t(store_.dstip_idx_id_, dst_ip));
    }

    void add_src_port(slog::token_list& list, uint16_t src_port) {
      list.push_back(slog::token_t(store_.srcport_idx_id_, src_port));
    }

    void add_dst_port(slog::token_list& list, uint16_t dst_port) {
      list.push_back(slog::token_t(store_.dstport_idx_id_, dst_port));
    }

    void add_timestamp(slog::token_list& list, uint32_t timestamp) {
      list.push_back(slog::token_t(store_.timestamp_idx_id_, timestamp));
    }

    uint32_t srcip_idx() const {
      return store_.srcip_idx_id_;
    }

    uint32_t dstip_idx() const {
      return store_.dstip_idx_id_;
    }

    uint32_t srcport_idx() const {
      return store_.srcport_idx_id_;
    }

    uint32_t dstport_idx() const {
      return store_.dstport_idx_id_;
    }

    uint32_t timestamp_idx() const {
      return store_.timestamp_idx_id_;
    }

   private:
    packet_store& store_;
  };

  /**
   * Constructor to initialize the packet store.
   *
   * By default, the packet store creates indexes on 5 fields:
   * Source IP, Destination IP, Source Port, Destination Port and Timestamp.
   */
  packet_store() {
    srcip_idx_id_ = add_index(4);
    dstip_idx_id_ = add_index(4);
    srcport_idx_id_ = add_index(2);
    dstport_idx_id_ = add_index(2);
    timestamp_idx_id_ = add_index(4);

    srcip_idx_ = idx4_->at(0);
    dstip_idx_ = idx4_->at(1);
    srcport_idx_ = idx2_->at(0);
    dstport_idx_ = idx2_->at(1);
    timestamp_idx_ = idx4_->at(2);
  }

  /**
   * Get a handle to the packet store. Each thread **must** have its own handle
   * -- handles cannot be shared between threads.
   *
   * @return A handle to the packet store.
   */
  handle* get_handle() {
    return new handle(*this);
  }

  /**
   * Filter index entries based on query.
   *
   * @param results The results of the filter query.
   * @param query The filter query.
   */
  void filter_pkts(std::unordered_set<uint64_t>& results,
                   slog::filter_query& query) {
    uint64_t max_rid = olog_->num_ids();
    for (slog::filter_conjunction& conjunction : query) {
      /* Get the min cardinality filter */
      uint64_t min_count = UINT64_MAX;
      slog::basic_filter f;
      for (const slog::basic_filter& basic : conjunction) {
        uint64_t cnt;
        if ((cnt = filter_count(basic)) < min_count) {
          min_count = cnt;
          f = basic;
        }
      }

      /* Evaluate the min cardinality filter */
      std::unordered_set<uint64_t> filter_res;
      std::unordered_set<uint64_t> empty;
      filter(filter_res, f, max_rid, empty);

      /* Iterate through its entries, eliminating those that don't match */
      typedef std::unordered_set<uint64_t>::iterator iterator_t;
      for (iterator_t it = filter_res.begin(); it != filter_res.end();) {
        uint64_t off;
        uint16_t len;
        olog_->lookup(*it, off, len);
        if (check_filters(*it, dlog_->ptr(off), conjunction, f))
          it++;
        else
          it = filter_res.erase(it);
      }

      /* Add filtered results to final results */
      results.insert(filter_res.begin(), filter_res.end());
    }
  }

  /**
   * Get the number of packets in the packet store.
   *
   * @return Number of packets in the packet store.
   */
  uint64_t num_pkts() {
    return num_records();
  }

 private:
  bool check_filters(uint64_t id, void *pkt, const slog::filter_conjunction& conjunction,
                     const slog::basic_filter& f) {
    uint64_t ts = timestamps_.get(id);
    for (const slog::basic_filter& basic : conjunction) {
      if (basic == f) continue;
      if (basic.index_id() == srcip_idx_id_ &&
          !src_ip_filter::apply(pkt, basic.token_beg(), basic.token_end())) {
        return false;
      } else if (basic.index_id() == dstip_idx_id_ &&
                 !dst_ip_filter::apply(pkt, basic.token_beg(), basic.token_end())) {
        return false;
      } else if (basic.index_id() == srcport_idx_id_ &&
                 !src_port_filter::apply(pkt, basic.token_beg(), basic.token_end())) {
        return false;
      } else if (basic.index_id() == dstport_idx_id_ &&
                 !dst_port_filter::apply(pkt, basic.token_beg(), basic.token_end())) {
        return false;
      } else if (basic.index_id() == timestamp_idx_id_ &&
                 !timestamp_filter::apply(&ts, basic.token_beg(), basic.token_end())) {
        return false;
      }
    }
    return true;
  }

  uint32_t srcip_idx_id_;
  uint32_t dstip_idx_id_;
  uint32_t srcport_idx_id_;
  uint32_t dstport_idx_id_;
  uint32_t timestamp_idx_id_;

  slog::__index4* srcip_idx_;
  slog::__index4* dstip_idx_;
  slog::__index2* srcport_idx_;
  slog::__index2* dstport_idx_;
  slog::__index4* timestamp_idx_;

  slog::__monolog_base <uint32_t, 32> timestamps_;
};

}

#endif /* PACKETSTORE_H_ */
