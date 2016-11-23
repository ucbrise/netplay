#ifndef DPDK_UTILS_H_
#define DPDK_UTILS_H_

#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <string>

#include <rte_config.h>
#include <rte_byteorder.h>
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

#define NUM_PFRAMES         512     // Number of pframes in the mempool
#define CACHE_SIZE          32      // Size of per-core mempool cache
#define METADATA_SLOT_SIZE  8       // size in bytes of a metadata slot

namespace netplay {
namespace dpdk {

typedef struct rte_mbuf** mbuf_array_t;

namespace mempool {

struct rte_mempool* init_mempool(int master_core,
                                 unsigned int mempool_size,
                                 unsigned int mcache_size,
                                 unsigned short metadata_slots) {

  int sid = rte_lcore_to_socket_id(master_core);

  char name[256];
  sprintf(name, "pframe%d", sid);
  return rte_pktmbuf_pool_create(name,
                                 mempool_size,
                                 mcache_size,
                                 metadata_slots * METADATA_SLOT_SIZE,
                                 RTE_MBUF_DEFAULT_BUF_SIZE,
                                 sid);
}

static void find_mempool_helper(struct rte_mempool *mp, void *ptr) {
  const struct rte_mempool **result = static_cast<const struct rte_mempool**>(ptr);
  if (mp != NULL && (*result == NULL || (*result)->size < mp->size)) {
    *result = mp;
  }
}

struct rte_mempool* find_secondary_mempool() {
  struct rte_mempool *mempool = NULL;
  rte_mempool_walk(&find_mempool_helper, (void*)&mempool);
  if (mempool == NULL) {
    throw dpdk_exception("Could not find secondary mempool");
  }
  printf("Chose memory pool %s\n", mempool->name);
  return mempool;
}

struct rte_mbuf* mbuf_alloc(struct rte_mempool* mempool) {
  return rte_pktmbuf_alloc(mempool);
}

void mbuf_free(struct rte_mbuf* buf) {
  rte_pktmbuf_free(buf);
}

static inline int mbuf_alloc_bulk(mbuf_array_t array, uint16_t len, int cnt,
                                  struct rte_mempool* mempool) {

  int ret = rte_mempool_get_bulk(mempool, (void**)array, cnt);
  if (ret != 0) {
    return ret;
  }

  for (int i = 0; i < cnt; i++) {
    rte_mbuf_refcnt_set(array[i], 1);
    rte_pktmbuf_reset(array[i]);
    array[i]->pkt_len = array[i]->data_len = len;
  }

