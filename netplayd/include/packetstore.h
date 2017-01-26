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
#include "complex_character_index.h"
#include "packet_filter.h"
#include "query_plan.h"
#include "aggregates.h"
#include "packet_attributes.h"

#define MAX_FILTERS 65536

#ifndef INDEX_SRC_IP
#define INDEX_SRC_IP 1
#endif

#ifndef INDEX_DST_IP
#define INDEX_DST_IP 1
#endif

#ifndef INDEX_SRC_PORT
#define INDEX_SRC_PORT 1
#endif

#ifndef INDEX_DST_PORT
#define INDEX_DST_PORT 1
#endif

#ifndef INDEX_TS
#define INDEX_TS 1
#endif

#ifndef NUM_INDEXES
#define NUM_INDEXES (INDEX_SRC_IP + INDEX_DST_IP + INDEX_SRC_PORT + INDEX_DST_PORT + INDEX_TS)
#endif

namespace netplay {

struct flow_stats {
  uint32_t cur_seq;
  uint64_t cur_ts;

  flow_stats() {
    cur_seq = 0;
    cur_ts = 0;
  }

  void push_back(uint64_t val) {
    assert(val > 0);
  }

  size_t storage_size() {
    return sizeof(uint32_t) + sizeof(uint64_t);
  }
};

struct loss_info {
  std::atomic<size_t> retransmissions;
  slog::entry_list* list;

  loss_info() {
    retransmissions.store(0);
    list = new slog::entry_list;
  }

  void increment(uint64_t pkt_id) {
    retransmissions.fetch_add(1);
    list->push_back(pkt_id);
  }

  size_t get() {
    return retransmissions.load();
  }

  void push_back(uint64_t val) {
    assert(val > 0);
  }

  size_t storage_size() {
    return sizeof(uint32_t);
  }
};

typedef slog::__index_depth2<65536, 65536, loss_info> loss_idx;
typedef slog::__index_depth2<65536, 65536, flow_stats> flow_idx;

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
  typedef complex_character_index::result filter_result;
  typedef aggregate::count<attribute::packet_header> packet_counter;

  class handle : public slog::log_store::handle {
   public:
    handle(packet_store& store)
      : slog::log_store::handle(store),
        store_(store) {
    }

    inline uint64_t curusec() {
      using namespace ::std::chrono;
      auto ts = steady_clock::now().time_since_epoch();
      return duration_cast<std::chrono::microseconds>(ts).count();
    }

    void insert_pktburst(struct rte_mbuf** pkts, uint16_t cnt) {
      // uint64_t now = curusec();
      // uint32_t now_s = now / 1e6;
      uint64_t id = store_.olog_->request_id_block(cnt);
      uint64_t nbytes = cnt * sizeof(uint64_t);
      for (int i = 0; i < cnt; i++)
        nbytes += rte_pktmbuf_pkt_len(pkts[i]);
      uint64_t off = store_.request_bytes(nbytes);

      // size_t num_chars = store_.num_filters_.load(std::memory_order_acquire);
      // auto char_index = store_.char_idx_->get(now_s);

      // auto time_list = store_.timestamp_idx_->get(now_s);
      // time_list->push_back_range(id, id + cnt - 1);

      for (int i = 0; i < cnt; i++) {
        unsigned char* pkt = rte_pktmbuf_mtod(pkts[i], unsigned char*);
        uint16_t pkt_size = rte_pktmbuf_pkt_len(pkts[i]);

        struct ether_hdr *eth = (struct ether_hdr *) pkt;
        struct ipv4_hdr *ip = (struct ipv4_hdr *) (eth + 1);
        // store_.srcip_idx_->add_entry(ip->src_addr, id);
        // store_.dstip_idx_->add_entry(ip->dst_addr, id);

        struct tcp_hdr *tcp = (struct tcp_hdr *) (ip + 1);
        int32_t *path = (int32_t *) (tcp + 1);
        uint64_t pkt_ts = *((uint64_t *) (path + 6));
        store_.cur_ts = pkt_ts;

        uint32_t pkt_s = pkt_ts / 1e6;
        store_.timestamp_idx_->add_entry(pkt_s, id);

        // store_.srcport_idx_->add_entry(tcp->src_port, id);
        // store_.dstport_idx_->add_entry(tcp->dst_port, id);
        flow_stats* stats = store_.flow_idx_->get(ip->src_addr);
        loss_info* retr = store_.loss_idx_->get(pkt_s);
        if (tcp->sent_seq > stats->cur_seq) {
          fprintf(stderr, "[%" PRIu32 "] stats->seq = %" PRIu32 "\n", ip->src_addr, stats->cur_seq);
          stats->cur_seq = tcp->sent_seq;
          stats->cur_ts = pkt_ts;
        } else if (pkt_ts - stats->cur_ts > 3000) {
          fprintf(stderr, "[%" PRIu32 "] tcp->seq = %" PRIu32 "\n", ip->src_addr, tcp->sent_seq);
          retr->increment(id);
        } 

        store_.olog_->set_without_alloc(id, off, pkt_size);
        off += store_.append_pkt(off, 0, pkt, pkt_size);
        // for (size_t i = 0; i < num_chars; i++) {
        //   for (auto& filter : store_.filters_[i]) {
        //     if (filter.apply(pkt)) {
        //       char_index->get(i)->push_back(id);
        //       break;
        //     }
        //   }
        // }
        id++;
      }
      store_.olog_->end(id, cnt);
    }

