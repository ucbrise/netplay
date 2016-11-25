#ifndef PKTGEN_H_
#define PKTGEN_H_

#include <stdint.h>
#include <stdlib.h>
#include <sys/time.h>

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

#define REPORT_INTERVAL_SECS    10

template<typename iface_init>
class packet_generator {
 public:
  packet_generator(netplay::dpdk::virtual_port<iface_init>* vport,
                   uint64_t rate, uint64_t time_limit, int core)
    : bucket_(token_bucket(rate, TOKEN_BUCKET_CAPACITY)) {
    srand (time(NULL));

    vport_ = vport;
    rate_ = rate;
    time_limit_ = time_limit;
    sent_pkts_ = 0;
    core_ = core;
  }

  void generate(struct rte_mempool* mempool) {
    int ret = netplay::dpdk::mempool::mbuf_alloc_bulk(pkts_, HEADER_SIZE, RTE_BURST_SIZE, mempool);
    if (ret != 0) {
      fprintf(stderr, "Error allocating packets %d\n", ret);
      exit(-1);
    }

    uint64_t start = cursec();
    uint64_t epoch = start;
    uint64_t now;
    while (time_limit_ == 0 || cursec() - start < time_limit_) {
      update_pktbuf();
      if (rate_ == 0 || bucket_.consume(RTE_BURST_SIZE))
        sent_pkts_ += vport_->send_pkts(pkts_, RTE_BURST_SIZE);

      if ((now = cursec()) - epoch >= REPORT_INTERVAL_SECS) {
        double pkt_rate = (double) sent_pkts_ / (double) (now - start);
        fprintf(stderr, "[Core %d] Packet rate = %lf\n",
                core_, pkt_rate);
        epoch = now;
      }
    }
  }

  int core() {
    return core_;
  }

 private:
  uint64_t cursec() {
    struct timeval t;
    gettimeofday(&t, NULL);
    return t.tv_sec;
  }

  void update_pktbuf() {
    // Generate random packets
    for (int i = 0; i < RTE_BURST_SIZE; i++) {
      struct ether_hdr* eth = rte_pktmbuf_mtod(pkts_[i], struct ether_hdr*);
      eth->d_addr.addr_bytes[5] = 0;
      eth->s_addr.addr_bytes[5] = 1;
      eth->ether_type = rte_cpu_to_be_16(0x0800);

      struct ipv4_hdr *ip = (struct ipv4_hdr *) (eth + 1);
      ip->src_addr = rand() % 256;
      ip->dst_addr = rand() % 256;
      ip->next_proto_id = IPPROTO_TCP;

      struct tcp_hdr *tcp = (struct tcp_hdr *) (ip + 1);
      tcp->src_port = rand() % 10;
      tcp->dst_port = rand() % 10;
    }
  }

  struct rte_mbuf* pkts_[RTE_BURST_SIZE];
  netplay::dpdk::virtual_port<iface_init>* vport_;
  uint64_t rate_;
  uint64_t time_limit_;
  token_bucket bucket_;
  uint64_t sent_pkts_;
  int core_;
};

}
}

#endif  // PKTGEN_H_