  return 0;
}

int mbuf_free_bulk(mbuf_array_t array, int cnt) {
  for (int i = 0; i < cnt; i++)
    mbuf_free(array[i]);
  return 0;
}

void dump_pkt(struct rte_mbuf* buf) {
  rte_pktmbuf_dump(stdout, buf, 16384);
}

}

void enumerate_pmd_ports() {
  int num_dpdk_ports = rte_eth_dev_count();
  int i;

  printf("%d DPDK PMD ports have been recognized:\n", num_dpdk_ports);
  for (i = 0; i < num_dpdk_ports; i++) {
    struct rte_eth_dev_info dev_info;

    memset(&dev_info, 0, sizeof(dev_info));
    rte_eth_dev_info_get(i, &dev_info);

    printf("DPDK port_id %d (%s)   RXQ %hu TXQ %hu  ", i, dev_info.driver_name, dev_info.max_rx_queues,
           dev_info.max_tx_queues);

    if (dev_info.pci_dev) {
      printf("%04hx:%02hhx:%02hhx.%02hhx %04hx:%04hx  ", dev_info.pci_dev->addr.domain,
             dev_info.pci_dev->addr.bus, dev_info.pci_dev->addr.devid, dev_info.pci_dev->addr.function,
             dev_info.pci_dev->id.vendor_id, dev_info.pci_dev->id.device_id);
    }

    printf("\n");
  }
}

struct rte_eth_conf default_eth_conf() {
  struct rte_eth_conf conf = {};
  conf.link_speeds = ETH_LINK_SPEED_AUTONEG;
  conf.rxmode.mq_mode = ETH_MQ_RX_RSS;
  conf.rxmode.hw_strip_crc = 1;
  conf.txmode.mq_mode = ETH_MQ_TX_NONE;
  conf.lpbk_mode = 0;
  conf.rx_adv_conf.rss_conf.rss_key = NULL;
  conf.rx_adv_conf.rss_conf.rss_hf = ETH_RSS_IP | ETH_RSS_UDP | ETH_RSS_TCP | ETH_RSS_SCTP;
  conf.fdir_conf.mode = RTE_FDIR_MODE_NONE;
  conf.intr_conf.lsc = 0;
  return conf;
}

int init_pmd_port(int port, int rxqs, int txqs, int rxq_core[], int txq_core[], int nrxd, int ntxd, int loopback,
                  int tso, int csumoffload, struct rte_mempool* mempool) {
  struct rte_eth_dev_info dev_info = {};
  struct rte_eth_conf eth_conf;
  struct rte_eth_rxconf eth_rxconf;
  struct rte_eth_txconf eth_txconf;
  int ret, i;

  /* Need to accesss rte_eth_devices manually since DPDK currently
   * provides no other mechanism for checking whether something is
   * attached */
  if (port >= RTE_MAX_ETHPORTS || !rte_eth_devices[port].attached) {
    fprintf(stderr, "Port not found %d\n", port);
    return -ENODEV;
  }

  eth_conf = default_eth_conf();
  eth_conf.lpbk_mode = !(!loopback);

  /* Use defaut rx/tx configuration as provided by PMD drivers,
   * with minor tweaks */
  rte_eth_dev_info_get(port, &dev_info);

  eth_rxconf = dev_info.default_rxconf;
  /* Drop packets when no descriptors are available */
  eth_rxconf.rx_drop_en = 1;

  eth_txconf = dev_info.default_txconf;
  tso = !(!tso);
  csumoffload = !(!csumoffload);
  eth_txconf.txq_flags =
    ETH_TXQ_FLAGS_NOVLANOFFL | ETH_TXQ_FLAGS_NOMULTSEGS * (1 - tso) | ETH_TXQ_FLAGS_NOXSUMS * (1 - csumoffload);

  ret = rte_eth_dev_configure(port, rxqs, txqs, &eth_conf);
  if (ret != 0) {
    return ret; /* Don't need to clean up here */
  }

  /* Set to promiscuous mode */
  rte_eth_promiscuous_enable(port);

  for (i = 0; i < rxqs; i++) {
    int sid = rte_lcore_to_socket_id(rxq_core[i]);
    ret = rte_eth_rx_queue_setup(port, i, nrxd, sid, &eth_rxconf, mempool);
    if (ret != 0) {
      fprintf(stderr, "Failed to initialize rxq\n");
      return ret; /* Clean things up? */
    }
  }

  for (i = 0; i < txqs; i++) {
    int sid = rte_lcore_to_socket_id(txq_core[i]);

    ret = rte_eth_tx_queue_setup(port, i, ntxd, sid, &eth_txconf);
    if (ret != 0) {
      fprintf(stderr, "Failed to initialize txq\n");
      return ret; /* Clean things up */
    }
  }

  ret = rte_eth_dev_start(port);
  if (ret != 0) {
    fprintf(stderr, "Failed to start \n");
    return ret; /* Clean up things */
  }
  return 0;
}

/* Called by each secondary thread, responsible for affinitization, etc. */
void init_thread(int core) {
  /* Among other things this affinitizes the thread */
  rte_cpuset_t cpuset;
  CPU_ZERO(&cpuset);
  CPU_SET(core, &cpuset);
  rte_thread_set_affinity(&cpuset);

  /* Set thread ID correctly */
  // RTE_PER_LCORE(_mempool_core) = core;
}

/* Get NUMA count */
static int get_numa_count() {
  FILE* fp;

  int matched;
  int cnt;

  fp = fopen("/sys/devices/system/node/possible", "r");
  if (!fp)
    goto fail;

  matched = fscanf(fp, "0-%d", &cnt);
  if (matched == 1)
    return cnt + 1;

fail:
  if (fp)
    fclose(fp);

  fprintf(stderr,
          "Failed to detect # of NUMA nodes from: "
          "/sys/devices/system/node/possible. "
          "Assuming a single-node system...\n");
  return 1;
}

static int init_eal(const char* name, int core, int secondary) {
  /* As opposed to SoftNIC, this call only initializes the master thread.
   * We cannot rely on threads launched by DPDK within ZCSI, the threads
   * must be launched by the runtime */
  int rte_argc = 0;

  /* FIXME: Make sure number of arguments is not exceeded */
  char* rte_argv[128];

  char opt_master_lcore[1024];
  char opt_lcore_bitmap[1024];
  char opt_socket_mem[1024];

  const char* socket_mem = "1024";

  int numa_count = get_numa_count();

  int ret;
  int i;

  if (core > RTE_MAX_LCORE) {
    return -1;
  }

  sprintf(opt_master_lcore, "%d", core);

  /* We need to tell rte_eal_init that it should use all possible lcores.
   * If not, it does an insane thing and 0s out the cpusets for any unused
   * physical cores and will not work when new threads are allocated. We
   * could hack around this another way, but this seems more reasonable.*/
  sprintf(opt_lcore_bitmap, "0x%x", (1u << core));

  sprintf(opt_socket_mem, "%s", socket_mem);
  for (i = 1; i < numa_count; i++) sprintf(opt_socket_mem + strlen(opt_socket_mem), ",%s", socket_mem);

  char proc_name[256];
  sprintf(proc_name, "netplay-%s", name);
  rte_argv[rte_argc++] = proc_name;

  if (secondary) {
    rte_argv[rte_argc++] = "--proc-type";
    rte_argv[rte_argc++] = "secondary";
  }

  rte_argv[rte_argc++] = "--file-prefix";
  rte_argv[rte_argc++] = (char*) name;
  rte_argv[rte_argc++] = "-c";
  rte_argv[rte_argc++] = opt_lcore_bitmap;

  /* This just makes sure that by default everything is blacklisted */
  rte_argv[rte_argc++] = "-w";
  rte_argv[rte_argc++] = "99:99.0";

  rte_argv[rte_argc++] = "--master-lcore";
  rte_argv[rte_argc++] = opt_master_lcore;

  /* number of memory channels (Sandy Bridge) */
  rte_argv[rte_argc++] = "-n";
  rte_argv[rte_argc++] = "4";

  rte_argv[rte_argc++] = "--socket-mem";
  rte_argv[rte_argc++] = opt_socket_mem;
  rte_argv[rte_argc] = NULL;

  /* reset getopt() */
  optind = 0;

  /* rte_eal_init: Initializes EAL */
  ret = rte_eal_init(rte_argc, rte_argv);
  if (secondary && rte_eal_process_type() != RTE_PROC_SECONDARY)
    rte_panic("Not a secondary process");

  /* Change lcore ID */
  RTE_PER_LCORE(_lcore_id) = core;
  return ret;
}

struct rte_mempool* init_dpdk(const std::string& name, int core, int secondary) {
  // FIXME: Put back
  // rte_timer_subsystem_init();
  if (init_eal(name.c_str(), core, secondary) < 0)
    throw dpdk_exception("init_eal() failed");

  struct rte_mempool* pool = NULL;
  if (secondary) {
    pool = mempool::find_secondary_mempool();
  } else {
    fprintf(stderr, "Initializing primary process mempool\n");
    pool = mempool::init_mempool(core, NUM_PFRAMES, CACHE_SIZE, 1);
    fprintf(stderr, "Created mempool %s\n", pool->name);
  }

  if (pool == NULL)
    rte_panic("Pool is NULL\n");
  return pool;
}
}
}

#endif  // DPDK_UTILS_H_