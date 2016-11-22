#ifndef DPDK_EXCEPTION_H_
#define DPDK_EXCEPTION_H_

#include <exception>

class dpdk_exception : public std::exception {
 public:
  dpdk_exception(const char* msg) : msg_(msg) {
  }

  const char* what() const throw() {
    return msg_;
  }

 private:
  const char* msg_;
};

#endif  // DPDK_EXCEPTION_H_