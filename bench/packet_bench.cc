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
#include "character_builder.h"
#include "bench_vport.h"
#include "dpdk_utils.h"
#include "pkt_attrs.h"
#include "cpu_utilization.h"
#include "rate_limiter.h"

using namespace ::netplay::dpdk;
using namespace ::netplay;
using namespace ::slog;
using namespace ::std::chrono;

const char* usage =
  "Usage: %s [-n num-threads] [-r rate-limit] [-c]\n";

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
    pkt_data_ = pkt_data;

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
      ip->src_addr = pkt_data_[cur_pos_ + i].sip;
      ip->dst_addr = pkt_data_[cur_pos_ + i].dip;

      struct tcp_hdr *tcp = (struct tcp_hdr *) (ip + 1);
      tcp->src_port = pkt_data_[cur_pos_ + i].sport;
      tcp->dst_port = pkt_data_[cur_pos_ + i].dport;
    }
    cur_pos_ += size;

    return pkts_;
  }

 private:
  uint64_t cur_pos_;
  pkt_attrs* pkt_data_;

  struct rte_mbuf* pkts_[RTE_BURST_SIZE];
};

class packet_loader {
 public:
  static const uint64_t kMaxPktsPerThread = 60 * 1e6;

  packet_loader(bool add_filters, std::string& filters_file) {
    store_ = new packet_store();
    if (add_filters) {
      load_filters(filters_file);
    }
  }

  // Throughput benchmarks
  void load_packets(const uint32_t num_threads, const uint64_t rate_limit,
                    const bool measure_cpu) {

    typedef rate_limiter<pktstore_vport, static_rand_generator> pktgen_type;
    std::vector<std::thread> workers;
    uint64_t worker_rate = rate_limit / num_threads;
    uint64_t num_pkts = PKTS_PER_THREAD;
    if (worker_rate != 0) {
      num_pkts = worker_rate * 60;
    }

    // Generate packets
    zipf_generator gen1(1, 256);
    zipf_generator gen2(1, 10);
    for (uint64_t i = 0; i < num_threads * num_pkts; i++) {
      pkt_attrs attrs;
      attrs.sip = gen1.next<uint32_t>();
      attrs.dip = gen1.next<uint32_t>();
      attrs.sport = gen2.next<uint16_t>();
      attrs.dport = gen2.next<uint16_t>();
      pkt_data_.push_back(attrs);
    }
    fprintf(stderr, "Generated %zu packets.\n", pkt_data_.size());

    std::atomic<uint32_t> done;
    done.store(0);
    std::vector<double> thputs(num_threads, 0.0);
    struct rte_mempool* mempool = init_dpdk("pktbench", 0, 0);
    for (uint32_t i = 0; i < num_threads; i++) {
      workers.push_back(std::thread([i, worker_rate, num_pkts, &thputs, &done, &mempool, this] {
        pkt_attrs* buf = &pkt_data_[i * num_pkts];
        packet_store::handle* handle = store_->get_handle();
        pktstore_vport* vport = new pktstore_vport(handle);
        static_rand_generator* gen = new static_rand_generator(mempool, buf);
        pktgen_type pktgen(vport, gen, worker_rate, num_pkts);

        fprintf(stderr, "Starting benchmark.\n");
        timestamp_t start = get_timestamp();
        pktgen.generate();
        done.fetch_add(1);
        timestamp_t end = get_timestamp();
        double totsecs = (double) (end - start) / (1000.0 * 1000.0);
        thputs[i] = ((double) pktgen.total_sent() / totsecs);
        fprintf(stderr, "Thread #%u(%lfs): Throughput: %lf.\n", i, totsecs, thputs[i]);

        delete vport;
        delete gen;
        delete handle;
      }));

      // Create a cpu_set_t object representing a set of CPUs. Clear it and mark
      // only CPU i as set.
      cpu_set_t cpuset;
      CPU_ZERO(&cpuset);
      CPU_SET(i, &cpuset);
      int rc = pthread_setaffinity_np(workers.back().native_handle(),
                                      sizeof(cpu_set_t), &cpuset);
      if (rc != 0)
        fprintf(stderr, "Error calling pthread_setaffinity_np: %d\n", rc);
    }

    if (measure_cpu) {
      std::thread cpu_measure_thread([num_threads, rate_limit, &done, this] {
        std::ofstream util_stream("write_cpu_utilization_" + std::to_string(NUM_INDEXES) + "_" + std::to_string(num_threads) + "_" + std::to_string(rate_limit) + ".txt");
        cpu_utilization util;
        while (done.load() != num_threads) {
          util_stream << util.current() << "\n";
          util_stream.flush();
          sleep(1);
        }
        util_stream.close();
      });

      // Create a cpu_set_t object representing a set of CPUs. Clear it and mark
      // only CPU i as set.
      cpu_set_t cpuset;
      CPU_ZERO(&cpuset);
      CPU_SET(num_threads, &cpuset);
      int rc = pthread_setaffinity_np(cpu_measure_thread.native_handle(),
                                      sizeof(cpu_set_t), &cpuset);
      if (rc != 0)
        fprintf(stderr, "Error calling pthread_setaffinity_np: %d\n", rc);

      cpu_measure_thread.join();
    }

    for (auto& th : workers)
      th.join();

    double tot = 0.0;
    for (double thput : thputs)
      tot += thput;

    std::ofstream ofs("write_throughput_" + std::to_string(NUM_INDEXES) + "_" + std::to_string(num_threads) + "_" + std::to_string(rate_limit) + ".txt", std::ios_base::app);
    ofs << tot << "\n";
    ofs.close();

    fprintf(stderr, "Completed loading packets\n");
  }

 private:
  void load_filters(std::string& filters_file) {
    std::vector<std::string> filters;
    fprintf(stderr, "Loading filters...\n");
    std::ifstream in(filters_file);
    if (!in.is_open()) {
      fprintf(stderr, "Could not open filters file %s\n", filters_file.c_str());
      exit(-1);
    }

    std::string exp;
    while (std::getline(in, exp)) {
      auto c = character_builder(store_, exp).build();
      characters_.push_back(c);
    }
    fprintf(stderr, "Loaded %zu filters.\n", characters_.size());
  }

  packet_store *store_;
  std::vector<complex_character> characters_;
  std::vector<pkt_attrs> pkt_data_;
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
  int num_threads = 1;
  uint64_t rate_limit = 0;
  bool add_filters = false;
  std::string filters_file = "";
  bool measure_cpu = false;
  while ((c = getopt(argc, argv, "n:r:f:c")) != -1) {
    switch (c) {
    case 'n':
      num_threads = atoi(optarg);
      break;
    case 'r':
      rate_limit = atoll(optarg);
      break;
    case 'f':
      add_filters = true;
      filters_file = std::string(optarg);
      break;
    case 'c':
      measure_cpu = true;
      break;
    default:
      fprintf(stderr, "Could not parse command line arguments.\n");
      print_usage(argv[0]);
    }
  }

  packet_loader loader(add_filters, filters_file);
  loader.load_packets(num_threads, rate_limit, measure_cpu);

  return 0;
}
