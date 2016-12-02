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
#include "complex_character.h"
#include "packet_filter.h"
#include "query_plan.h"

namespace netplay {

/**
 * A data store for packet headers.
 *
 * Stores entire packet headers, along with 'casts' and 'characters' to enable
 * efficient rich semantics. See https://cs.berkeley.edu/~anuragk/netplay.pdf
 * for details.
 */
class packet_store: public slog::log_store {
 public:
  typedef std::unordered_set<uint64_t> result_type;
  class handle : public slog::log_store::handle {
   public:
    handle(packet_store& store)
      : slog::log_store::handle(store),
        store_(store) {
    }

    void insert_pktburst(struct rte_mbuf** pkts, uint16_t cnt) {
      std::time_t now = std::time(nullptr);
      uint64_t id = store_.olog_->request_id_block(cnt);
      store_.timestamps_->ensure_alloc(id, id + cnt);
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
        store_.dstip_idx_->add_entry(ip->dst_addr, id);
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
        store_.olog_->set(id, off, pkt_size);
        store_.append_record(pkt, pkt_size, off);
        store_.timestamps_->set(id, now);
        uint32_t num_chars = store_.complex_characters_->size();
        for (uint32_t i = 0; i < num_chars; i++)
          store_.complex_characters_->at(i)->check_and_add(id, pkt, now);
        off += pkt_size;
        id++;
      }
      store_.olog_->end(id, cnt);
    }

    uint64_t approx_pkt_count(const uint32_t index_id, const uint64_t tok_beg,
                              const uint64_t tok_end) const {
      return store_.approx_pkt_count(index_id, tok_beg, tok_end);
    }

    void filter_pkts(result_type& results, query_plan& plan) const {
      store_.filter_pkts(results, plan);
    }

    complex_character::result complex_character_lookup(const uint32_t char_id,
                                  const uint32_t ts_beg, const uint32_t ts_end) {
      return store_.complex_character_lookup(char_id, ts_beg, ts_end);
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

    uint64_t num_pkts() const {
      return store_.num_pkts();
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

    timestamps_ = new slog::__monolog_base <uint32_t, 32>();
    complex_characters_ = new slog::monolog_linearizable<complex_character*>();
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
   * Add a new complex character with specified packet filters.
   *
   * @param filters The packet filters.
   * @return The id of the newly created complex character.
   */
  uint32_t add_complex_character(const std::vector<packet_filter>& filters) {
    return complex_characters_->push_back(new complex_character(filters));
  }

  uint64_t approx_pkt_count(const uint32_t index_id, const uint64_t tok_beg,
                            const uint64_t tok_end) const {
    return filter_count(index_id, tok_beg, tok_end);
  }

  /**
   * Filter index entries based on query.
   *
   * @param results The results of the filter query.
   * @param query The filter query.
   */
  void filter_pkts(result_type& results, query_plan& plan) const {
    uint64_t max_rid = olog_->num_ids();

    for (clause_plan& cplan : plan) {
      /* Evaluate the min cardinality filter */
      uint32_t idx_id = cplan.idx_filter.index_id;
      uint64_t tok_beg = cplan.idx_filter.tok_range.first;
      uint64_t tok_end = cplan.idx_filter.tok_range.second;

      std::unordered_set<uint64_t> filter_res;
      if (idx_id == srcip_idx_id_) {
        auto res = filter(srcip_idx_, tok_beg, tok_end, max_rid);
        if (cplan.perform_pkt_filter) {
          auto pf_res = build_result(res, cplan.pkt_filter, dlog_, olog_, timestamps_);
          results.insert(pf_res.begin(), pf_res.end());
        } else {
          results.insert(res.begin(), res.end());
        }
      } else if (idx_id == dstip_idx_id_) {
        auto res = filter(dstip_idx_, tok_beg, tok_end, max_rid);
        if (cplan.perform_pkt_filter) {
          auto pf_res = build_result(res, cplan.pkt_filter, dlog_, olog_, timestamps_);
          results.insert(pf_res.begin(), pf_res.end());
        } else {
          results.insert(res.begin(), res.end());
        }
      } else if (idx_id == srcport_idx_id_) {
        auto res = filter(srcport_idx_, tok_beg, tok_end, max_rid);
        if (cplan.perform_pkt_filter) {
          auto pf_res = build_result(res, cplan.pkt_filter, dlog_, olog_, timestamps_);
          results.insert(pf_res.begin(), pf_res.end());
        } else {
          results.insert(res.begin(), res.end());
        }
      } else if (idx_id == dstport_idx_id_) {
        auto res = filter(dstport_idx_, tok_beg, tok_end, max_rid);
        if (cplan.perform_pkt_filter) {
          auto pf_res = build_result(res, cplan.pkt_filter, dlog_, olog_, timestamps_);
          results.insert(pf_res.begin(), pf_res.end());
        } else {
          results.insert(res.begin(), res.end());
        }
      } else if (idx_id == timestamp_idx_id_) {
        auto res = filter(timestamp_idx_, tok_beg, tok_end, max_rid);
        if (cplan.perform_pkt_filter) {
          auto pf_res = build_result(res, cplan.pkt_filter, dlog_, olog_, timestamps_);
          results.insert(pf_res.begin(), pf_res.end());
        } else {
          results.insert(res.begin(), res.end());
        }
      }
    }
  }

  complex_character::result complex_character_lookup(const uint32_t char_id,
      const uint32_t ts_beg,
      const uint32_t ts_end) {
    std::pair<uint64_t, uint64_t> time_range(ts_beg, ts_end);
    uint64_t max_rid = olog_->num_ids();
    complex_character* character = complex_characters_->get(char_id);
    return character->filter(max_rid, time_range, olog_);
  }

  /**
   * Get the number of packets in the packet store.
   *
   * @return Number of packets in the packet store.
   */
  uint64_t num_pkts() const {
    return num_records();
  }

 private:
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

  slog::__monolog_base<uint32_t, 32> *timestamps_;
  slog::monolog_linearizable<complex_character*> *complex_characters_;
};

}

#endif /* PACKETSTORE_H_ */
