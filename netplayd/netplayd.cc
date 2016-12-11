#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <sys/file.h>

#include <exception>
#include <new>
#include <map>

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
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>
#include <rte_lpm.h>
#include <rte_mbuf.h>

#include "dpdk_utils.h"
#include "ovs_init.h"
#include "bess_init.h"
#include "netplayd.h"

#define DEFAULT_RUN_DIR  "/var/run"
#define DEFAULT_LOG_DIR  "/var/log"

const char* exec = "netplayd";
const char* desc = "%s: Open NetPlay daemon\n";
const char* usage =
  "usage: %s [OPTIONS] [VIRTUAL-SWITCH]\n"
  "where VIRTUAL-SWITCH is the virtual switch which NetPlay should connect to.\n"
  "Examples: ovs, bess, etc.\n";
const char* daemon_opts =
  "\nDaemon options:\n"
  "  --detach                       run in background as daemon\n"
  "  --no-chdir                     do not chdir to \'/\'\n"
  "  --pidfile[=FILE]               create pidfile FILE (default: %s/%s.pid)\n"
  "  --log-dir[=PATH]               enable logging to specified PATH (default: %s)\n";
const char* netplay_opts =
  "\nNetPlay options:\n"
  "  -m, --master-core=CORE         CORE on which master should run (default: 0)\n"
  "  -w, --writer-mappings=MAPPINGS comma separated MAPPINGS between NetPlay writer\n"
  "                                 core and DPDK ring buffer interface it should\n"
  "                                 poll; each mapping is of the form:\n"
  "                                 <core>:<interface> (default: empty)\n"
  "  -q, --query-server-port=PORT   PORT mask for NetPlay writers (default: 11001)\n"
  "  --bench                        Run benchmark (Measures throughput and dies)\n";
const char* other_opts =
  "\nOther options:\n"
  "  -h, --help                     display this help message\n";

void print_usage() {
  fprintf(stderr, usage, exec);
  fprintf(stderr, daemon_opts, DEFAULT_RUN_DIR, exec, DEFAULT_LOG_DIR);
  fputs(netplay_opts, stderr);
  fputs(other_opts, stderr);
}

void print_help() {
  fprintf(stderr, desc, exec);
  print_usage();
}

void prompt_help() {
  fprintf(stderr, "Try `%s --help' for more information.\n", exec);
}

char* default_pid_file() {
  char tmp[64];
  sprintf(tmp, "%s/%s.pid", DEFAULT_RUN_DIR, exec);
  return strdup(tmp);
}

char* log_file_stderr(char *prefix = DEFAULT_LOG_DIR) {
  char tmp[64];
  sprintf(tmp, "%s/%s.stderr", prefix, exec);
  return strdup(tmp);
}

char* log_file_stdout(char *prefix = DEFAULT_LOG_DIR) {
  char tmp[64];
  sprintf(tmp, "%s/%s.stdout", prefix, exec);
  return strdup(tmp);
}

void check_user() {
  uid_t euid;

  euid = geteuid();
  if (euid != 0) {
    fprintf(stderr, "ERROR: You need root privilege to run NetPlay daemon\n");
    exit(EXIT_FAILURE);
  }

  /* Great power comes with great responsibility */
  umask(S_IWGRP | S_IWOTH);
}

void lock_pidfile(char* pidfile) {
  pid_t pid;
  char buf[1024];

  int fd = open(pidfile, O_RDWR | O_CREAT, 0644);
  if (fd == -1) {
    fprintf(stderr, "Could not open pidfile: %s\n", pidfile);
    exit(EXIT_FAILURE);
  }

  int ret = flock(fd, LOCK_EX | LOCK_NB);
  if (ret) {
    if (errno != EWOULDBLOCK) {
      fprintf(stderr, "Could not lock pidfile: %s\n", pidfile);
      exit(EXIT_FAILURE);
    }

    /* lock is already acquired */
    ret = read(fd, buf, sizeof(buf) - 1);
    if (ret <= 0) {
      fprintf(stderr, "Could not read from pidfile: %s\n", pidfile);
      exit(EXIT_FAILURE);
    }

    buf[ret] = '\0';

    sscanf(buf, "%d", &pid);
    fprintf(stderr, "ERROR: There is another %s instance running (PID=%d)\n", exec, pid);
    exit(EXIT_FAILURE);
  }

  ret = ftruncate(fd, 0);
  if (ret) {
    fprintf(stderr, "Could not truncate pidfile: %s\n", pidfile);
    exit(EXIT_FAILURE);
  }

  ret = lseek(fd, 0, SEEK_SET);
  if (ret) {
    fprintf(stderr, "Could not seek to beginning of pidfile: %s\n", pidfile);
    exit(EXIT_FAILURE);
  }

  pid = getpid();
  ret = sprintf(buf, "%d\n", pid);

  ret = write(fd, buf, ret);
  if (ret < 0) {
    fprintf(stderr, "Could not write to pidfile: %s\n", pidfile);
    exit(EXIT_FAILURE);
  }
}

