#ifndef COMPLEX_CHARACTER_H_
#define COMPLEX_CHARACTER_H_

#include <vector>

#include "packet_filter.h"
#include "monolog.h"

namespace netplay {

class complex_character {
 public:
  class iterator : __input_iterator {
   public:
    typedef uint64_t value_type;
    typedef uint64_t difference_type;
    typedef const uint64_t* pointer;
    typedef uint64_t reference;

    complex_character(const uint64_t cur_idx) {
      cur_idx_ = cur_idx;
    }

    reference operator*() const {
      return monolog_->get(cur_idx_);
    }

    complex_character& operator++() {
      cur_idx_++;
      return *this;
    }

    complex_character operator++(int) {
      complex_character it = *this;
      ++(*this);
      return it;
    }

    bool operator==(complex_character other) const {
      return cur_idx_ == other.cur_idx_;
    }

    bool operator!=(complex_character other) const {
      return !(*this == other);
    }

   private:
    uint64_t cur_idx_;
    slog::monolog_relaxed<uint64_t, 24>* monolog_;  
  };

  /**
    * Constructor to initialize complex character.
    *
    * @param filters Packet filters to use for this complex character.
    */
  complex_character(const std::vector<packet_filter>& filters) {
    monolog_ = new slog::entry_list;
    filters_ = filters;
  }

  /**
   * Filters packets using the packet filters, and adds the packet id if the
   * packet passes through any filter.
   *
   * @param pkt_id The id of the packet.
   * @param pkt The packet data.
   * @param ts The packet timestamp.
   */
  void check_and_add(uint64_t pkt_id, const void* pkt) {
    for (const packet_filter& filter: filters_) {
      if (filter_.apply(pkt)) {
        monolog_->push_back(pkt_id);
        return;
      }
    }
  }

  iterator begin() {
    return iterator(0);
  }

  iterator end() {
    return iterator(monolog_->size());
  }

 private:
  /* Packet filters */
  const std::vector<packet_filter> filters_;

  /* Monolog */
  slog::monolog_relaxed<uint64_t, 24>* monolog_;
}

}

#endif