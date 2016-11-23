#ifndef PMD_INIT_H_
#define PMD_INIT_H_

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

#include "dpdk_utils.h"
#include "dpdk_exception.h"

namespace netplay {
namespace dpdk {

struct pmd_init {
  inline int operator()(const char* iface, struct rte_mempool* mempool) {
    int port = atoi(iface);
    int cores[1] = { 0 };
    if (netplay::dpdk::init_pmd_port(port, 1, 1, cores, cores, 256, 256, 0, 0, 0, mempool) != 0) {
      netplay::dpdk::enumerate_pmd_ports();
      throw dpdk_exception("Could not intialize port");
    }
    return port;
  }
};

}
}

#endif  // PMD_INIT_H_