    uint64_t approx_pkt_count(const id_t index_id, const uint64_t tok_beg,
                              const uint64_t tok_end) const {
      return store_.approx_pkt_count(index_id, tok_beg, tok_end);
    }

    void filter_pkts(result_type& results, query_plan& plan) const {
      store_.filter_pkts(results, plan);
    }

    template<typename aggregate_type>
    typename aggregate_type::result_type execute_cast(query_plan& plan) {
      return store_.execute_cast<aggregate_type>(plan);
    }

    filter_result complex_character_lookup(const id_t char_id,
                                           const uint32_t ts_beg, const uint32_t ts_end) {
      return store_.complex_character_lookup(char_id, ts_beg, ts_end);
    }

    template<typename aggregate_type>
    typename aggregate_type::result_type query_character(const uint32_t char_id,
        const uint32_t ts_beg,
        const uint32_t ts_end) {
      return store_.query_character<aggregate_type>(char_id, ts_beg, ts_end);
    }

    id_t srcip_idx() const {
      return store_.srcip_idx_id_;
    }

    id_t dstip_idx() const {
      return store_.dstip_idx_id_;
    }

    id_t srcport_idx() const {
      return store_.srcport_idx_id_;
    }

    id_t dstport_idx() const {
      return store_.dstport_idx_id_;
    }

    id_t timestamp_idx() const {
      return store_.timestamp_idx_id_;
    }

    uint64_t num_pkts() const {
      return store_.num_pkts();
    }

    std::pair<uint32_t, size_t> get_retransmissions() {
      return store_.get_retransmissions();
    }

    void diagnose_outcast_1(uint32_t ts, std::map<uint32_t, size_t>& src_dist,
                            std::map<int32_t, size_t>& switch_dist) {
      return store_.diagnose_outcast_1(ts, src_dist, switch_dist);
    }

    void diagnose_outcast_2(uint32_t ts, std::map<uint32_t, size_t>& src_dist,
                            std::map<int32_t, size_t>& switch_dist) {
      return store_.diagnose_outcast_2(ts, src_dist, switch_dist);
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
    cur_ts = 0;

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

    flow_idx_ = new flow_idx;
    loss_idx_ = new loss_idx;

    char_idx_ = new complex_character_index();
    num_filters_.store(0U, std::memory_order_release);
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
   * Add a new complex character with specified packet filter.
   *
   * @param filter The packet filter.
   * @return The id of the newly created complex character.
   */
  uint32_t add_complex_character(const filter_list& filter) {
    size_t idx = num_filters_.fetch_add(1UL, std::memory_order_release);
    filters_[idx] = filter;
    return idx;
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
      auto res = filter(idx_id, tok_beg, tok_end, max_rid);
      if (cplan.perform_pkt_filter) {
        auto pf_res = packet_filter_result(res, cplan.pkt_filter, dlog_, olog_);
        results.insert(pf_res.begin(), pf_res.end());
      } else {
        results.insert(res.begin(), res.end());
      }
    }
  }

  template<typename aggregate_type>
  typename aggregate_type::result_type execute_cast(query_plan& plan) {
    result_type result;
    filter_pkts(result, plan);
    return aggregate_type::aggregate(result);
  }

  filter_result complex_character_lookup(const uint32_t char_id,
                                         const uint32_t ts_beg,
                                         const uint32_t ts_end) {
    std::pair<uint64_t, uint64_t> time_range(ts_beg, ts_end);
    uint64_t max_rid = olog_->num_ids();
    return char_idx_->filter(max_rid, char_id, time_range);
  }

  template<typename aggregate_type>
  typename aggregate_type::result_type query_character(const uint32_t char_id,
      const uint32_t ts_beg,
      const uint32_t ts_end) {
    filter_result result = complex_character_lookup(char_id, ts_beg, ts_end);
    return aggregate_type::aggregate(result);
  }

  /**
   * Get the number of packets in the packet store.
   *
   * @return Number of packets in the packet store.
   */
  uint64_t num_pkts() const {
    return num_records();
  }

  std::pair<uint32_t, size_t> get_retransmissions() {
    uint32_t cur_s = cur_ts / 1e6;
    return std::pair<uint32_t, size_t>(cur_s, loss_idx_->at(cur_s)->get());
  }

