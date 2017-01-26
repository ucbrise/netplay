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
#include "cpu_utilization.h"
#include "rate_limiter.h"

using namespace ::netplay::dpdk;
using namespace ::netplay;
using namespace ::slog;
using namespace ::std::chrono;

const char* usage = "Usage: %s [-r rate-limit] [-c]\n";

typedef uint64_t timestamp_t;

static timestamp_t get_timestamp() {
  struct timeval now;
  gettimeofday(&now, NULL);

  return now.tv_usec + (timestamp_t) now.tv_sec * 1000000;
}

#define PACKET_SIZE             90
#define RTE_BURST_SIZE          32
#define RETR_THRESHOLD          100

class array_generator {
 public:
  array_generator(struct rte_mempool* mempool, unsigned char* pkt_data) {
    cur_pos_ = 0;
    pkt_data_ = pkt_data;

    int ret = netplay::dpdk::mempool::mbuf_alloc_bulk(pkts_, PACKET_SIZE,
              RTE_BURST_SIZE, mempool);
    if (ret != 0) {
      fprintf(stderr, "Error allocating packets %d\n", ret);
      exit(-1);
    }
  }

  struct rte_mbuf** generate_batch(size_t size) {
    // Get next batch
    for (size_t i = 0; i < size; i++) {
      unsigned char* pkt = rte_pktmbuf_mtod(pkts_[i], unsigned char*);
      size_t off = cur_pos_ + i;
      memcpy(pkt, pkt_data_ + off * PACKET_SIZE, PACKET_SIZE);
    }
    cur_pos_ += size;
    return pkts_;
  }

 private:
  uint64_t cur_pos_;
  unsigned char* pkt_data_;
  struct rte_mbuf* pkts_[RTE_BURST_SIZE];
};

class priority {
 public:
  priority(const std::string& trace_file) {
    store_ = new packet_store();
    FILE *f = fopen(trace_file.c_str(), "rb");
    fseek(f, 0, SEEK_END);
    size_ = ftell(f);
    fseek(f, 0, SEEK_SET);

    pkt_data_ = (unsigned char *) malloc(size_);
    size_t item_count = fread(pkt_data_, size_, 1, f);
    fclose(f);
    pkt_count_ = size_ / PACKET_SIZE;

    fprintf(stderr, "Loaded %zu packets; Item count = %zu.\n", pkt_count_, item_count);
  }

