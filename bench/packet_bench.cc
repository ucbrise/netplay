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

#include "packetstore.h"
#include "cpu_utilization.h"

#define PKT_LEN 54

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

class rate_limiter {
 public:
  rate_limiter(uint64_t ops_per_sec, packet_store::handle* handle) {
    handle_ = handle;
    min_ns_per_10000_ops = 1e13 / ops_per_sec;
    local_ops_ = 0;
    last_ts_ = high_resolution_clock::now();
    tspec_.tv_sec = 0;
    fprintf(stderr, "10000 ops per %lld ns.\n", min_ns_per_10000_ops);
  }

  uint64_t insert_packet(unsigned char* data, uint16_t len, token_list& tkns) {
    uint64_t num_pkts = handle_->insert(data, len, tkns) + 1;
    local_ops_++;
    if (local_ops_ % 10000 == 0) {
      high_resolution_clock::time_point now = high_resolution_clock::now();
      auto ns_last_10000_ops =
        duration_cast<nanoseconds>(now - last_ts_).count();
      if (ns_last_10000_ops < min_ns_per_10000_ops) {
        tspec_.tv_nsec = (min_ns_per_10000_ops - ns_last_10000_ops);
        nanosleep(&tspec_, NULL);
      }
      last_ts_ = high_resolution_clock::now();
    }
    return num_pkts;
  }

  uint64_t local_ops() {
    return local_ops_;
  }

 private:
  struct timespec tspec_;
  high_resolution_clock::time_point last_ts_;
  uint64_t local_ops_;
  long long min_ns_per_10000_ops;
  packet_store::handle* handle_;
};

class rate_limiter_inf {
 public:
  rate_limiter_inf(uint64_t ops_per_sec, packet_store::handle* handle) {
    handle_ = handle;
    local_ops_ = 0;
    local_ops_++;
    assert(ops_per_sec == 0);
  }

  uint64_t insert_packet(unsigned char* data, uint16_t len, token_list& tkns) {
    local_ops_++;
    return handle_->insert(data, len, tkns) + 1;
  }

  uint64_t local_ops() {
    return local_ops_;
  }

 private:
  uint64_t local_ops_;
  packet_store::handle* handle_;
};

template<class rlimiter = rate_limiter_inf>
class packet_loader {
 public:
  static const uint64_t kReportRecordInterval = 11111;
  static const uint64_t kMaxNumPkts = 600 * 1e6;

  packet_loader() {
    store_ = new packet_store();

    fprintf(stderr, "Generating packets...\n");
    generate_pkts();
    fprintf(stderr, "Initialization complete.\n");
  }

  void init_tokens(token_list& tokens, packet_store::handle* handle) {
    handle->add_src_ip(tokens, 0);
    handle->add_dst_ip(tokens, 0);
    handle->add_src_port(tokens, 0);
    handle->add_dst_port(tokens, 0);
    handle->add_timestamp(tokens, 0);
  }

  void set_tokens(token_list& tokens, uint64_t idx) {
    tokens[0].update_data(srcips_[idx]);
    tokens[1].update_data(dstips_[idx]);
    tokens[2].update_data(sports_[idx]);
    tokens[3].update_data(dports_[idx]);
    tokens[4].update_data(std::time(NULL));
  }

  void generate_pkts() {
    std::string attr_line;
    while (data_.size() < kMaxNumPkts) {
      srcips_.push_back(rand() % 256);
      dstips_.push_back(rand() % 256);
      sports_.push_back(rand() % 10);
      dports_.push_back(rand() % 10);
    }
    fprintf(stderr, "Generated %zu packets.\n", timestamps_.size());
  }

  // Throughput benchmarks
  void load_packets(const uint32_t num_threads, const uint64_t rate_limit) {
    std::vector<std::thread> workers;
    uint64_t thread_ops = timestamps_.size() / num_threads;
    uint64_t worker_rate = rate_limit / num_threads;
    for (uint32_t i = 0; i < num_threads; i++) {
      workers.push_back(std::thread([i, worker_rate, thread_ops, this] {
        uint64_t idx = thread_ops * i;
        unsigned char data[PKT_LEN] = {};
        packet_store::handle* handle = store_->get_handle();
        token_list tokens;
        init_tokens(tokens, handle);
        rlimiter* limiter = new rlimiter(worker_rate, handle);
        double throughput = 0;
        fprintf(stderr, "Starting benchmark.\n");
        try {
          timestamp_t start = get_timestamp();
          while (limiter->local_ops() < thread_ops) {
            set_tokens(tokens, idx);
            limiter->insert_packet(data, PKT_LEN, tokens);
            idx++;
          }
          timestamp_t end = get_timestamp();
          double totsecs = (double) (end - start) / (1000.0 * 1000.0);
          throughput = ((double) limiter->local_ops() / totsecs);
          fprintf(stderr, "Thread #%u(%lfs): Throughput: %lf.\n", i, totsecs, throughput);
        } catch (std::exception &e) {
          fprintf(stderr, "Throughput thread ended prematurely: %s\n", e.what());
        }
        std::ofstream ofs("write_throughput_" + std::to_string(i));
        ofs << throughput << "\n";
        ofs.close();
        delete limiter;
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

#ifdef MEASURE_CPU
    std::thread cpu_measure_thread([&] {
      timestamp_t start = get_timestamp();
      std::ofstream util_stream("cpu_utilization");
      cpu_utilization util;
      while (get_timestamp() - start < timebound) {
        sleep(1);
        util_stream << util.current() << "\n";
      }
      util_stream.close();
    });
#endif

    for (auto& th : workers) {
      th.join();
    }

#ifdef MEASURE_CPU
    cpu_measure_thread.join();
#endif
  }

 private:
  std::vector<uint32_t> srcips_;
  std::vector<uint32_t> dstips_;
  std::vector<uint16_t> sports_;
  std::vector<uint16_t> dports_;

  packet_store *store_;
};

void print_usage(char *exec) {
  fprintf(stderr, usage, exec);
}

int main(int argc, char** argv) {
  if (argc < 3 || argc > 9) {
    print_usage(argv[0]);
    return -1;
  }

  int c;
  int num_threads = 1;
  uint64_t rate_limit = 0;
  while ((c = getopt(argc, argv, "n:r:")) != -1) {
    switch (c) {
    case 'n':
      num_threads = atoi(optarg);
      break;
    case 'r':
      rate_limit = atoll(optarg);
      break;
    default:
      fprintf(stderr, "Could not parse command line arguments.\n");
    }
  }

  if (rate_limit == 0) {
    packet_loader<> loader;
    loader.load_packets(num_threads, 0);
  } else {
    packet_loader<rate_limiter> loader;
    loader.load_packets(num_threads, rate_limit);
  }

  return 0;

}
