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

#define PKT_LEN   54
#define MAX_PKTS  1000000

using namespace ::netplay::dpdk;
using namespace ::netplay::pktgen;
using namespace ::netplay;
using namespace ::slog;
using namespace ::std::chrono;

const char* usage =
  "Usage: %s -n [numthreads] -r [ratelimit]\n";

typedef uint64_t timestamp_t;

static timestamp_t get_timestamp() {
  struct timeval now;
  gettimeofday(&now, NULL);

  return now.tv_usec + (timestamp_t) now.tv_sec * 1000000;
}

class packet_loader {
 public:
  static const uint64_t kMaxPktsPerThread = 60 * 1e6;

  packet_loader() {
    store_ = new packet_store();
  }

  // Throughput benchmarks
  void load_packets(const uint32_t num_threads, const uint64_t rate_limit,
                    const bool measure_cpu) {
    typedef packet_generator<pktstore_vport, static_rand_generator> pktgen_t;
    std::vector<std::thread> workers;
    uint64_t worker_rate = rate_limit / num_threads;
    std::vector<double> thputs(num_threads, 0.0);
    struct rte_mempool* mempool = init_dpdk("pktbench", 0, 0);
    for (uint32_t i = 0; i < num_threads; i++) {
      workers.push_back(std::thread([i, worker_rate, &thputs, this] {
        uint64_t idx = i * kMaxPktsPerThread;
        struct rte_mbuf** pkts = data_.pkts_;
        packet_store::handle* handle = store_->get_handle();
        pktstore_vport* vport = new pktstore_vport(handle);
        rand_generator* gen = new rand_generator(mempool);
        pktgen_t pktgen(vport, gen, worker_rate, 0, kMaxPktsPerThread);

        fprintf(stderr, "Starting benchmark.\n");

        timestamp_t start = get_timestamp();
        pktgen.generate();
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
      std::thread cpu_measure_thread([&] {
        std::ofstream util_stream("cpu_utilization");
        cpu_utilization util;
        while (true) {
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
      cpu_measure_thread.detach();
    }

    for (auto& th : workers)
      th.join();

    double tot = 0.0;
    for (double thput : thputs)
      tot += thput;

    std::ofstream ofs("write_throughput", std::ios_base::app);
    ofs << num_threads << "\t" << tot << "\n";
    ofs.close();
  }

 private:
  packet_store *store_;
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
