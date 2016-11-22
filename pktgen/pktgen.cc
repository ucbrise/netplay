#include "pktgen.h"

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#include "dpdk_utils.h"
#include "pktgen.h"

const char* exec = "pktgen";
const char* desc = "%s: Random packet generator\n";
const char* usage =
  "usage: %s [OPTIONS] [INTERFACE]\n"
  "where INTERFACE is the DPDK interface on which %s sends packets\n"
  "\nINTERFACE has the following format: <vswitch>:<pmd-port>\n"
  "  <vswitch>                      virtual switch (ovs, bess, etc.)\n"
  "  <pmd-port>                     DPDK PMD port\n";
const char* pktgen_opts =
  "\n%s options:\n"
  "  -c, --core=CORE                CORE on which %s should run (default: 0)\n"
  "  -r, --rate-limit=RATE          RATE (pkts/s) at which %s sends packets \n"
  "                                 (0 implies no RATE limit, default: 0)\n"
  "  -t, --time-limit=DURATION      DURATION for which %s should run\n" 
  "                                 (0 implies no DURATION limit, default: 0)\n";
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
    {"core", required_argument, NULL, 'c'},
    {"rate-limit", required_argument, NULL, 'r'},
    {"time-limit", required_argument, NULL, 't'},
    {"help", no_argument, NULL, 'h'},
    {NULL, 0, NULL, 0}
  };

  int c;
  int option_index = 0;
  int master_core = 0;
  uint64_t rate_limit = 0;
  uint64_t time_limit = 0;
  while ((c = getopt_long(argc, argv, "c:r:t:h", long_options, &option_index)) != -1) {
    switch (c) {
    case 0:
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

  char *vswitch;
  char *iface;
  vswitch = strsep(&argv[optind], ":");
  iface = strsep(&argv[optind], ":");
  if (vswitch == NULL || iface == NULL) {
    fprintf(stderr, "%s: Invalid INTERFACE\n", exec);
    prompt_help();
    return -1;
  }

  check_user();

  struct rte_mempool* mempool = netplay::dpdk::init_dpdk(vswitch, master_core);
  netplay::dpdk::enumerate_pmd_ports();
  
  if (!strcmp("ovs", vswitch)) {
    netplay::pktgen::packet_generator pktgen(iface, mempool, rate_limit, time_limit);
    pktgen.generate();
  } else if (!strcmp("bess", vswitch)) {
    fprintf(stderr, "Virtual Switch interface %s is not yet supported.\n", vswitch);
    return -1;
  } else {
    fprintf(stderr, "Virtual Switch interface %s is not yet supported.\n", vswitch);
    return -1;
  }

}