  void run_priority(const uint64_t rate_limit, const bool measure_cpu) {

    typedef rate_limiter<pktstore_vport, array_generator> pktgen_type;
    std::vector<std::thread> workers;

    std::atomic<bool> done;
    done.store(false);
    double thput;
    struct rte_mempool* mempool = init_dpdk("pktbench", 0, 0);

    {
      fprintf(stderr, "Starting priority thread.\n");
      workers.push_back(std::thread([rate_limit, &thput, &done, &mempool, this] {
        packet_store::handle* handle = store_->get_handle();
        pktstore_vport* vport = new pktstore_vport(handle);
        array_generator* gen = new array_generator(mempool, pkt_data_);
        pktgen_type pktgen(vport, gen, rate_limit, pkt_count_);

        
        timestamp_t start = get_timestamp();
        pktgen.generate();
        done.store(true);
        timestamp_t end = get_timestamp();
        double totsecs = (double) (end - start) / (1000.0 * 1000.0);
        thput = ((double) pktgen.total_sent() / totsecs);

        delete vport;
        delete gen;
        delete handle;
      }));

      // Create a cpu_set_t object representing a set of CPUs. Clear it and mark
      // only CPU 0 as set.
      cpu_set_t cpuset;
      CPU_ZERO(&cpuset);
      CPU_SET(0, &cpuset);
      int rc = pthread_setaffinity_np(workers.back().native_handle(),
                                      sizeof(cpu_set_t), &cpuset);
      if (rc != 0)
        fprintf(stderr, "Error calling pthread_setaffinity_np: %d\n", rc);
    }

    {
      fprintf(stderr, "Starting monitor thread.\n");
      workers.push_back(std::thread([&done, this] {
        packet_store::handle* handle = store_->get_handle();
        struct timespec tspec;
        tspec.tv_sec = 0;
        tspec.tv_nsec = 1e8;

        std::unordered_map<uint32_t, std::pair<size_t, size_t>> pkt_dist;
        std::vector<size_t> off1(15, 0);
        std::vector<size_t> off2(15, 0);

        typedef std::unordered_map<uint32_t, std::pair<size_t, size_t>>::iterator iter;

        // sleep(5);
        bool enable = false;
        size_t prev_retr = 0;
        while (!done.load()) {
          nanosleep(&tspec, NULL);
          if (enable) {
            timestamp_t t0 = get_timestamp();
            handle->diagnose_priority(off1, off2, pkt_dist);
            timestamp_t t1 = get_timestamp();
            timestamp_t tdiff = t1 - t0;

            fprintf(stderr, "Time taken = %lu us\n", tdiff);
            fprintf(stderr, "Diagnosis:\n");
            fprintf(stderr, "Source IP: Retransmissions, Recvd. Packets:\n");
            for (iter s = pkt_dist.begin(); s != pkt_dist.end(); s++) {
              print_ip(s->first);
              fprintf(stderr, ": %zu, %zu\n", s->second.first, s->second.second);
            }
          }

          size_t retr = handle->get_retransmissions();
          if (!enable && retr - prev_retr > RETR_THRESHOLD) {
            handle->init_pkt_offs(off1);
            handle->init_retr_offs(off2);

            enable = true;
            fprintf(stderr, "Number of retransmissions = %zu\n", retr - prev_retr);
          }
          prev_retr = retr;
        }
        delete handle;
      }));

      // Create a cpu_set_t object representing a set of CPUs. Clear it and mark
      // only CPU 0 as set.
      cpu_set_t cpuset;
      CPU_ZERO(&cpuset);
      CPU_SET(1, &cpuset);
      int rc = pthread_setaffinity_np(workers.back().native_handle(),
                                      sizeof(cpu_set_t), &cpuset);
      if (rc != 0)
        fprintf(stderr, "Error calling pthread_setaffinity_np: %d\n", rc);
    }

    if (measure_cpu) {
      fprintf(stderr, "Starting CPU measure thread.\n");
      std::thread cpu_measure_thread([rate_limit, &done, this] {
        std::ofstream util_stream("priority_util_" + std::to_string(rate_limit) + ".txt");
        cpu_utilization util;
        while (!done.load()) {
          sleep(1);
          util_stream << util.current() << "\n";
          util_stream.flush();
        }
        util_stream.close();
      });

      // Create a cpu_set_t object representing a set of CPUs. Clear it and mark
      // only CPU i as set.
      cpu_set_t cpuset;
      CPU_ZERO(&cpuset);
      CPU_SET(2, &cpuset);
      int rc = pthread_setaffinity_np(cpu_measure_thread.native_handle(),
                                      sizeof(cpu_set_t), &cpuset);
      if (rc != 0)
        fprintf(stderr, "Error calling pthread_setaffinity_np: %d\n", rc);

      cpu_measure_thread.join();
    }

    for (auto& th : workers)
      th.join();

    std::ofstream ofs("priority_thput_" + std::to_string(rate_limit) + ".txt");
    ofs << thput << "\n";
    ofs.close();

    fprintf(stderr, "Packet Capture Throughput: %lf.\n", thput);
    fprintf(stderr, "Completed priority experiment.\n");
  }

 private:
  inline uint64_t curusec() {
    using namespace ::std::chrono;
    auto ts = steady_clock::now().time_since_epoch();
    return duration_cast<std::chrono::microseconds>(ts).count();
  }

  void print_ip(uint32_t ip) {
    unsigned char bytes[4];
    bytes[0] = ip & 0xFF;
    bytes[1] = (ip >> 8) & 0xFF;
    bytes[2] = (ip >> 16) & 0xFF;
    bytes[3] = (ip >> 24) & 0xFF;
    fprintf(stderr, "%u.%u.%u.%u", bytes[0], bytes[1], bytes[2], bytes[3]);
  }

  void print_bytes(unsigned char* bytes) {
    fprintf(stderr, "Bytes: ");
    for (size_t i = 0; i < PACKET_SIZE; i++) {
      fprintf(stderr, "%u ", bytes[i]);
    }
    fprintf(stderr, "\n");
  }

  packet_store *store_;
  unsigned char* pkt_data_;
  size_t pkt_count_;
  size_t size_;
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
  uint64_t rate_limit = 0;
  bool measure_cpu = false;
  std::string trace_file;
  while ((c = getopt(argc, argv, "t:r:c")) != -1) {
    switch (c) {
    case 't':
      trace_file = std::string(optarg);
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

  priority expt(trace_file);
  expt.run_priority(rate_limit, measure_cpu);

  return 0;
}
