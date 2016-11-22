#include "gtest/gtest.h"

#include <stdio.h>

#include <thread>

#include "virtual_port.h"
#include "dpdk_init.h"
#include "ovs_init.h"

class VirtualPortTest : public testing::Test {
 public:
  const uint64_t PKT_BATCH = 32;
};

TEST_F(VirtualPortTest, ReadWriteTestOVS) {
  char iface[10];
  sprintf(iface, "%u", 0);

  struct rte_mempool* mempool = netplay::dpdk::init_dpdk("ovs", 1);
  netplay::dpdk::virtual_port<netplay::dpdk::ovs_ring_init> port(iface, mempool);
}
