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

#include "packetstore.h"
#include "pktgen.h"
#include "bench_vport.h"
#include "dpdk_utils.h"
#include "cpu_utilization.h"

using namespace ::netplay::dpdk;
using namespace ::netplay::pktgen;
using namespace ::netplay;
using namespace ::slog;
using namespace ::std::chrono;

const char* usage =
  "Usage: %s -n [num-threads] -r [rate-limit]\n";

typedef uint64_t timestamp_t;

static timestamp_t get_timestamp() {
  struct timeval now;
  gettimeofday(&now, NULL);

  return now.tv_usec + (timestamp_t) now.tv_sec * 1000000;
}

#define HEADER_SIZE             54
#define RTE_BURST_SIZE          32
#define PKTS_PER_THREAD         60000000

struct pkt_attrs {
  uint32_t sip;
  uint32_t dip;
  uint16_t sport;
  uint16_t dport;
};

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

  packet_loader() {
    store_ = new packet_store();
  }

  // Throughput benchmarks
  void load_packets(const uint32_t num_threads, const uint64_t rate_limit,
                    const bool measure_cpu) {

    // Generate packets
    for (uint64_t i = 0; i < num_threads * PKTS_PER_THREAD; i++) {
      pkt_attrs attrs;
      attrs.sip = rand() % 256;
      attrs.dip = rand() % 256;
      attrs.sport = rand() % 10;
      attrs.dport = rand() % 10;
      pkt_data_.push_back(attrs);
    }

    typedef packet_generator<pktstore_vport, static_rand_generator> pktgen_type;
    std::vector<std::thread> workers;
    uint64_t worker_rate = rate_limit / num_threads;
    std::atomic<uint64_t> done = 0;
    std::vector<double> thputs(num_threads, 0.0);
    struct rte_mempool* mempool = init_dpdk("pktbench", 0, 0);
    for (uint32_t i = 0; i < num_threads; i++) {
      workers.push_back(std::thread([i, worker_rate, &thputs, &mempool, this] {
        pkt_attrs* buf = &pkt_data_[i * PKTS_PER_THREAD];
        packet_store::handle* handle = store_->get_handle();
        pktstore_vport* vport = new pktstore_vport(handle);
        static_rand_generator* gen = new static_rand_generator(mempool, buf);
        pktgen_type pktgen(vport, gen, worker_rate, 0, PKTS_PER_THREAD);

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
      std::thread cpu_measure_thread([num_threads, &done, this] {
        std::ofstream util_stream("write_cpu_utilization_" + std::to_string(num_threads) + ".txts");
        cpu_utilization util;
        while (done.load() != num_threads) {
          sleep(1);
          util_stream << util.current() << "\n";
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

    std::ofstream ofs("write_throughput_" + std::to_string(num_threads) + ".txt", std::ios_base::app);
    ofs << num_threads << "\t" << tot << "\n";
    ofs.close();
  }

 private:
  packet_store *store_;
  std::vector<pkt_attrs> pkt_data_;
};

void print_usage(char *exec) {
  fprintf(stderr, usage, exec);
}

int main(int argc, char** argv) {
  int c;
  int num_threads = 1;
  uint64_t rate_limit = 0;
  bool measure_cpu = false;
  while ((c = getopt(argc, argv, "n:r:c")) != -1) {
    switch (c) {
    case 'n':
      num_threads = atoi(optarg);
      break;
    case 'r':
      rate_limit = atoll(optarg);
      break;
    case 'c':
      measure_cpu = true;
      break;
    default:
      fprintf(stderr, "Could not parse command line arguments.\n");
      print_usage(argv[0]);
    }
  }

  packet_loader loader;
  loader.load_packets(num_threads, rate_limit, measure_cpu);

  return 0;
}
