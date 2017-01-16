#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>

#include <ctime>
#include <chrono>
#include <random>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cassert>
#include <vector>
#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <iostream>

#include <execinfo.h>
#include <signal.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>
#include <cxxabi.h>

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

#include "rand_generators.h"
#include "critical_error_handler.h"
#include "packetstore.h"
#include "bench_vport.h"
#include "dpdk_utils.h"
#include "pkt_attrs.h"
#include "cpu_utilization.h"
#include "rate_limiter.h"

using namespace ::netplay::dpdk;
using namespace ::netplay;
using namespace ::slog;
using namespace ::std::chrono;

const char* usage = "Usage: %s [-n num-pkts] [-i interval]\n";

typedef uint64_t timestamp_t;

static timestamp_t get_timestamp() {
  struct timeval now;
  gettimeofday(&now, NULL);

  return now.tv_usec + (timestamp_t) now.tv_sec * 1000000;
}

#define HEADER_SIZE             54
#define RTE_BURST_SIZE          32
#define PKTS_PER_THREAD         60000000

class static_rand_generator {
 public:
  static_rand_generator(struct rte_mempool* mempool, pkt_attrs* pkt_data) {
    cur_pos_ = 0;
    pkt_data = pkt_data;

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
    // Get next batch
    for (size_t i = 0; i < size; i++) {
      struct ether_hdr* eth = rte_pktmbuf_mtod(pkts_[i], struct ether_hdr*);

      struct ipv4_hdr *ip = (struct ipv4_hdr *) (eth + 1);
      ip->src_addr = pkt_data[cur_pos_ + i].sip;
      ip->dst_addr = pkt_data[cur_pos_ + i].dip;

      struct tcp_hdr *tcp = (struct tcp_hdr *) (ip + 1);
      tcp->src_port = pkt_data[cur_pos_ + i].sport;
      tcp->dst_port = pkt_data[cur_pos_ + i].dport;
    }
    cur_pos_ += size;

    return pkts_;
  }

 private:
  uint64_t cur_pos_;
  pkt_attrs* pkt_data;

  struct rte_mbuf* pkts_[RTE_BURST_SIZE];
};

class storage_bench {
 public:
  storage_bench() {
    store_ = new packet_store();
  }

  // Storage footprint benchmark
  void load_packets(const uint64_t num_pkts, const uint64_t interval) {

    typedef rate_limiter<pktstore_vport, static_rand_generator> pktgen_type;

    // Generate packets
    zipf_generator gen1(1, 256);
    zipf_generator gen2(1, 10);

    std::vector<pkt_attrs> pkt_data;
    for (uint64_t i = 0; i < num_pkts; i++) {
      pkt_attrs attrs;
      attrs.sip = gen1.next<uint32_t>();
      attrs.dip = gen1.next<uint32_t>();
      attrs.sport = gen2.next<uint16_t>();
      attrs.dport = gen2.next<uint16_t>();
      pkt_data.push_back(attrs);
    }
    fprintf(stderr, "Generated %zu packets.\n", pkt_data.size());

    struct rte_mempool* mempool = init_dpdk("sbench", 0, 0);

    std::ofstream ofs("storage_footprint_" + std::to_string(num_pkts) + "_"
                      + std::to_string(interval) + ".txt", std::ios_base::app);
    for (uint64_t batch_id = 0; batch_id < num_pkts / interval; batch_id++) {
      pkt_attrs* buf = &pkt_data[batch_id * interval];
      packet_store::handle* handle = store_->get_handle();
      pktstore_vport* vport = new pktstore_vport(handle);
      static_rand_generator* gen = new static_rand_generator(mempool, buf);
      pktgen_type pktgen(vport, gen, 0, interval);

      fprintf(stderr, "Starting benchmark.\n");
      timestamp_t start = get_timestamp();
      pktgen.generate();
      timestamp_t end = get_timestamp();
      double totsecs = (double) (end - start) / (1000.0 * 1000.0);

      slog::logstore_storage storage;
      store_->storage_footprint(storage);

      ofs << store_->num_pkts() << "\t" << storage.total() << "\n";

      fprintf(stderr, "Interval %" PRIu64 " in %lf seconds, storage = %zuB.\n",
        batch_id, totsecs, storage.total());

      delete vport;
      delete gen;
      delete handle;
    }

    ofs.close();
  }

 private:
  packet_store *store_;
};

void print_usage(char *exec) {
  fprintf(stderr, usage, exec);
}

int main(int argc, char** argv) {
  struct sigaction sigact;

  sigact.sa_sigaction = crit_err_hdlr;
  sigact.sa_flags = SA_RESTART | SA_SIGINFO;

  if (sigaction(SIGSEGV, &sigact, (struct sigaction *)NULL) != 0) {
    fprintf(stderr, "error setting signal handler for %d (%s)\n",
            SIGSEGV, strsignal(SIGSEGV));

    exit(EXIT_FAILURE);
  }

  int c;
  uint64_t num_pkts = 300000000;
  uint64_t interval = 1000000;
  while ((c = getopt(argc, argv, "n:i:")) != -1) {
    switch (c) {
    case 'n':
      num_pkts = atoll(optarg);
      break;
    case 'i':
      interval = atoll(optarg);
      break;
    default:
      fprintf(stderr, "Could not parse command line arguments.\n");
      print_usage(argv[0]);
    }
  }

  storage_bench bench;
  bench.load_packets(num_pkts, interval);

  return 0;
}
