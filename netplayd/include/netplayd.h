#ifndef NETPLAYD_H_
#define NETPLAYD_H_

#include <stdint.h>
#include <pthread.h>
#include <unistd.h>

#include <chrono>
#include <thread>

#include <rte_mbuf.h>

#include "packetstore.h"
#include "virtual_port.h"
#include "netplay_writer.h"

namespace netplay {

#define SLEEP_INTERVAL        10000000
#define BENCH_SLEEP_INTERVAL  20000000

template<typename vport_init>
void* writer_thread(void* arg) {
  netplay_writer<vport_init>* writer = (netplay_writer<vport_init>*) arg;
  dpdk::init_thread(writer->core());
  writer->start();

  return NULL;
}

template<typename vport_init>
class netplay_daemon {
 public:
  typedef std::map<int, std::string> writer_map;
  netplay_daemon(const writer_map& mapping, struct rte_mempool* mempool,
                 int query_server_port): writer_interface_mapping_(mapping) {
    query_server_port_ = query_server_port;
    mempool_ = mempool;
    pkt_store_ = new packet_store();
  }

  void start() {
    typedef netplay_writer<vport_init> writer_t;
    for (auto& entry : writer_interface_mapping_) {
      printf("Starting writer on core %d polling interface %s...\n",
             entry.first, entry.second.c_str());
      pthread_t writer_thread_id;
      packet_store::handle* handle = pkt_store_->get_handle();
      dpdk::virtual_port<vport_init>* vport =
        new dpdk::virtual_port<vport_init>(entry.second.c_str(), mempool_);
      writer_t* writer = new writer_t(entry.first, vport, handle);
      pthread_create(&writer_thread_id, NULL, &writer_thread<vport_init>,
                     (void*) writer);
      pthread_detach(writer_thread_id);
    }
  }

  void monitor() {
    uint64_t start = curusec();
    uint64_t start_pkts = processed_pkts();
    uint64_t epoch = start;
    uint64_t epoch_pkts = start_pkts;
    while (1) {
      usleep(SLEEP_INTERVAL);
      uint64_t pkts = processed_pkts();
      uint64_t now = curusec();

      double epoch_rate = (double) (pkts - epoch_pkts) * 1000000.0 / (double) (now - epoch);
      double tot_rate = (double) (pkts - start_pkts) * 1000000.0 / (double) (now - start);
      printf("[%" PRIu64 "] Packet rate: %lf pkts/s (since last epoch), "
             "%lf pkts/s (since start)\n", (now - start), epoch_rate, tot_rate);
      epoch = now;
      epoch_pkts = pkts;
    }
  }

  void bench() {
    uint64_t start = curusec();
    uint64_t start_pkts = processed_pkts();

    usleep(BENCH_SLEEP_INTERVAL);
    uint64_t pkts = processed_pkts();
    uint64_t now = curusec();
    double tot_rate = (double) (pkts - start_pkts) * 1000000.0 / (double) (now - start);
    fprintf(stderr, "%zu\t%lf\n", writer_interface_mapping_.size(), tot_rate);
  }

 private:
  inline uint64_t curusec() {
    using namespace ::std::chrono;
    auto ts = steady_clock::now().time_since_epoch();
    return duration_cast<std::chrono::microseconds>(ts).count();
  }

  uint64_t processed_pkts() {
    return pkt_store_->num_pkts();
  }

  int query_server_port_;
  writer_map writer_interface_mapping_;
  struct rte_mempool* mempool_;
  packet_store *pkt_store_;
};

}

#endif  // NETPLAYD_H_