void redirect_output(char* logprefix) {
  FILE* fp;

  char* stderr_file = log_file_stderr(logprefix);
  fp = freopen(stderr_file, "w", stderr);
  if (fp == NULL) {
    fprintf(stderr, "Could not redirect output to logfile: %s\n", stderr_file);
    exit(EXIT_FAILURE);
  }

  char* stdout_file = log_file_stdout(logprefix);
  fp = freopen(stdout_file, "w", stdout);
  if (fp == NULL) {
    fprintf(stderr, "Could not redirect output to logfile: %s\n", stdout_file);
    exit(EXIT_FAILURE);
  }
}

void parse_writer_mapping(std::map<int, std::string>& writer_mapping,
                          char* mapping_str) {

  char* cur_mapping = strsep(&mapping_str, ",");
  while (cur_mapping != NULL) {
    char* core_str = strsep(&cur_mapping, ":");
    char* iface_str = strsep(&cur_mapping, ":");

    if (core_str == NULL) {
      fprintf(stderr, "Could not parse writer mapping (invalid core): %s\n",
              cur_mapping);
      exit(EXIT_FAILURE);
    }

    if (iface_str == NULL) {
      fprintf(stderr, "Could not parse writer mapping (invalid iface): %s\n",
              cur_mapping);
      exit(EXIT_FAILURE);
    }

    int core = atoi(core_str);
    std::string iface = std::string(iface_str);
    writer_mapping[core] = iface;

    cur_mapping = strsep(&mapping_str, ",");
  }
}

int main(int argc, char** argv) {
  int detach = 0;
  int nochdir = 0;
  int bench = 0;

  static struct option long_options[] = {
    {"detach", no_argument, &detach, 1},
    {"no-chdir", no_argument, &nochdir, 1},
    {"pidfile", optional_argument, NULL, 'p'},
    {"log-dir", optional_argument, NULL, 'l'},
    {"master-core", required_argument, NULL, 'm'},
    {"writer-mappings", required_argument, NULL, 'w'},
    {"query-server-port", required_argument, NULL, 'q'},
    {"bench", no_argument, NULL, 'b'},
    {"help", no_argument, &bench, 1},
    {NULL, 0, NULL, 0}
  };

  int c;
  int option_index = 0;
  int master_core = 0;
  int query_server_port = 11001;
  std::map<int, std::string> writer_mapping;
  char* pidfile = NULL;
  char* logprefix = NULL;
  while ((c = getopt_long(argc, argv, "m:w:q:hp::l::", long_options, &option_index)) != -1) {
    switch (c) {
    case 0:
      break;
    case 'p':
      if (optarg) {
        pidfile = strdup(optarg);
      } else {
        pidfile = default_pid_file();
      }
      break;
    case 'l':
      if (optarg) {
        logprefix = strdup(optarg);
      } else {
        logprefix = strdup(DEFAULT_LOG_DIR);
      }
    case 'm':
      master_core = atoi(optarg);
      break;
    case 'w':
      parse_writer_mapping(writer_mapping, optarg);
      break;
    case 'q':
      query_server_port = atoi(optarg);
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

  char *vswitch = argv[optind];

  check_user();

  if (detach) {
    int noclose = (logprefix != NULL);
    if (daemon(nochdir, noclose) == -1) {
      fprintf(stderr, "Error creating daemon\n");
    }
  }

  if (pidfile) {
    lock_pidfile(pidfile);
  }

  if (logprefix) {
    redirect_output(logprefix);
  }

  struct rte_mempool* mempool = netplay::dpdk::init_dpdk(vswitch, master_core, 1);
  if (!strcmp("ovs", vswitch)) {
    typedef netplay::netplay_daemon<netplay::dpdk::ovs_ring_init> daemon_t;
    daemon_t netplayd(writer_mapping, mempool, query_server_port);
    netplayd.start();
    if (bench) {
      netplayd.bench();
    } else {
      netplayd.monitor();
    }
  } else if (!strcmp("bess", vswitch)) {
    typedef netplay::netplay_daemon<netplay::dpdk::bess_ring_init> daemon_t;
    daemon_t netplayd(writer_mapping, mempool, query_server_port);
    netplayd.start();
    if (bench) {
      netplayd.bench();
    } else {
      netplayd.monitor();
    }
  } else {
    fprintf(stderr, "Virtual Switch interface %s is not yet supported.\n", vswitch);
    return -1;
  }

}