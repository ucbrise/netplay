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

#include <execinfo.h>
#include <signal.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>

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

typedef struct _sig_ucontext {
  unsigned long     uc_flags;
  struct ucontext   *uc_link;
  stack_t           uc_stack;
  struct sigcontext uc_mcontext;
  sigset_t          uc_sigmask;
} sig_ucontext_t;

class filter_benchmark {
 public:
  typedef unsigned long long int timestamp_t;

  static const uint64_t kThreadQueryCount = 1000;

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

  template<typename container_type>
  uint64_t count_container(container_type& container) {
    typedef typename container_type::iterator iterator_t;
    uint64_t count = 0;
    for (iterator_t it = container.begin(); it != container.end(); it++) {
      count++;
    }
    return count;
  }

  void bench_char_latency() {
    std::ofstream out("query_latency_char.txt");
    packet_store::handle* handle = store_->get_handle();

    for (size_t i = 0; i < cast_queries_.size(); i++) {
      double avg = 0.0;
      size_t size = 0;
      for (size_t repeat = 0; repeat < 100; repeat++) {
        timestamp_t start = get_timestamp();
        auto res = handle->complex_character_lookup(char_ids_[i], end_time_ - 4, end_time_);
        size += count_container(res);
        timestamp_t end = get_timestamp();
        avg += (end - start);
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
  void bench_cast_throughput(uint64_t query_rate, uint32_t num_threads, bool measure_cpu) {
    uint64_t worker_rate = query_rate / num_threads;
    for (size_t qid = 0; qid < cast_queries_.size(); qid++) {
      std::vector<std::thread> workers;
      std::vector<double> query_thputs(num_threads, 0.0);
      std::vector<double> pkt_thputs(num_threads, 0.0);
      for (uint32_t i = 0; i < num_threads; i++) {
        workers.push_back(std::thread([i, qid, worker_rate, &query_thputs, &pkt_thputs, this] {
          packet_store::handle* handle = store_->get_handle();
          token_bucket bucket(worker_rate, 1);
          uint64_t num_pkts = 0;
          timestamp_t start = get_timestamp();
          for (size_t repeat = 0; repeat < kThreadQueryCount; repeat++) {
            if (worker_rate == 0 || bucket.consume(1)) {
              std::unordered_set<uint64_t> results;
              handle->filter_pkts(results, cast_queries_[qid]);
              num_pkts += results.size();
            } else {
              repeat--;
            }
          }
          timestamp_t end = get_timestamp();
          double totsecs = (double) (end - start) / (1000.0 * 1000.0);
          query_thputs[i] = ((double) kThreadQueryCount / totsecs);
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
          std::ofstream util_stream("cast_cpu_utilization_" + std::to_string(qid) + "_" + std::to_string(num_threads) + ".txt");
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

      std::ofstream ofs("query_throughput_cast_" + std::to_string(num_threads) + ".txt", std::ios_base::app);
      ofs << (qid + 1) << "\t" << num_threads << "\t" << qtot << "\t" << ptot << "\n";
      ofs.close();
    }
  }

  void bench_char_throughput(uint64_t query_rate, uint32_t num_threads, bool measure_cpu) {
    uint64_t worker_rate = query_rate / num_threads;
    for (size_t qid = 0; qid < cast_queries_.size(); qid++) {
      std::vector<std::thread> workers;
      std::vector<double> query_thputs(num_threads, 0.0);
      std::vector<double> pkt_thputs(num_threads, 0.0);
      for (uint32_t i = 0; i < num_threads; i++) {
        workers.push_back(std::thread([i, qid, worker_rate, &query_thputs, &pkt_thputs, this] {
          packet_store::handle* handle = store_->get_handle();
          token_bucket bucket(worker_rate, 1);
          uint64_t num_pkts = 0;
          timestamp_t start = get_timestamp();
          for (size_t repeat = 0; repeat < kThreadQueryCount; repeat++) {
            if (worker_rate == 0 || bucket.consume(1)) {
              auto res = handle->complex_character_lookup(char_ids_[qid], end_time_ - 4, end_time_);
              num_pkts += count_container(res);
            } else {
              repeat--;
            }
          }
          timestamp_t end = get_timestamp();
          double totsecs = (double) (end - start) / (1000.0 * 1000.0);
          query_thputs[i] = ((double) kThreadQueryCount / totsecs);
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
          std::ofstream util_stream("char_cpu_util_" + std::to_string(qid) + "_" + std::to_string(num_threads) + ".txt");
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

      std::ofstream ofs("query_throughput_char_" + std::to_string(num_threads) + ".txt", std::ios_base::app);
      ofs << (qid + 1) << "\t" << num_threads << "\t" << qtot << "\t" << ptot << "\n";
      ofs.close();
    }
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
      filter_list list = netplay_utils::build_filter_list(handle, e);
      uint32_t id = store_->add_complex_character(list);
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

void crit_err_hdlr(int sig_num, siginfo_t * info, void * ucontext) {
  sig_ucontext_t * uc = (sig_ucontext_t *)ucontext;

  /* Get the address at the time the signal was raised */
#if defined(__i386__) // gcc specific
  void *caller_address = (void *) uc->uc_mcontext.eip; // EIP: x86 specific
#elif defined(__x86_64__) // gcc specific
  void *caller_address = (void *) uc->uc_mcontext.rip; // RIP: x86_64 specific
#else
#error Unsupported architecture. // TODO: Add support for other arch.
#endif

  std::cerr << "signal " << sig_num
            << " (" << strsignal(sig_num) << "), address is "
            << info->si_addr << " from " << caller_address
            << std::endl << std::endl;

  void * array[50];
  int size = backtrace(array, 50);

  array[1] = caller_address;

  char ** messages = backtrace_symbols(array, size);

  // skip first stack frame (points here)
  for (int i = 1; i < size && messages != NULL; ++i) {
    char *mangled_name = 0, *offset_begin = 0, *offset_end = 0;

    // find parantheses and +address offset surrounding mangled name
    for (char *p = messages[i]; *p; ++p) {
      if (*p == '(') {
        mangled_name = p;
      } else if (*p == '+') {
        offset_begin = p;
      } else if (*p == ')') {
        offset_end = p;
        break;
      }
    }

    // if the line could be processed, attempt to demangle the symbol
    if (mangled_name && offset_begin && offset_end &&
        mangled_name < offset_begin) {
      *mangled_name++ = '\0';
      *offset_begin++ = '\0';
      *offset_end++ = '\0';

      int status;
      char * real_name = abi::__cxa_demangle(mangled_name, 0, 0, &status);

      // if demangling is successful, output the demangled function name
      if (status == 0) {
        std::cerr << "[bt]: (" << i << ") " << messages[i] << " : "
                  << real_name << "+" << offset_begin << offset_end
                  << std::endl;

      }
      // otherwise, output the mangled function name
      else {
        std::cerr << "[bt]: (" << i << ") " << messages[i] << " : "
                  << mangled_name << "+" << offset_begin << offset_end
                  << std::endl;
      }
      free(real_name);
    }
    // otherwise, print the whole line
    else {
      std::cerr << "[bt]: (" << i << ") " << messages[i] << std::endl;
    }
  }
  std::cerr << std::endl;

  free(messages);

  exit(EXIT_FAILURE);
}

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

  filter_benchmark ls_bench(load_rate, num_pkts, query_path);
  if (bench_type.find("latency-cast") == 0) {
    ls_bench.bench_cast_latency();
  } else if (bench_type == "throughput-cast") {
    ls_bench.bench_cast_throughput(query_rate, num_threads, measure_cpu);
  } else if (bench_type.find("latency-char") == 0) {
    ls_bench.bench_char_latency();
  } else if (bench_type == "throughput-char") {
    ls_bench.bench_char_throughput(query_rate, num_threads, measure_cpu);
  } else if (bench_type == "all") {
    ls_bench.bench_cast_latency();
    for (uint32_t i = 1; i < 12; i++) {
      ls_bench.bench_cast_throughput(query_rate, i, measure_cpu);
    }
    ls_bench.bench_char_latency();
    for (uint32_t i = 1; i < 12; i++) {
      ls_bench.bench_char_throughput(query_rate, i, measure_cpu);
    }
  } else {
    fprintf(stderr, "Unknown benchmark type: %s; must be one of: "
            "{latency, throughput}\n", bench_type.c_str());
  }

  return 0;
}