  void diagnose_outcast_1(uint32_t ts, std::map<uint32_t, size_t>& src_dist,
                          std::map<int32_t, size_t>& switch_dist) {
    auto pkt_ids = timestamp_idx_->get(ts);
    size_t size = pkt_ids->size();
    for (size_t i = 0; i < size; i++) {
      uint64_t pkt_id = pkt_ids->at(i);
      uint64_t off;
      uint16_t len;
      olog_->lookup(pkt_id, off, len);
      unsigned char *ptr = (unsigned char*) dlog_->ptr(off);
      unsigned char *pkt = ptr + sizeof(uint64_t);
      struct ether_hdr *eth = (struct ether_hdr *) pkt;
      struct ipv4_hdr *ip = (struct ipv4_hdr *) (eth + 1);
      src_dist[ip->src_addr]++;
      struct tcp_hdr *tcp = (struct tcp_hdr *) (ip + 1);
      int32_t *path = (int32_t *)  (tcp + 1);

      uint32_t pos = 0;
      while (pos < 5 && path[pos + 1] != -1) pos++;
      if (path[pos] == -1) continue;

      switch_dist[path[pos]]++;
    }
  }

  void diagnose_outcast_2(uint32_t ts, std::map<uint32_t, size_t>& src_dist,
                          std::map<int32_t, size_t>& switch_dist) {
    auto pkt_ids = loss_idx_->get(ts)->list;
    size_t size = pkt_ids->size();
    for (size_t i = 0; i < size; i++) {
      uint64_t pkt_id = pkt_ids->at(i);
      uint64_t off;
      uint16_t len;
      olog_->lookup(pkt_id, off, len);
      unsigned char *ptr = (unsigned char*) dlog_->ptr(off);
      unsigned char *pkt = ptr + sizeof(uint64_t);
      struct ether_hdr *eth = (struct ether_hdr *) pkt;
      struct ipv4_hdr *ip = (struct ipv4_hdr *) (eth + 1);
      src_dist[ip->src_addr]++;
      struct tcp_hdr *tcp = (struct tcp_hdr *) (ip + 1);
      int32_t *path = (int32_t *)  (tcp + 1);

      uint32_t pos = 0;
      while (pos < 5 && path[pos + 1] != -1) pos++;
      if (path[pos] == -1) continue;

      switch_dist[path[pos]]++;
    }
  }

 private:

  void print_ip(uint32_t ip) {
    unsigned char bytes[4];
    bytes[0] = ip & 0xFF;
    bytes[1] = (ip >> 8) & 0xFF;
    bytes[2] = (ip >> 16) & 0xFF;
    bytes[3] = (ip >> 24) & 0xFF;
    fprintf(stderr, "%u.%u.%u.%u", bytes[0], bytes[1], bytes[2], bytes[3]);
  }

  /**
   * Append a packet to the packet store.
   *
   * @param record The buffer containing record data.
   * @param record_len The length of the buffer.
   * @param offset The offset into the log where data should be written.
   */
  size_t append_pkt(uint64_t offset, uint64_t ts, unsigned char* pkt, uint16_t pkt_len) {

    /* We can append the value to the log without locking since this
     * thread has exclusive access to the region (offset, offset + sizeof(uint64_t) + pkt_len).
     */
    unsigned char* loc = (unsigned char*) dlog_->ptr(offset);
    memcpy(loc, &ts, sizeof(uint64_t));
    memcpy(loc + sizeof(uint64_t), pkt, pkt_len);
    return pkt_len + sizeof(uint64_t);
  }

  id_t srcip_idx_id_;
  id_t dstip_idx_id_;
  id_t srcport_idx_id_;
  id_t dstport_idx_id_;
  id_t timestamp_idx_id_;

  slog::__index4* srcip_idx_;
  slog::__index4* dstip_idx_;
  slog::__index2* srcport_idx_;
  slog::__index2* dstport_idx_;
  slog::__index4* timestamp_idx_;

  flow_idx* flow_idx_;
  loss_idx* loss_idx_;

  uint64_t cur_ts;

  /* Complex characters */
  /* Packet filters */
  std::array<filter_list, MAX_FILTERS> filters_;
  std::atomic<uint32_t> num_filters_;
  complex_character_index* char_idx_;
};

template<> packet_store::packet_counter::result_type packet_store::query_character<packet_store::packet_counter>(
  const uint32_t char_id,
  const uint32_t ts_beg,
  const uint32_t ts_end) {
  packet_counter::result_type res = 0;
  for (uint32_t ts = ts_beg; ts <= ts_end; ts++)
    res += char_idx_->get(ts)->get(char_id)->size();
  return res;
}

}

#endif /* PACKETSTORE_H_ */
