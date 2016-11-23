#ifndef NETPLAYD_H_
#define NETPLAYD_H_

#include <stdint.h>
#include <pthread.h>

#include <rte_mbuf.h>
#include "packetstore.h"
#include "virtual_port.h"
#include "netplay_writer.h"
#include "netplay_reader.h"
#include "pktgen.h"

namespace netplay {

#define MAX_WRITERS  64
#define MAX_READERS  64

#define CORE_MASK(i)    (1L << (i))
#define CORE_SET(m, i)  (m & CORE_MASK(i))

template<typename vport_init>
void* writer_thread(void* arg) {
  netplay_writer<vport_init>* writer = (netplay_writer<vport_init>*) arg;
  dpdk::init_thread(writer->core());
  writer->start();

  return NULL;
}

void* reader_thread(void* arg) {
  netplay_reader* reader = (netplay_reader*) arg;
  dpdk::init_thread(reader->core());
  reader->start();

  return NULL;
}

template<typename vport_init>
class netplay_daemon {
 public:
  netplay_daemon(const char* iface, struct rte_mempool* mempool, uint32_t writer_core_mask,
                 uint32_t reader_core_mask, int master_core, int pktgen, uint64_t pktgen_rate_limit) {

    master_core_ = master_core;
    
    mempool_ = mempool;
    pkt_store_ = new packet_store();
    vport_ = new dpdk::virtual_port<vport_init>(iface, mempool);

    writer_core_mask_ = writer_core_mask;
    reader_core_mask_ = reader_core_mask;

    pktgen_ = pktgen;
    generator_ = new netplay::pktgen::packet_generator<vport_init>(vport_,
        pktgen_rate_limit, 0, master_core_);
  }

  void start() {
    for (uint64_t i = 0; i < MAX_WRITERS; i++) {
      if (CORE_SET(writer_core_mask_, i)) {
        packet_store::handle* handle = pkt_store_->get_handle();
        writers_[i] = new netplay_writer<vport_init>(i, vport_, handle);
        pthread_create(&writer_thread_[i], NULL, &writer_thread<vport_init>,
                       (void*) writers_[i]);
      } else {
        writers_[i] = NULL;
      }
    }

    for (uint64_t i = 0; i < MAX_READERS; i++) {
      if (CORE_SET(reader_core_mask_, i)) {
        packet_store::handle* handle = pkt_store_->get_handle();
        readers_[i] = new netplay_reader(i, handle);
        pthread_create(&reader_thread_[i], NULL, &reader_thread,
                       (void*) readers_[i]);
      } else {
        readers_[i] = NULL;
      }
    }

    if (pktgen_) {
      generator_->generate(mempool_);
    }

    for (uint64_t i = 0; i < MAX_WRITERS; i++) {
      if (writers_[i] != NULL)
        pthread_join(writer_thread_[i], NULL);
    }

    for (uint64_t i = 0; i < MAX_READERS; i++) {
      if (readers_[i] != NULL)
        pthread_join(reader_thread_[i], NULL);
    }
  }

  uint64_t processed_pkts() {
    return pkt_store_->num_pkts();
  }

 private:
  int master_core_;

  struct rte_mempool* mempool_;

  packet_store *pkt_store_;
  dpdk::virtual_port<vport_init> *vport_;

  uint64_t writer_core_mask_;
  uint64_t reader_core_mask_;
  pthread_t writer_thread_[MAX_WRITERS];
  pthread_t reader_thread_[MAX_READERS];
  netplay_writer<vport_init>* writers_[MAX_WRITERS];
  netplay_reader* readers_[MAX_READERS];

  int pktgen_;
  netplay::pktgen::packet_generator<vport_init>* generator_;
};

}

#endif  // NETPLAYD_H_