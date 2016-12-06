#ifndef RATE_LIMITER_H_
#define RATE_LIMITER_H_

#include <chrono>

#define BURST_SIZE          32
#define BATCH_SIZE          8192

using namespace ::std::chrono;

template<typename vport_type, typename generator_type>
class rate_limiter {
 public:
  rate_limiter(vport_type* vport, generator_type* generator,
               uint64_t rate, uint64_t pkt_limit) {

    rate_ = rate;
    pkt_limit_ = pkt_limit;
    
    min_batch_ns_ = (1e9 * BATCH_SIZE) / rate;
    tot_sent_pkts_ = 0;
    
    tspec_.tv_sec = 0;

    fprintf(stderr, "%d ops per %lld ns.\n", BATCH_SIZE, min_batch_ns_);

    vport_ = vport;
    generator_ = generator;
  }

  void generate() {
    auto start = high_resolution_clock::now();
    auto epoch = start;
    while (tot_sent_pkts_ < pkt_limit_) {
      auto pkts = generator_->generate_batch(RTE_BURST_SIZE);
      uint16_t sent = vport_->send_pkts(pkts, RTE_BURST_SIZE);
      tot_sent_pkts_ += sent;
      if (tot_sent_pkts_ % BATCH_SIZE == 0) {
        auto now = high_resolution_clock::now();
        auto batch_ns = duration_cast<nanoseconds>(now - epoch).count();
        if (batch_ns < min_batch_ns_) {
          tspec_.tv_nsec = (min_batch_ns_ - batch_ns);
          nanosleep(&tspec_, NULL);
        }
        epoch = high_resolution_clock::now();
      }
    }
  }

  uint64_t total_sent() {
    return tot_sent_pkts_;
  }

 private:
  struct timespec tspec_;
  uint64_t rate_;
  uint64_t tot_sent_pkts_;
  double min_batch_ns_;
  uint64_t pkt_limit_;
  
  vport_type* vport_;
  generator_type* generator_;
};

#endif