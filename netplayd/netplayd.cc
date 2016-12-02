#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <sys/file.h>

#include <exception>
#include <new>

#include <rte_malloc.h>

#include "dpdk_utils.h"
#include "ovs_init.h"
#include "bess_init.h"
#include "netplayd.h"

#define DEFAULT_RUN_DIR  "/var/run"
#define DEFAULT_LOG_DIR  "/var/log"

// Override new and delete operators
inline void* operator new(size_t size) { 
  void* ret = rte_malloc(NULL, size, 0); 
  if (ret == NULL)
    throw std::bad_alloc();
  return ret;
}

inline void* operator new[](size_t size) { 
  void* ret = rte_malloc(NULL, size, 0); 
  if (ret == NULL)
    throw std::bad_alloc();
  return ret;
}

inline void operator delete(void* ptr) { 
  rte_free(ptr);  
}

inline void operator delete[](void* ptr) { 
  rte_free(ptr);
}


const char* exec = "netplayd";
const char* desc = "%s: Open NetPlay daemon\n";
const char* usage =
  "usage: %s [OPTIONS] [INTERFACE]\n"
  "where INTERFACE is the ring interface on which NetPlay is listening\n"
  "\nINTERFACE has the following format: <vswitch>:<interface>\n"
  "  <vswitch>                      virtual switch\n"
  "  <interface>                    DPDK ring interface\n"
  "Examples: ovs:dpdkr0, bess:rte_ring0, etc.\n";
const char* daemon_opts =
  "\nDaemon options:\n"
  "  --detach                       run in background as daemon\n"
  "  --no-chdir                     do not chdir to \'/\'\n"
  "  --pidfile[=FILE]               create pidfile FILE (default: %s/%s.pid)\n"
  "  --log-dir[=PATH]               enable logging to specified PATH (default: %s)\n";
const char* netplay_opts =
  "\nNetPlay options:\n"
  "  -m, --master-core=CORE         CORE on which master should run (default: 0)\n"
  "  -w, --writer-core-mask=MASK    core MASK for NetPlay writers (default: 0x0)\n"
  "  -q, --query-server-port=PORT   PORT mask for NetPlay writers (default: 11001)\n";
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
  }

  char* stdout_file = log_file_stdout(logprefix);
  fp = freopen(stdout_file, "w", stdout);
  if (fp == NULL) {
    fprintf(stderr, "Could not redirect output to logfile: %s\n", stdout_file);
  }
}

int main(int argc, char** argv) {
  int detach = 0;
  int nochdir = 0;

  static struct option long_options[] = {
    {"detach", no_argument, &detach, 1},
    {"no-chdir", no_argument, &nochdir, 1},
    {"pidfile", optional_argument, NULL, 'p'},
    {"log-dir", optional_argument, NULL, 'l'},
    {"master-core", required_argument, NULL, 'm'},
    {"writer-core-mask", required_argument, NULL, 'w'},
    {"query-server-port", required_argument, NULL, 'q'},
    {"help", no_argument, NULL, 'h'},
    {NULL, 0, NULL, 0}
  };

  int c;
  int option_index = 0;
  int master_core = 0;
  int query_server_port = 11001;
  uint64_t writer_core_mask = 0x0;
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
      writer_core_mask = (uint64_t) strtol(optarg, NULL, 0);
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
    netplay::netplay_daemon<netplay::dpdk::ovs_ring_init> netplayd(iface,
        mempool, master_core, writer_core_mask, query_server_port);
    netplayd.start();
  } else if (!strcmp("bess", vswitch)) {
    netplay::netplay_daemon<netplay::dpdk::bess_ring_init> netplayd(iface,
        mempool, master_core, writer_core_mask, query_server_port);
    netplayd.start();
  } else {
    fprintf(stderr, "Virtual Switch interface %s is not yet supported.\n", vswitch);
    return -1;
  }

}