#ifndef RAND_GENERATORS_H_
#define RAND_GENERATORS_H_

#include <cassert>
#include <cstdio>
#include <cmath>
#include <ctime>

class uniform_generator {
 public:
  uniform_generator(uint32_t n) {
    n_ = n;
  }

  template<typename T>
  T next() {
    return (T)(rand() % n_);
  }

 private:
  uint32_t n_;
};

class zipf_generator {
 public:
  // Constructor for zipf distribution
  zipf_generator(double theta, uint64_t n) {
    // Ensure parameters are sane
    assert(n > 0);
    assert(theta >= 0.0);
    assert(theta <= 1.0);

    // srand (time(NULL));

    theta_ = theta;
    n_ = n;
    zdist_ = new double[n];
    generate_zipf();
  }

  ~zipf_generator() {
    delete[] zdist_;
  }

  // Returns the next zipf value
  template<typename T>
  T next() {
    double r = ((double) rand() / (RAND_MAX));
    int64_t lo = 0;
    int64_t hi = n_;
    while (lo != hi) {
      int64_t mid = (lo + hi) / 2;
      if (zdist_[mid] <= r) {
        lo = mid + 1;
      } else {
        hi = mid;
      }
    }

    return (T) lo;
  }

 private:
  // Generates the zipf probability distribution
  void generate_zipf() {
    double sum = 0.0;
    double c = 0.0;
    double expo = 1.0 - theta_;
    double sumc = 0.0;
    uint64_t i;

    /*
     * zipfian - p(i) = c / i ^^ (1 - theta)
     * At theta = 1, uniform
     * At theta = 0, pure zipfian
     */

    for (i = 1; i <= n_; i++) {
      sum += 1.0 / pow((double) i, (double) (expo));

    }
    c = 1.0 / sum;

    for (i = 0; i < n_; i++) {
      sumc += c / pow((double) (i + 1), (double) (expo));
      zdist_[i] = sumc;
    }
  }

  double theta_;       // The skew parameter (0=pure zipf, 1=pure uniform)
  uint64_t n_;         // The number of objects
  double *zdist_;
};

#endif