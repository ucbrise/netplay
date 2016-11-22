#ifndef BESS_INIT_H_
#define BESS_INIT_H_

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

#define PORT_NAME_LEN   128
#define PORT_FNAME_LEN  PORT_NAME_LEN + 256
#define MAX_QUEUES_PER_DIR 32
#define PORT_DIR_PREFIX "sn_vports"

namespace netplay {

namespace dpdk {

struct bess_ring_init {
  struct rte_ring_bar {
    char name[PORT_NAME_LEN];

    int num_inc_q;
    int num_out_q;

    struct rte_ring* inc_qs[MAX_QUEUES_PER_DIR];
    struct rte_ring* out_qs[MAX_QUEUES_PER_DIR];
  };

  inline int operator()(const char* port_name, struct rte_mempool* mempool) {
    char port_file[PORT_FNAME_LEN];
    snprintf(port_file, PORT_FNAME_LEN, "%s/%s/%s", P_tmpdir, PORT_DIR_PREFIX, port_name);
    
    FILE* fd = fopen(port_file, "r");
    if (!fd) {
      throw dpdk_exception("Could not open port file");
    }
    
    /* Assuming we need to read one pointer */
    struct rte_ring_bar *bar;
    int i = fread(&bar, 8, 1, fd);
    fclose(fd);
    
    if (i != 1)
      throw dpdk_exception("Invalid number of bytes read");
    if (bar == NULL)
      throw dpdk_exception("Could not find bar");

    struct rte_eth_conf null_conf;
    memset(&null_conf, 0, sizeof(struct rte_eth_conf));

    int port = rte_eth_from_rings(bar->name, bar->inc_qs, bar->num_inc_q,
        bar->out_qs, bar->num_out_q, 0);
    
    if (port == -1)
      throw dpdk_exception("Could not create eth ring");
    
    if(rte_eth_dev_configure(port, bar->num_inc_q, bar->num_out_q, &null_conf) < 0)
      throw dpdk_exception("Could not configure port");

    /* Set up both RX and TX cores */
    for (int i = 0; i < bar->num_inc_q; i++) {
      if (rte_eth_rx_queue_setup(port, i, 32, 0, NULL, mempool) != 0) 
        throw dpdk_exception("rte_eth_rx_queue_setup() failed");
    }

    for (int i = 0; i < bar->num_out_q; i++) {
      if (rte_eth_tx_queue_setup(port, i, 32, 0, NULL) != 0) 
        throw dpdk_exception("rte_eth_tx_queue_setup() failed");
    }

    return port;
  }
};

}
}

#endif // BESS_INIT_H_