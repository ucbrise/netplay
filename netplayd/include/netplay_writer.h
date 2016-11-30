#ifndef NETPLAY_WRITER_H_
#define NETPLAY_WRITER_H_

#include <sys/time.h>

#include <ctime>
#include <chrono>

#include <rte_mbuf.h>
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
  for (auto& token : tokens)
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

    init_tokens();
  }

  void start() {
    struct rte_mbuf* pkts[BATCH_SIZE];

    uint64_t start = curusec();
    uint64_t epoch = start;
    while (1) {
      uint16_t recv = vport_->recv_pkts(pkts, BATCH_SIZE);
      handle_->insert_pktburst(pkts, recv);
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
          double write_rate = (double) (rec_pkts_ * 1e6) / (double) elapsed;
          double write_rate_tot = (double) (tot_rec_pkts_ * 1e6) / (double) elapsed_tot;
          fprintf(stderr, "[Core %d] %" PRIu64 " packets read in last epoch "
                  "(%" PRIu64 " secs, %lf pkts/s, tot: %lf pkts/s)...\n", 
                  core_, rec_pkts_, elapsed_sec, write_rate, write_rate_tot);
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
  inline uint64_t curusec() {
    using namespace ::std::chrono;
    auto ts = steady_clock::now().time_since_epoch();
    return duration_cast<std::chrono::microseconds>(ts).count();
  }

  int core_;
  dpdk::virtual_port<vport_init>* vport_;

  packet_store::handle* handle_;
  slog::token_list tokens_;

  uint64_t rec_pkts_;
  uint64_t req_pkts_;
  uint64_t tot_rec_pkts_;
};

}

#endif  // NETPLAY_WRITER_H_