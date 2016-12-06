#ifndef COMPLEX_CHARACTER_H_
#define COMPLEX_CHARACTER_H_

#include <vector>
#include <cstdint>

#include "packet_filter.h"
#include "monolog.h"

namespace netplay {

class complex_character {
 public:
  typedef std::pair<uint64_t, uint64_t> time_range;
  struct time_id {
    uint64_t id;
    uint64_t ts;
  };
  typedef slog::monolog_relaxed<time_id, 24> monolog_type;

  class result {
   public:
    class iterator : __input_iterator {
     public:
      typedef uint64_t value_type;
      typedef uint64_t difference_type;
      typedef const uint64_t* pointer;
      typedef uint64_t reference;

      iterator(const uint64_t cur_idx,
               uint64_t max_rid,
               const time_range range,
               const uint32_t monolog_size,
               monolog_type* monolog) {
        cur_idx_ = cur_idx;
        max_rid_ = max_rid;
        range_ = range;
        monolog_size_ = monolog_size;
        monolog_ = monolog;
      }

      reference operator*() const {
        return monolog_->get(cur_idx_).id;
      }

      iterator& operator++() {
        if (cur_idx_ == monolog_size_)
          return *this;

        time_id val;
        do {
          cur_idx_++;
          if (cur_idx_ != monolog_size_)
            val = monolog_->get(cur_idx_);
        } while (cur_idx_ != monolog_size_ && !is_valid(val));

        return *this;
      }

      iterator operator++(int) {
        iterator it = *this;
        ++(*this);
        return it;
      }

      bool operator==(iterator other) const {
        return cur_idx_ == other.cur_idx_;
      }

      bool operator!=(iterator other) const {
        return !(*this == other);
      }

     private:
      inline bool is_valid(const time_id& val) {
        return val.ts >= range_.first && val.ts <= range_.second
               && val.id < max_rid_;
      }

      uint64_t max_rid_;
      time_range range_;
      uint64_t cur_idx_;
      uint32_t monolog_size_;
      monolog_type* monolog_;
    };

    result(uint64_t max_rid, const time_range range,
           const uint32_t monolog_size,
           monolog_type* monolog) {
      max_rid_ = max_rid;
      range_ = range;
      monolog_size_ = monolog_size;
      monolog_ = monolog;
    }

    iterator begin() {
      return iterator(0, max_rid_, range_, monolog_size_, monolog_);
    }

    iterator end() {
      return iterator(monolog_size_, max_rid_, range_, monolog_size_,
                      monolog_);
    }

   private:
    uint64_t max_rid_;
    time_range range_;
    uint32_t monolog_size_;
    monolog_type* monolog_;
  };

  /**
    * Constructor to initialize complex character.
    *
    * @param filters Packet filters to use for this complex character.
    */
  complex_character(const std::vector<packet_filter>& filters)
    : filters_(filters) {
    monolog_ = new monolog_type();
  }

  /**
   * Filters packets using the packet filters, and adds the packet id if the
   * packet passes through any filter.
   *
   * @param pkt_id The id of the packet.
   * @param pkt The packet data.
   * @param ts The packet timestamp.
   */
  void check_and_add(uint64_t pkt_id, void* pkt, uint64_t ts) {
    for (const packet_filter& filter : filters_) {
      if (filter.apply(pkt)) {
        monolog_->push_back({ pkt_id, ts });
        return;
      }
    }
  }

  result filter(uint64_t max_rid, const time_range range) {
    return result(max_rid, range, monolog_->size(), monolog_);
  }

 private:
  /* Packet filters */
  const std::vector<packet_filter> filters_;

  /* Monolog */
  monolog_type* monolog_;
};

}

#endif