#ifndef NETPLAY_PACKETSTORE_H_
#define NETPLAY_PACKETSTORE_H_

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

    uint32_t srcip_idx() {
      return store_.srcip_idx_id_;
    }

    uint32_t dstip_idx() {
      return store_.dstip_idx_id_;
    }

    uint32_t srcport_idx() {
      return store_.srcport_idx_id_;
    }

    uint32_t dstport_idx() {
      return store_.dstport_idx_id_;
    }

    uint32_t timestamp_idx() {
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

    srcip_idx_ = get_index<slog::__index4>(srcip_idx_id_);
    dstip_idx_ = get_index<slog::__index4>(dstip_idx_id_);
    srcport_idx_ = get_index<slog::__index2>(srcport_idx_id_);
    dstport_idx_ = get_index<slog::__index2>(dstport_idx_id_);
    timestamp_idx_ = get_index<slog::__index4>(timestamp_idx_id_);
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

  __monolog_base <uint32_t, 32> timestamps_;
};

}

#endif /* NETPLAY_PACKETSTORE_H_ */
