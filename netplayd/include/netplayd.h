#ifndef NETPLAYD_H_
#define NETPLAYD_H_

#include <stdint.h>
#include <pthread.h>

#include <rte_mbuf.h>

#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TThreadedServer.h>
#include <thrift/transport/TSocket.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/concurrency/PosixThreadFactory.h>

#include <boost/make_shared.hpp>

#include "packetstore.h"
#include "virtual_port.h"
#include "netplay_writer.h"
#include "query_handler.h"

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

template<typename vport_init>
class netplay_daemon {
 public:
  netplay_daemon(const char* iface, struct rte_mempool* mempool,
                 int master_core, uint64_t writer_core_mask, 
                 int query_server_port) {

    master_core_ = master_core;
    writer_core_mask_ = writer_core_mask;

    query_server_port_ = query_server_port;

    mempool_ = mempool;
    pkt_store_ = new packet_store();
    vport_ = new dpdk::virtual_port<vport_init>(iface, mempool);
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

    // Initialize query handler
    {
      using namespace ::apache::thrift;
      using namespace ::apache::thrift::protocol;
      using namespace ::apache::thrift::transport;
      using namespace ::apache::thrift::server;
      using namespace ::netplay::thrift;

      TThreadedServer server(
        boost::make_shared<NetPlayQueryServiceProcessor>(
          boost::make_shared<query_handler>(pkt_store_->get_handle())),
        boost::make_shared<TServerSocket>(query_server_port_), //port
        boost::make_shared<TBufferedTransportFactory>(),
        boost::make_shared<TBinaryProtocolFactory>());

      fprintf(stderr, "Starting query server on port %d\n", query_server_port_);
      try {
        server.serve();
      } catch (std::exception& e) {
        fprintf(stderr, "Query server crashed: %s\n", e.what());
      }
    }

    for (uint64_t i = 0; i < MAX_WRITERS; i++) {
      if (writers_[i] != NULL)
        pthread_join(writer_thread_[i], NULL);
    }
  }

  uint64_t processed_pkts() {
    return pkt_store_->num_pkts();
  }

 private:
  int master_core_;
  uint64_t writer_core_mask_;

  int query_server_port_;

  struct rte_mempool* mempool_;

  packet_store *pkt_store_;
  dpdk::virtual_port<vport_init> *vport_;

  pthread_t writer_thread_[MAX_WRITERS];
  netplay_writer<vport_init>* writers_[MAX_WRITERS];
};

}

#endif  // NETPLAYD_H_