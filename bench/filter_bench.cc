#include <chrono>
#include <ctime>
#include <sys/time.h>
#include <random>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <thread>
#include <vector>
#include <condition_variable>
#include <unistd.h>
#include <sstream>
#include <algorithm>
#include <unistd.h>

#include "token_bucket.h"
#include "packetstore.h"
#include "query_utils.h"

#define PKT_LEN 54
#define PKT_BURST 32
#define QUERY_BURST 1

using namespace ::netplay;
using namespace ::netplay::pktgen;
using namespace ::slog;
using namespace ::std::chrono;

uint32_t query_utils::now = std::time(NULL);

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

    fprintf(stderr, "Loading data...\n");
    load_data(load_rate, num_pkts);
    load_queries(query_path);
    fprintf(stderr, "Initialization complete.\n");
  }

  void load_data(uint64_t load_rate, uint64_t num_pkts) {
    packet_store::handle* handle = store_->get_handle();
    size_t i = 0;

    unsigned char data[PKT_LEN];
    token_list tokens;
    handle->add_src_ip(tokens, 0);
    handle->add_dst_ip(tokens, 0);
    handle->add_src_port(tokens, 0);
    handle->add_dst_port(tokens, 0);
    handle->add_timestamp(tokens, 0);

    token_bucket bucket(load_rate, PKT_BURST);
    while (i < num_pkts) {
      tokens[0].update_data(rand() % 256);
      tokens[1].update_data(rand() % 256);
      tokens[2].update_data(rand() % 10);
      tokens[3].update_data(rand() % 10);
      tokens[4].update_data(std::time(NULL));
      if (bucket.consume(1)) {
        handle->insert(data, PKT_LEN, tokens);
        i++;
      }
    }
    fprintf(stderr, "Loaded %zu packets.\n", i);

    delete handle;
  }

  void load_queries(const std::string& query_path) {
    std::ifstream in(query_path);
    if (!in) {
      fprintf(stderr, "Could not open query file %s\n", query_path.c_str());
      exit(-1);
    }
    packet_store::handle* handle = store_->get_handle();
    std::string exp;
    while (std::getline(in, exp))
      queries_.push_back(query_utils::expression_to_filter_query(handle, exp));
    fprintf(stderr, "Loaded %zu queries.\n", queries_.size());

    delete handle;
  }

  // Latency benchmarks
  void bench_latency() {
    std::ofstream out("query_latency.txt");
    packet_store::handle* handle = store_->get_handle();

    for (size_t i = 0; i < queries_.size(); i++) {
      std::unordered_set<uint64_t> results;
      timestamp_t start = get_timestamp();
      handle->filter(results, queries_[i]);
      timestamp_t end = get_timestamp();
      out << i << "\t" << results.size() << "\t" << (end - start) << "\n";
      fprintf(stderr, "Query %zu: Count = %zu, Latency = %llu\n", i,
              results.size(), (end - start));
    }
    out.close();
  }

  // Throughput benchmarks
  void bench_throughput(uint64_t query_rate, int num_threads) {
    assert(query_rate < 1e6);
    assert(num_threads >= 1);
    // TODO: Implement
  }

 private:
  static timestamp_t get_timestamp() {
    struct timeval now;
    gettimeofday(&now, NULL);

    return now.tv_usec + (timestamp_t) now.tv_sec * 1000000;
  }

  std::vector<filter_query> queries_;
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
  if (bench_type.find("latency") == 0) {
    ls_bench.bench_latency();
  } else if (bench_type == "throughput") {
    ls_bench.bench_throughput(query_rate, num_threads);
  } else {
    fprintf(stderr, "Unknown benchmark type: %s; must be one of: "
            "{latency, throughput}\n", bench_type.c_str());
  }

  return 0;
}