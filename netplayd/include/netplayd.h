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

#define MAX_WRITERS  64
#define MAX_READERS  64

#define SLEEP_INTERVAL   10000000

#define CORE_MASK(i)    (1L << (i))
#define CORE_SET(m, i)  (m & CORE_MASK(i))

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
  netplay_daemon(const char* iface, struct rte_mempool* mempool,
                 uint64_t writer_core_mask, int query_server_port) {

    writer_core_mask_ = writer_core_mask;

    query_server_port_ = query_server_port;

    mempool_ = mempool;
    pkt_store_ = new packet_store();
    vport_ = new dpdk::virtual_port<vport_init>(iface, mempool);
  }

  void start() {
    typedef netplay_writer<vport_init> writer_t;
    for (uint64_t i = 0; i < MAX_WRITERS; i++) {
      if (CORE_SET(writer_core_mask_, i)) {
        fprintf(stderr, "Starting writer on core %" PRIu64 "\n", i);
        pthread_t writer_thread_id;
        packet_store::handle* handle = pkt_store_->get_handle();
        writer_t* writer = new writer_t(i, vport_, handle);
        pthread_create(&writer_thread_id, NULL, &writer_thread<vport_init>,
                       (void*) writer);
        pthread_detach(writer_thread_id);
      }
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
      fprintf(stderr, "[%" PRIu64 "] Packet rate: %lf pkts/s (since last epoch), "
              "%lf pkts/s (since start)\n", (now - start), epoch_rate, tot_rate);
      epoch = now;
      epoch_pkts = pkts;
    }
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

  uint64_t writer_core_mask_;

  int query_server_port_;

  struct rte_mempool* mempool_;

  packet_store *pkt_store_;
  dpdk::virtual_port<vport_init> *vport_;
};

}

#endif  // NETPLAYD_H_