#ifndef OVS_INIT_H_
#define OVS_INIT_H_

#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

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
#include <rte_mbuf.h>

#include "dpdk_exception.h"

#define Q_NAME 64
#define RING_NAME "%s"
#define MP_CLIENT_RXQ_NAME "%s_tx"
#define MP_CLIENT_TXQ_NAME "%s_rx"

namespace netplay {
namespace dpdk {

struct ovs_ring_init {
  inline int operator()(const char* iface, struct rte_mempool* mempool) {
    /* Get queue names */
    char rxq_name[Q_NAME];
    char txq_name[Q_NAME];
    char port_name[Q_NAME];
    snprintf(rxq_name, Q_NAME, MP_CLIENT_RXQ_NAME, iface);
    snprintf(txq_name, Q_NAME, MP_CLIENT_TXQ_NAME, iface);
    snprintf(port_name, Q_NAME, RING_NAME, iface);
    
    struct rte_ring *rxq = rte_ring_lookup(rxq_name);
    if (rxq == NULL)
      throw dpdk_exception("could not find rx rte_ring");

    struct rte_ring *txq = rte_ring_lookup(txq_name);
    if (txq == NULL)
      throw dpdk_exception("could not find tx rte_ring");

    int port = rte_eth_from_rings(port_name, &rxq, 1, &txq, 1, 0);
    if (port < 0)
      throw dpdk_exception("Could not create eth device for ring");

    if (rte_eth_rx_queue_setup(port, 0, 32, 0, NULL, mempool) != 0)
      throw dpdk_exception("rte_eth_rx_queue_setup() failed");
    
    if (rte_eth_tx_queue_setup(port, 0, 32, 0, NULL) != 0) {
      throw dpdk_exception("rte_eth_tx_queue_setup() failed");
    }
    
    return port;
  }
};

}
}

#endif  // OVS_INIT_H_