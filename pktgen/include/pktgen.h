#ifndef PKTGEN_H_
#define PKTGEN_H_

#include <stdint.h>
#include <stdlib.h>
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
#include <rte_mbuf.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>

#include "virtual_port.h"
#include "dpdk_utils.h"
#include "token_bucket.h"
#include "dpdk_exception.h"

namespace netplay {
namespace pktgen {

#define TOKEN_BUCKET_CAPACITY   64
#define RTE_BURST_SIZE          32
#define HEADER_SIZE             54

#define REPORT_INTERVAL         100000000ULL

class rand_generator {
 public:
  rand_generator(struct rte_mempool* mempool) {
    int ret = netplay::dpdk::mempool::mbuf_alloc_bulk(pkts_, HEADER_SIZE,
              RTE_BURST_SIZE, mempool);
    if (ret != 0) {
      fprintf(stderr, "Error allocating packets %d\n", ret);
      exit(-1);
    }

    for (int i = 0; i < RTE_BURST_SIZE; i++) {
      struct ether_hdr* eth = rte_pktmbuf_mtod(pkts_[i], struct ether_hdr*);
      eth->d_addr.addr_bytes[5] = 0;
      eth->s_addr.addr_bytes[5] = 1;
      eth->ether_type = rte_cpu_to_be_16(0x0800);

      struct ipv4_hdr *ip = (struct ipv4_hdr *) (eth + 1);
      ip->src_addr = 0;
      ip->dst_addr = 0;
      ip->next_proto_id = IPPROTO_TCP;

      struct tcp_hdr *tcp = (struct tcp_hdr *) (ip + 1);
      tcp->src_port = 0;
      tcp->dst_port = 0;
    }
  }

  struct rte_mbuf** generate_batch(size_t size) {
    // Generate random packets
    for (size_t i = 0; i < size; i++) {
      struct ether_hdr* eth = rte_pktmbuf_mtod(pkts_[i], struct ether_hdr*);

      struct ipv4_hdr *ip = (struct ipv4_hdr *) (eth + 1);
      ip->src_addr = rand() % 256;
      ip->dst_addr = rand() % 256;

      struct tcp_hdr *tcp = (struct tcp_hdr *) (ip + 1);
      tcp->src_port = rand() % 10;
      tcp->dst_port = rand() % 10;
    }

    return pkts_;
  }

 private:
  struct rte_mbuf* pkts_[RTE_BURST_SIZE];
};

template<typename vport_type, typename generator_type = rand_generator>
class packet_generator {
 public:
  packet_generator(vport_type* vport, generator_type* generator,
                   uint64_t rate, uint64_t time_limit, uint64_t pkt_limit)
    : bucket_(token_bucket(rate, TOKEN_BUCKET_CAPACITY)) {
    srand (time(NULL));

    rate_ = rate;
    time_limit_ = time_limit;
    pkt_limit_ = pkt_limit;
    sent_pkts_ = 0;
    tot_sent_pkts_ = 0;

    vport_ = vport;
    generator_ = generator;
  }

  void generate() {
    uint64_t start = curusec();
    uint64_t epoch = start;
    while ((time_limit_ == 0 || curusec() - start < time_limit_) && tot_sent_pkts_ < pkt_limit_) {
      if (rate_ == 0 || bucket_.consume(RTE_BURST_SIZE)) {
        struct rte_mbuf** pkts = generator_->generate_batch(RTE_BURST_SIZE);
        uint16_t recv = vport_->send_pkts(pkts, RTE_BURST_SIZE);
        sent_pkts_ += recv;
        tot_sent_pkts_ += recv;
      }

      if (sent_pkts_ >= REPORT_INTERVAL) {
        uint64_t now = curusec();
        double pkt_rate = (double) (sent_pkts_ * 1e6) / (double) (now - epoch);
        epoch = now;
        sent_pkts_ = 0;

        fprintf(stderr, "[PKTGEN] Packet rate = %lf\n", pkt_rate);
      }
    }
  }

  uint64_t total_sent() {
    return tot_sent_pkts_;
  }

 private:
  inline uint64_t curusec() {
    using namespace ::std::chrono;
    auto ts = steady_clock::now().time_since_epoch();
    return duration_cast<std::chrono::microseconds>(ts).count();
  }

  uint64_t rate_;
  uint64_t time_limit_;
  uint64_t pkt_limit_;
  uint64_t sent_pkts_;
  uint64_t tot_sent_pkts_;

  token_bucket bucket_;
  vport_type* vport_;
  generator_type* generator_;
};

}
}

#endif  // PKTGEN_H_