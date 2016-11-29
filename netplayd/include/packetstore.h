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
      uint64_t nbytes = 0;
      for (int i = 0; i < cnt; i++)
        nbytes += rte_pktmbuf_pkt_len(pkts[i]);
      uint64_t off = base_.request_bytes(data_block_size_);

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
        off += pkt_size;
        id++;
      }
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
   * Get the number of packets in the packet store.
   *
   * @return Number of packets in the packet store.
   */
  uint64_t num_pkts() {
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

  slog::__monolog_base <uint32_t, 32> timestamps_;
};

}

#endif /* PACKETSTORE_H_ */
