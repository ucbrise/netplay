#ifndef RATE_LIMITER_H_
#define RATE_LIMITER_H_

#include <chrono>
#include <iostream>
#include <fstream>

#define BURST_SIZE          32
#define BATCH_SIZE          65536

using namespace ::std::chrono;

template<size_t batch_size>
class pacer {
 public:
  pacer(uint64_t rate) {
    rate_ = rate;
    batch_size_ = batch_size;
    min_batch_ns_ = 0;
    if (rate_ != 0)
      min_batch_ns_ = (1e9 * batch_size) / rate_;
    tspec_.tv_sec = 0;
    num_ops_ = 0;

    epoch_ = high_resolution_clock::now();
  }

  void pace() {
    num_ops_++;
    if (rate_ != 0 && num_ops_ % batch_size == 0) {
      auto now = high_resolution_clock::now();
      uint64_t batch_ns = duration_cast<nanoseconds>(now - epoch_).count();
      if (batch_ns < min_batch_ns_) {
        tspec_.tv_nsec = (min_batch_ns_ - batch_ns);
        nanosleep(&tspec_, NULL);
      }
      epoch_ = high_resolution_clock::now();
    }
  }

  uint64_t num_ops() {
    return num_ops_;
  }

 private:
  uint64_t num_ops_;
  uint64_t rate_;
  uint64_t batch_size_;
  uint64_t min_batch_ns_;

  time_point<high_resolution_clock> epoch_;

  struct timespec tspec_;
};

template<typename vport_type, typename generator_type, size_t batch_size = BATCH_SIZE, size_t burst_size = BURST_SIZE>
class rate_limiter {
 public:
  rate_limiter(vport_type* vport, generator_type* generator,
               uint64_t rate, uint64_t pkt_limit) {

    rate_ = rate;
    pkt_limit_ = pkt_limit;

    tot_sent_pkts_ = 0;

    vport_ = vport;
    generator_ = generator;
  }

  void generate() {
    double min_batch_ns = 0;
    if (rate_ != 0) {
      min_batch_ns = (double) (1e9 * batch_size) / (double) rate_;
      fprintf(stderr, "%zu ops per %lf ns.\n", batch_size, min_batch_ns);
    }

    struct timespec tspec;
    tspec.tv_sec = 0;

#if MEASURE_LATENCY
    std::ofstream out("write_batch_latency.txt");
#endif

    auto start = high_resolution_clock::now();
    auto epoch = start;
    while (tot_sent_pkts_ < pkt_limit_) {
      auto pkts = generator_->generate_batch(burst_size);
#if MEASURE_LATENCY
      auto start = high_resolution_clock::now();
#endif
      uint16_t sent = vport_->send_pkts(pkts, burst_size);
#if MEASURE_LATENCY
      auto end = high_resolution_clock::now();
      uint64_t batch_ns = duration_cast<nanoseconds>(end - start).count();
      out << batch_ns << "\n";
#endif
      tot_sent_pkts_ += sent;
      if (rate_ != 0 && tot_sent_pkts_ % batch_size == 0) {
        auto now = high_resolution_clock::now();
        auto batch_ns = duration_cast<nanoseconds>(now - epoch).count();
        if (batch_ns < min_batch_ns) {
          tspec.tv_nsec = (min_batch_ns - batch_ns);
          nanosleep(&tspec, NULL);
        }
        epoch = high_resolution_clock::now();
      }
    }
  }

  uint64_t total_sent() {
    return tot_sent_pkts_;
  }

 private:
  uint64_t rate_;
  uint64_t pkt_limit_;
  uint64_t tot_sent_pkts_;

  vport_type* vport_;
  generator_type* generator_;
};

#endif