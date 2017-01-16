#ifndef CRITICAL_ERROR_HANDLER_H_
#define CRITICAL_ERROR_HANDLER_H_

#include <execinfo.h>
#include <signal.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>
#include <cxxabi.h>

typedef struct _sig_ucontext {
  unsigned long     uc_flags;
  struct ucontext   *uc_link;
  stack_t           uc_stack;
  struct sigcontext uc_mcontext;
  sigset_t          uc_sigmask;
} sig_ucontext_t;

void crit_err_hdlr(int sig_num, siginfo_t * info, void * ucontext) {
  sig_ucontext_t * uc = (sig_ucontext_t *)ucontext;

  /* Get the address at the time the signal was raised */
#if defined(__i386__) // gcc specific
  void *caller_address = (void *) uc->uc_mcontext.eip; // EIP: x86 specific
#elif defined(__x86_64__) // gcc specific
  void *caller_address = (void *) uc->uc_mcontext.rip; // RIP: x86_64 specific
#else
#error Unsupported architecture. // TODO: Add support for other arch.
#endif

  std::cerr << "Received signal " << sig_num
            << " (" << strsignal(sig_num) << "), address is "
            << info->si_addr << " from " << caller_address
            << std::endl;

  void * array[50];
  int size = backtrace(array, 50);

  array[1] = caller_address;

  char ** messages = backtrace_symbols(array, size);

  // skip first stack frame (points here)
  for (int i = 1; i < size && messages != NULL; ++i) {
    char *mangled_name = 0, *offset_begin = 0, *offset_end = 0;

    // find parantheses and +address offset surrounding mangled name
    for (char *p = messages[i]; *p; ++p) {
      if (*p == '(') {
        mangled_name = p;
      } else if (*p == '+') {
        offset_begin = p;
      } else if (*p == ')') {
        offset_end = p;
        break;
      }
    }

    // if the line could be processed, attempt to demangle the symbol
    if (mangled_name && offset_begin && offset_end &&
        mangled_name < offset_begin) {
      *mangled_name++ = '\0';
      *offset_begin++ = '\0';
      *offset_end++ = '\0';

      int status;
      char * real_name = abi::__cxa_demangle(mangled_name, 0, 0, &status);

      // if demangling is successful, output the demangled function name
      if (status == 0) {
        std::cerr << "[bt]: (" << i << ") " << messages[i] << ": "
                  << real_name << "+" << offset_begin << offset_end
                  << std::endl;

      }
      // otherwise, output the mangled function name
      else {
        std::cerr << "[bt]: (" << i << ") " << messages[i] << ": "
                  << mangled_name << "+" << offset_begin << offset_end
                  << std::endl;
      }
      free(real_name);
    }
    // otherwise, print the whole line
    else {
      std::cerr << "[bt]: (" << i << ") " << messages[i] << std::endl;
    }

    /* find first occurence of '(' or ' ' in message[i] and assume
     * everything before that is the file name. (Don't go beyond 0 though
     * (string terminator)*/
    int p = 0;
    while (messages[i][p] != '(' && messages[i][p] != ' '
           && messages[i][p] != 0)
      ++p;
    char syscom[256];
    sprintf(syscom, "addr2line %p -e %.*s", array[i], p, messages[i]);
    //last parameter is the file name of the symbol
    int ret = system(syscom);

    if (ret == -1) {
      fprintf(stderr, "Command %s failed\n", syscom);
    }
  }

  free(messages);

  exit(EXIT_FAILURE);
}

#endif