#ifndef NETPLAY_WRITER_H_
#define NETPLAY_WRITER_H_

#include <sys/time.h>

#include <ctime>
#include <chrono>

#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>
#include <rte_lpm.h>

#include "packetstore.h"
#include "tokens.h"

namespace netplay {

#define BATCH_SIZE        32
#define REFRESH_INTERVAL  3355443200ULL
#define REPORT_INTERVAL   10000000ULL

void print_pkt(const unsigned char* buf, uint16_t len, slog::token_list& tokens) {
  fprintf(stderr, "[Len: %u, ", len);
  for (uint16_t i = 0; i < len; i++)
    fprintf(stderr, "%x ", buf[i]);
  fprintf(stderr, "; token-list: ");
  for (auto& token: tokens)
    fprintf(stderr, "%u:%" PRIu64 " ", token.index_id(), token.data());
  fprintf(stderr, "]\n");
}

template<typename vport_init>
class netplay_writer {
 public:
  netplay_writer(int core, dpdk::virtual_port<vport_init>* vport, packet_store::handle* handle) {
    rec_pkts_ = 0;
    req_pkts_ = 0;
    tot_rec_pkts_ = 0;
    core_ = core;
    vport_ = vport;
    handle_ = handle;
  }

  void start() {
    struct rte_mbuf* pkts[BATCH_SIZE];

    uint64_t start = curusec();
    uint64_t epoch = start;
    while (1) {
      uint16_t recv = vport_->recv_pkts(pkts, BATCH_SIZE);
      process_batch(pkts, recv);
      rec_pkts_ += recv;
      req_pkts_ += BATCH_SIZE;

      if (req_pkts_ >= REFRESH_INTERVAL || rec_pkts_ >= REPORT_INTERVAL) {
        uint64_t elapsed = curusec() - epoch;
        epoch = curusec();
        uint64_t elapsed_tot = epoch - start;
        uint64_t elapsed_sec = (elapsed / 1e6);
        tot_rec_pkts_ += rec_pkts_;
        if (rec_pkts_ == 0) {
          fprintf(stderr, "[Core %d] WARN: No packets read since last epoch "
                  "(%" PRIu64 " secs)...\n", core_, elapsed_sec);
        } else {
          double write_rate = (double) (tot_rec_pkts_ * 1e6) / (double) elapsed_tot;
          fprintf(stderr, "[Core %d] %" PRIu64 " packets read in last epoch "
                  "(%" PRIu64 " secs, %lf pkts/s)...\n", core_,  rec_pkts_, 
                  elapsed_sec, write_rate);
        }
        fflush(stderr);
        req_pkts_ = 0;
        rec_pkts_ = 0;
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

  inline uint64_t curusec() {
    using namespace ::std::chrono;
    auto ts = steady_clock::now().time_since_epoch();
    return duration_cast<std::chrono::microseconds>(ts).count();
  }

  void process_batch(struct rte_mbuf** pkts, uint16_t cnt) {
    std::time_t now = std::time(nullptr);
    for (int i = 0; i < cnt; i++) {
      slog::token_list tokens;
      uint32_t num_bytes = 0;

      handle_->add_timestamp(tokens, (uint32_t)now);

      void* pkt = rte_pktmbuf_mtod(pkts[i], void*);
      struct ether_hdr *eth = (struct ether_hdr *) pkt;
      num_bytes += sizeof(struct ether_hdr);

      struct ipv4_hdr *ip = (struct ipv4_hdr *) (eth + 1);
      num_bytes += sizeof(struct ipv4_hdr);

      handle_->add_src_ip(tokens, ip->src_addr);
      handle_->add_dst_ip(tokens, ip->dst_addr);
      if (ip->next_proto_id == IPPROTO_TCP) {
        struct tcp_hdr *tcp = (struct tcp_hdr *) (ip + 1);
        num_bytes += sizeof(struct tcp_hdr);

        handle_->add_src_port(tokens, tcp->src_port);
        handle_->add_dst_port(tokens, tcp->dst_port);
      } else if (ip->next_proto_id == IPPROTO_UDP) {
        struct udp_hdr *udp = (struct udp_hdr *) (ip + 1);
        num_bytes += sizeof(struct udp_hdr);

        handle_->add_src_port(tokens, udp->src_port);
        handle_->add_dst_port(tokens, udp->dst_port);
      } else {
        fprintf(stderr, "Unhandled packet type.\n");
        continue;
      }
#ifdef DEBUG
      print_pkt((unsigned char*) pkt, num_bytes, tokens)
#endif
      handle_->insert((unsigned char*) pkt, num_bytes, tokens);
    }
  }

  int core_;
  dpdk::virtual_port<vport_init>* vport_;
  packet_store::handle* handle_;
  uint64_t rec_pkts_;
  uint64_t req_pkts_;
  uint64_t tot_rec_pkts_;
};

}

#endif  // NETPLAY_WRITER_H_