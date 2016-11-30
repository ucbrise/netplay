#include "pktgen.h"

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#include "dpdk_utils.h"
#include "bess_init.h"
#include "ovs_init.h"
#include "pmd_init.h"
#include "pktgen.h"

const char* exec = "pktgen";
const char* desc = "%s: Random packet generator\n";
const char* usage =
  "usage: %s [OPTIONS] [INTERFACE]\n"
  "where INTERFACE is the DPDK PMD port on which %s sends packets\n";
const char* pktgen_opts =
  "\n%s options:\n"
  "  -p, --secondary-to=PRIMARY     Run as secondary process to PRIMARY\n"
  "  -c, --core=CORE                CORE on which %s should run (default: 0)\n"
  "  -r, --rate-limit=RATE          RATE (pkts/s) at which %s sends packets \n"
  "                                 (0 implies no RATE limit, default: 0)\n"
  "  -t, --time-limit=DURATION      DURATION for which %s should run\n"
  "                                 (0 implies no DURATION limit, default: 0)\n"
  "  -l, --max-packets=MAXPACKETS   MAXPACKETS for that %s should send\n"
  "                                 (default: UINT64_MAX)\n";
const char* other_opts =
  "\nOther options:\n"
  "  -h, --help                     display this help message\n";

void print_usage() {
  fprintf(stderr, usage, exec, exec);
  fprintf(stderr, pktgen_opts, exec, exec, exec, exec);
  fputs(other_opts, stderr);
}

void print_help() {
  fprintf(stderr, desc, exec);
  print_usage();
}

void prompt_help() {
  fprintf(stderr, "Try `%s --help' for more information.\n", exec);
}

void check_user() {
  uid_t euid;

  euid = geteuid();
  if (euid != 0) {
    fprintf(stderr, "ERROR: You need root privilege to run %s\n", exec);
    exit(EXIT_FAILURE);
  }

  /* Great power comes with great responsibility */
  umask(S_IWGRP | S_IWOTH);
}

int main(int argc, char** argv) {

  static struct option long_options[] = {
    {"secondary-to", required_argument, NULL, 'p'},
    {"core", required_argument, NULL, 'c'},
    {"rate-limit", required_argument, NULL, 'r'},
    {"time-limit", required_argument, NULL, 't'},
    {"max-packets", required_argument, NULL, 'l'},
    {"help", no_argument, NULL, 'h'},
    {NULL, 0, NULL, 0}
  };

  int c;
  int secondary = 0;
  int option_index = 0;
  int master_core = 0;
  char* primary = (char*) exec;
  uint64_t rate_limit = 0;
  uint64_t time_limit = 0;
  uint64_t max_pkts = UINT64_MAX;
  while ((c = getopt_long(argc, argv, "p:c:r:l:t:h", long_options, &option_index)) != -1) {
    switch (c) {
    case 0:
      break;
    case 'p':
      secondary = 1;
      primary = strdup(optarg);
      break;
    case 'c':
      master_core = atoi(optarg);
      break;
    case 'r':
      rate_limit = (uint64_t) atoll(optarg);
      break;
    case 't':
      time_limit = (uint64_t) atoll(optarg);
      break;
    case 'l':
      max_pkts = (uint64_t) atoll(optarg);
      break;
    case 'h':
      print_help();
      return 0;
    case ':':
    case '?':
      prompt_help();
      return -1;
    default:
      fprintf(stderr, "%s: invalid option -- %c\n", exec, c);
      prompt_help();
      return -1;
    }
  }

  if (optind != argc - 1) {
    fprintf(stderr, "%s expects a single INTERFACE argument\n", exec);
    prompt_help();
    return -1;
  }

  fprintf(stderr, "rate_limit=%" PRIu64 ", time_limit=%" PRIu64 "\n",
          rate_limit, time_limit);

  char *iface = strdup(argv[optind]);

  check_user();

  struct rte_mempool* mempool = netplay::dpdk::init_dpdk(primary, master_core, secondary);

  using namespace ::netplay::pktgen;
  using namespace ::netplay::dpdk;
  if (!strcmp(exec, primary)) {
    typedef virtual_port<pmd_init> vport_type;
    typedef packet_generator<vport_type> pktgen_type;
    vport_type* vport = new vport_type(iface, mempool);
    pktgen_type pktgen(vport, rate_limit, time_limit, max_pkts);
    pktgen.generate(mempool);
  } else if (!strcmp("ovs", primary)) {
    typedef virtual_port<ovs_ring_init> vport_type;
    typedef packet_generator<vport_type> pktgen_type;
    vport_type* vport = new vport_type(iface, mempool);
    pktgen_type pktgen(vport, rate_limit, time_limit, max_pkts);
    pktgen.generate(mempool);
  } else if (!strcmp("bess", primary)) {
    typedef virtual_port<bess_ring_init> vport_type;
    typedef packet_generator<vport_type> pktgen_type;
    vport_type* vport = new vport_type(iface, mempool);
    pktgen_type pktgen(vport, rate_limit, time_limit, max_pkts);
    pktgen.generate(mempool);
  } else {
    fprintf(stderr, "Primary interface %s is not yet supported.\n", primary);
    return -1;
  }

}