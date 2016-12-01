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

#include "dpdk_utils.h"
#include "query_planner.h"
#include "query_parser.h"
#include "token_bucket.h"
#include "packetstore.h"
#include "bench_vport.h"
#include "pktgen.h"
#include "netplay_utils.h"

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

  static const uint64_t kWarmupCount = 1000;
  static const uint64_t kMeasureCount = 100000;
  static const uint64_t kCooldownCount = 1000;

  static const uint64_t kWarmupTime = 5000000;
  static const uint64_t kMeasureTime = 10000000;
  static const uint64_t kCooldownTime = 5000000;

  static const uint64_t kThreadQueryCount = 75000;

  filter_benchmark(const uint64_t load_rate, uint64_t num_pkts,
                   const std::string& query_path) {

    store_ = new packet_store();
    fprintf(stderr, "Adding complex chars...\n");
    add_complex_chars(query_path);
    fprintf(stderr, "Loading data...\n");
    load_data(load_rate, num_pkts);
    fprintf(stderr, "Loading cast queries...\n");
    load_cast_queries(query_path);
    fprintf(stderr, "Initialization complete.\n");
  }

  // Latency benchmarks
  void bench_cast_latency() {
    std::ofstream out("query_latency_cast.txt");
    packet_store::handle* handle = store_->get_handle();

    for (size_t i = 0; i < cast_queries_.size(); i++) {
      double avg = 0.0;
      size_t size = 0;
      for (size_t repeat = 0; repeat < 100; repeat++) {
        std::unordered_set<uint64_t> results;
        timestamp_t start = get_timestamp();
        handle->filter_pkts(results, cast_queries_[i]);
        timestamp_t end = get_timestamp();
        avg += (end - start);
        size += results.size();
      }
      avg /= 100;
      size /= 100;
      out << (i + 1) << "\t" << size << "\t" << avg << "\n";
      fprintf(stderr, "Query %zu: Count = %zu, Latency = %lf\n", (i + 1),
              size, avg);
    }
    out.close();

    delete handle;
  }

  void bench_char_latency() {
    std::ofstream out("query_latency_char.txt");
    packet_store::handle* handle = store_->get_handle();

    for (size_t i = 0; i < cast_queries_.size(); i++) {
      double avg = 0.0;
      size_t size = 0;
      for (size_t repeat = 0; repeat < 100; repeat++) {
        std::unordered_set<uint64_t> results;
        timestamp_t start = get_timestamp();
        handle->complex_character_lookup(results, char_ids_[i], end_time_ - 4, end_time_);
        timestamp_t end = get_timestamp();
        avg += (end - start);
        size += results.size();
      }
      avg /= 100;
      size /= 100;
      out << (i + 1) << "\t" << size << "\t" << avg << "\n";
      fprintf(stderr, "Query %zu: Count = %zu, Latency = %lf\n", (i + 1),
              size, avg);
    }
    out.close();

    delete handle;
  }

  // Throughput benchmarks
  void bench_cast_throughput(uint64_t query_rate, int num_threads) {
    assert(query_rate < 1e6);
    assert(num_threads >= 1);
    // TODO: Implement
  }

  void bench_char_throughput(uint64_t query_rate, int num_threads) {
    assert(query_rate < 1e6);
    assert(num_threads >= 1);
    // TODO: Implement
  }

 private:
  void load_data(uint64_t load_rate, uint64_t num_pkts) {
    struct rte_mempool* mempool = init_dpdk("filter", 0, 0);
    packet_store::handle* handle = store_->get_handle();
    pktstore_vport* vport = new pktstore_vport(handle);
    rand_generator* gen = new rand_generator(mempool);
    packet_generator<pktstore_vport> pktgen(vport, gen, load_rate, 0, num_pkts);
    pktgen.generate();
    end_time_ = std::time(NULL);
    fprintf(stderr, "Loaded %zu packets.\n", handle->num_pkts());
    delete handle;
  }

  void load_cast_queries(const std::string& query_path) {
    std::ifstream in(query_path);
    if (!in.is_open()) {
      fprintf(stderr, "Could not open query file %s\n", query_path.c_str());
      exit(-1);
    }
    packet_store::handle* handle = store_->get_handle();
    std::string exp;
    while (std::getline(in, exp)) {
      parser p(exp);
      expression *e = p.parse();
      query_plan qp = query_planner::plan(handle, e);
      cast_queries_.push_back(qp);
      free_expression(e);
    }
    fprintf(stderr, "Loaded %zu cast queries.\n", cast_queries_.size());

    delete handle;
  }

  void add_complex_chars(const std::string& query_path) {
    std::ifstream in(query_path);
    if (!in.is_open()) {
      fprintf(stderr, "Could not open query file %s\n", query_path.c_str());
      exit(-1);
    }
    packet_store::handle* handle = store_->get_handle();
    std::string exp;
    while (std::getline(in, exp)) {
      parser p(exp);
      expression *e = p.parse();
      fprintf(stderr, "Parsed expression\n");
      filter_list list = netplay_utils::build_filter_list(handle, e);
      fprintf(stderr, "Created filter list\n");
      uint32_t id = store_->add_complex_character(list);
      fprintf(stderr, "Added complex char\n");
      char_ids_.push_back(id);
      free_expression(e);
    }
    fprintf(stderr, "Added %zu chars.\n", char_ids_.size());

    delete handle;
  }

  static timestamp_t get_timestamp() {
    struct timeval now;
    gettimeofday(&now, NULL);

    return now.tv_usec + (timestamp_t) now.tv_sec * 1000000;
  }

  uint32_t end_time_;
  std::vector<query_plan> cast_queries_;
  std::vector<uint32_t> char_ids_;
  packet_store *store_;
};

const char* usage =
  "Usage: %s [-b bench-type] [-q query-rate] [-l load-rate] [-p num-packets] [-n num-threads] query-path\n";

void print_usage(char *exec) {
  fprintf(stderr, usage, exec);
}

int main(int argc, char** argv) {
  int c;
  std::string bench_type = "latency-get";
  uint64_t num_pkts = 60 * 1e6;
  uint64_t load_rate = 1e6;
  uint64_t query_rate = 0;
  int num_threads = 1;
  while ((c = getopt(argc, argv, "b:p:q:l:n:")) != -1) {
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
    default:
      fprintf(stderr, "Could not parse command line arguments.\n");
    }
  }

  if (optind == argc) {
    print_usage(argv[0]);
    return -1;
  }

  std::string query_path = std::string(argv[optind]);

  filter_benchmark ls_bench(load_rate, num_pkts, query_path);
  if (bench_type.find("latency-cast") == 0) {
    ls_bench.bench_cast_latency();
  } else if (bench_type == "throughput-cast") {
    ls_bench.bench_cast_throughput(query_rate, num_threads);
  } else if (bench_type.find("latency-char") == 0) {
    ls_bench.bench_char_latency();
  } else if (bench_type == "throughput-char") {
    ls_bench.bench_char_throughput(query_rate, num_threads);
  } else {
    fprintf(stderr, "Unknown benchmark type: %s; must be one of: "
            "{latency, throughput}\n", bench_type.c_str());
  }

  return 0;
}