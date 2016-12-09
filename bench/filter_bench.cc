#include <unistd.h>
#include <sys/time.h>

#include <chrono>
#include <ctime>
#include <random>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <thread>
#include <vector>
#include <sstream>
#include <algorithm>
#include <iostream>

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

#include "cpu_utilization.h"
#include "critical_error_handler.h"
#include "dpdk_utils.h"
#include "query_planner.h"
#include "query_parser.h"
#include "token_bucket.h"
#include "packetstore.h"
#include "bench_vport.h"
#include "pktgen.h"
#include "netplay_utils.h"
#include "rate_limiter.h"
#include "cast_builder.h"
#include "character_builder.h"
#include "packet_attributes.h"
#include "aggregates.h"

#define PKT_LEN 54
#define PKT_BURST 32
#define QUERY_BURST 1

using namespace ::netplay;
using namespace ::netplay::dpdk;
using namespace ::netplay::pktgen;
using namespace ::slog;
using namespace ::std::chrono;

class filter_benchmark {
 public:
  typedef unsigned long long int timestamp_t;
  typedef aggregate::count<attribute::packet_header> packet_counter;

  static const uint64_t CHAR_COUNT = 100000;
  static const uint64_t CAST_COUNT = 100;

  filter_benchmark(const uint64_t load_rate, const std::string& query_path)
    : query_path_(query_path) {

    load_rate_ = load_rate;
    store_ = new packet_store();
    mempool_ = init_dpdk("filter", 0, 0);
    output_suffix_ = ".txt";

    load_filters();
    add_complex_chars();
  }

  void load_data(uint64_t num_pkts) {
    fprintf(stderr, "Loading packets...\n");
    packet_store::handle* handle = store_->get_handle();
    pktstore_vport* vport = new pktstore_vport(handle);
    rand_generator* gen = new rand_generator(mempool_);
    packet_generator<pktstore_vport> pktgen(vport, gen, load_rate_, 0, num_pkts);
    start_time_ = std::time(NULL);
    pktgen.generate();
    end_time_ = std::time(NULL);
    uint32_t totsecs = end_time_ - start_time_;
    double pkt_rate = (double) pktgen.total_sent() / (double) totsecs;
    fprintf(stderr, "Loaded %zu packets in %" PRIu32 "s (%lf packets/s).\n",
            pktgen.total_sent(), totsecs, pkt_rate);
    build_casts();
    delete handle;
    delete gen;
    delete vport;
    output_suffix_ = "_" + std::to_string(load_rate_) + "_" +
                     std::to_string(handle->num_pkts()) + ".txt";
  }

  // Latency benchmarks
  void bench_cast_latency(size_t repeat_max = CAST_COUNT) {
    std::ofstream out("latency_cast" + output_suffix_);
    for (size_t i = 0; i < casts_.size(); i++) {
      double avg = 0.0;
      size_t size = 0;
      for (size_t repeat = 0; repeat < repeat_max; repeat++) {
        timestamp_t start = get_timestamp();
        size_t cnt = casts_[i].execute<packet_counter>();
        timestamp_t end = get_timestamp();
        avg += (end - start);
        size += cnt;
      }
      avg /= repeat_max;
      size /= repeat_max;
      out << (i + 1) << "\t" << size << "\t" << avg << "\n";
      fprintf(stderr, "q%zu: Count=%zu, Latency=%lf\n", (i + 1), size, avg);
    }
    out.close();
  }

  template<typename container_type>
  uint64_t count_container(container_type& container) {
    typedef typename container_type::iterator iterator_t;
    uint64_t count = 0;
    for (iterator_t it = container.begin(); it != container.end(); it++) {
      count++;
    }
    return count;
  }

  void bench_char_latency(size_t repeat_max = CHAR_COUNT) {
    std::ofstream out("latency_char" + output_suffix_);
    for (size_t i = 0; i < characters_.size(); i++) {
      double avg = 0.0;
      size_t size = 0;
      for (size_t repeat = 0; repeat < repeat_max; repeat++) {
        timestamp_t start = get_timestamp();
        size_t cnt = characters_[i].execute<packet_counter>(end_time_ - 4, end_time_);
        timestamp_t end = get_timestamp();
        avg += (end - start);
        size += cnt;
      }
      avg /= repeat_max;
      size /= repeat_max;
      out << (i + 1) << "\t" << size << "\t" << avg << "\n";
      fprintf(stderr, "q%zu: Count=%zu, Latency=%lf\n", (i + 1), size, avg);
    }
    out.close();
  }

