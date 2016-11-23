#ifndef TOKEN_BUCKET_H_
#define TOKEN_BUCKET_H_

#include <stdint.h>

#include <chrono>

namespace netplay {
namespace pktgen {

class token_bucket {
 public:
  token_bucket() : capacity_(0) {
    last_fill_time_ = 0;
    rate_ = 0.0;
    available_tokens_ = 0;
  }

  token_bucket(const uint64_t rate, const uint64_t max_burst)
    : capacity_((double)max_burst) {

    last_fill_time_ = cur_time_us();      // Initialize with current time
    rate_ = (double) rate / (double) 1e6; // Convert from tokens/s to tokens/us
    available_tokens_ = capacity_;        // Initially, token bucket is full
  }

  token_bucket(const token_bucket& other) : capacity_(other.capacity_) {
    last_fill_time_ = other.last_fill_time_;
    rate_ = other.rate_;
    available_tokens_ = other.available_tokens_;
  }

  bool consume(const uint64_t requested_tokens) {
    const uint64_t now = cur_time_us();

    // Fill token bucket based on rate
    const double delta = rate_ * (now - last_fill_time_);
    available_tokens_ = std::min(capacity_, available_tokens_ + delta);

    // Update last fill time for token bucket
    last_fill_time_ = now;

    // Grant tokens if available, and update available tokens accordingly
    if (requested_tokens < available_tokens_)
      available_tokens_ -= requested_tokens;
    else
      return false;

    return true;
  }

 private:
  inline uint64_t cur_time_us() {
    using namespace ::std::chrono;
    auto ts = steady_clock::now().time_since_epoch();
    return duration_cast<std::chrono::microseconds>(ts).count();
  }

  uint64_t last_fill_time_;
  double rate_;
  double available_tokens_;
  const double capacity_;
};

}
}

#endif  // TOKEN_BUCKET_H_