  // Throughput benchmarks
  void bench_cast_throughput(uint64_t query_rate, uint32_t num_threads, bool measure_cpu) {
    uint64_t worker_rate = query_rate / num_threads;
    for (size_t qid = 0; qid < filters_.size(); qid++) {
      std::vector<std::thread> workers;
      std::vector<double> query_thputs(num_threads, 0.0);
      std::vector<double> pkt_thputs(num_threads, 0.0);
      for (uint32_t i = 0; i < num_threads; i++) {
        workers.push_back(std::thread([i, qid, worker_rate, &query_thputs, &pkt_thputs, this] {
          pacer<1> p(worker_rate);
          size_t num_pkts = 0;
          timestamp_t start = get_timestamp();
          for (size_t repeat = 0; repeat < CAST_COUNT; repeat++) {
            num_pkts += casts_[qid].execute<packet_counter>();
            p.pace();
          }
          timestamp_t end = get_timestamp();
          double totsecs = (double) (end - start) / (1000.0 * 1000.0);
          query_thputs[i] = ((double) CAST_COUNT / totsecs);
          pkt_thputs[i] = ((double) num_pkts / totsecs);
          fprintf(stderr, "Thread #%u(%lfs): Throughput: %lf.\n", i, totsecs, query_thputs[i]);
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
          std::ofstream util_stream("cast_cpu_utilization_" + std::to_string(qid) + "_" + std::to_string(num_threads) + output_suffix_);
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

      double qtot = 0.0;
      for (double thput : query_thputs)
        qtot += thput;

      double ptot = 0.0;
      for (double thput : pkt_thputs)
        ptot += thput;

      std::ofstream ofs("throughput_cast_" + std::to_string(num_threads) + output_suffix_, std::ios_base::app);
      ofs << (qid + 1) << "\t" << num_threads << "\t" << qtot << "\t" << ptot << "\n";
      ofs.close();
    }
  }

  void bench_char_throughput(uint64_t query_rate, uint32_t num_threads, bool measure_cpu) {
    uint64_t worker_rate = query_rate / num_threads;
    for (size_t qid = 0; qid < characters_.size(); qid++) {
      std::vector<std::thread> workers;
      std::vector<double> query_thputs(num_threads, 0.0);
      std::vector<double> pkt_thputs(num_threads, 0.0);
      for (uint32_t i = 0; i < num_threads; i++) {
        workers.push_back(std::thread([i, qid, worker_rate, &query_thputs, &pkt_thputs, this] {
          pacer<10> p(worker_rate);
          uint64_t num_pkts = 0;
          timestamp_t start = get_timestamp();
          for (size_t repeat = 0; repeat < CHAR_COUNT; repeat++) {
            num_pkts += characters_[qid].execute<packet_counter>(end_time_ - 4, end_time_);
            p.pace();
          }
          timestamp_t end = get_timestamp();
          double totsecs = (double) (end - start) / (1000.0 * 1000.0);
          query_thputs[i] = ((double) CHAR_COUNT / totsecs);
          pkt_thputs[i] = ((double) num_pkts / totsecs);
          fprintf(stderr, "Thread #%u(%lfs): Throughput: %lf.\n", i, totsecs, query_thputs[i]);
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
          std::ofstream util_stream("char_cpu_utilization_" + std::to_string(qid) + "_" + std::to_string(num_threads) + output_suffix_);
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

      double qtot = 0.0;
      for (double thput : query_thputs)
        qtot += thput;

      double ptot = 0.0;
      for (double thput : pkt_thputs)
        ptot += thput;

      std::ofstream ofs("throughput_char_" + std::to_string(num_threads) + output_suffix_, std::ios_base::app);
      ofs << (qid + 1) << "\t" << num_threads << "\t" << qtot << "\t" << ptot << "\n";
      ofs.close();
    }
  }

 private:
  void build_casts() {
    fprintf(stderr, "Building casts...\n");
    casts_.clear();
    for (std::string& exp : filters_) {
      auto c = cast_builder(store_, exp).build();
      casts_.push_back(c);
    }
  }

  void load_filters() {
    fprintf(stderr, "Loading filters...\n");
    std::ifstream in(query_path_);
    if (!in.is_open()) {
      fprintf(stderr, "Could not open query file %s\n", query_path_.c_str());
      exit(-1);
    }

    std::string exp;
    while (std::getline(in, exp)) {
      filters_.push_back(exp);
    }
    fprintf(stderr, "Loaded %zu filters.\n", filters_.size());
  }

  void add_complex_chars() {
    fprintf(stderr, "Adding complex characters...\n");
    for (std::string& exp : filters_) {
      auto c = character_builder(store_, exp).build();
      characters_.push_back(c);
    }
    fprintf(stderr, "Added %zu complex characters.\n", characters_.size());
  }

  static timestamp_t get_timestamp() {
    struct timeval now;
    gettimeofday(&now, NULL);

    return now.tv_usec + (timestamp_t) now.tv_sec * 1000000;
  }

  uint64_t load_rate_;

  uint32_t start_time_;
  uint32_t end_time_;

  const std::string query_path_;
  std::vector<std::string> filters_;
  std::vector<cast> casts_;
  std::vector<complex_character> characters_;

  std::string output_suffix_;

  struct rte_mempool* mempool_;
  packet_store *store_;
};

const char* usage =
  "Usage: %s [-b bench-type] [-q query-rate] [-l load-rate] [-p num-packets] [-n num-threads] [-c measure-cpu] query-path\n";

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
  std::string bench_type = "latency-get";
  uint64_t num_pkts = 60 * 1e6;
  uint64_t load_rate = 1e6;
  uint64_t query_rate = 0;
  uint32_t num_threads = 1;
  bool measure_cpu = false;
  while ((c = getopt(argc, argv, "b:p:q:l:n:c")) != -1) {
    switch (c) {
    case 'b':
      bench_type = std::string(optarg);
      break;
    case 'p':
      num_pkts = atoll(optarg);
      break;
    case 'q':
      query_rate = atoll(optarg);
      break;
    case 'l':
      load_rate = atoll(optarg);
      break;
    case 'n':
      num_threads = atoi(optarg);
      break;
    case 'c':
      measure_cpu = true;
      break;
    default:
      fprintf(stderr, "Could not parse command line arguments.\n");
    }
  }

  if (optind == argc) {
    print_usage(argv[0]);
    return -1;
  }

  std::string query_path = std::string(argv[optind]);
  filter_benchmark ls_bench(load_rate, query_path);
  if (bench_type == "latency-cast") {
    fprintf(stderr, "Latency cast benchmark\n");
    ls_bench.load_data(num_pkts);
    ls_bench.bench_cast_latency();
  } else if (bench_type == "latency-char") {
    fprintf(stderr, "Latency char benchmark\n");
    ls_bench.load_data(num_pkts);
    ls_bench.bench_char_latency();
  } else if (bench_type == "latency-trend") {
    fprintf(stderr, "Latency trend benchmark\n");
    const uint64_t packet_batch = 10000000;
    for (uint64_t p = packet_batch; p <= num_pkts; p += packet_batch) {
      ls_bench.load_data(packet_batch);
      ls_bench.bench_cast_latency();
      ls_bench.bench_char_latency();
    }
  } else if (bench_type == "latency") {
    fprintf(stderr, "Latency benchmark\n");
    ls_bench.load_data(num_pkts);
    ls_bench.bench_cast_latency();
    ls_bench.bench_char_latency();
  } else if (bench_type == "throughput-char") {
    fprintf(stderr, "Throughput char benchmark\n");
    ls_bench.load_data(num_pkts);
    ls_bench.bench_char_throughput(query_rate, num_threads, measure_cpu);
  } else if (bench_type == "throughput-cast") {
    fprintf(stderr, "Throughput cast benchmark\n");
    ls_bench.load_data(num_pkts);
    ls_bench.bench_cast_throughput(query_rate, num_threads, measure_cpu);
  } else if (bench_type == "throughput") {
    fprintf(stderr, "Throughput benchmark\n");
    ls_bench.load_data(num_pkts);
    ls_bench.bench_cast_throughput(query_rate, num_threads, measure_cpu);
    ls_bench.bench_char_throughput(query_rate, num_threads, measure_cpu);
  } else if (bench_type == "all") {
    fprintf(stderr, "All benchmark\n");
    ls_bench.load_data(num_pkts);
    ls_bench.bench_cast_latency();
    for (uint32_t i = 1; i < 8; i++) {
      ls_bench.bench_cast_throughput(query_rate, i, measure_cpu);
    }
    ls_bench.bench_char_latency();
    for (uint32_t i = 1; i < 8; i++) {
      ls_bench.bench_char_throughput(query_rate, i, measure_cpu);
    }
  } else {
    fprintf(stderr, "Unknown benchmark type: %s; must be one of: "
            "{latency, throughput}\n", bench_type.c_str());
  }

  return 0;
}