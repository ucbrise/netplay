#ifndef COMPLEX_CHARACTER_INDEX_H_
#define COMPLEX_CHARACTER_INDEX_H_

#include <vector>
#include <cstdint>

#include "tieredindex.h"
#include "packet_filter.h"
#include "monolog.h"

namespace netplay {

typedef std::pair<uint64_t, uint64_t> time_range;

class complex_character_index {
 public:
  typedef slog::indexlet<slog::entry_list> char_index;
  typedef slog::__index_depth2<65536, 65536, char_index> time_char_index;

  class result {
   public:
    class iterator : __input_iterator {
     public:
      typedef uint64_t value_type;
      typedef uint64_t difference_type;
      typedef const uint64_t* pointer;
      typedef uint64_t reference;

      iterator(const result* res) {
        res_ = res;

        cur_idx_ = -1;
        cur_ts_ = res_->range_.first;
        cur_list_ = NULL;
        char_index* c = res_->index_->at(cur_ts_);
        if (c != NULL)
          cur_list_ = c->at(res_->char_id_);

        advance();
      }

      iterator(const int64_t idx, const uint64_t ts) {
        res_ = NULL;

        cur_idx_ = idx;
        cur_ts_ = ts;
        cur_list_ = NULL;
      }

      iterator(const iterator& other) {
        res_ = other.res_;

        cur_idx_ = other.cur_idx_;
        cur_ts_ = other.cur_ts_;
        cur_list_ = other.cur_list_;
      }

      reference operator*() const {
        return cur_list_->at(cur_idx_);
      }

      iterator& operator++() {
        do {
          advance();
        } while (cur_ts_ <= res_->range_.second &&
                 cur_list_->at(cur_idx_) >= res_->max_rid_);
        return *this;
      }

      iterator operator++(int) {
        iterator it = *this;
        ++(*this);
        return it;
      }

      bool operator==(iterator other) const {
        return (cur_idx_ == other.cur_idx_) && (cur_ts_ == other.cur_ts_);
      }

      bool operator!=(iterator other) const {
        return !(*this == other);
      }

     private:
      inline void advance() {
        if (cur_ts_ == res_->range_.second + 1)
          return;

        cur_idx_++;
        if (cur_list_ == NULL || static_cast<uint64_t>(cur_idx_) == cur_list_->size()) {
          cur_idx_ = 0;
          cur_list_ = NULL;
          while (cur_list_ == NULL && cur_ts_ <= res_->range_.second) {
            char_index* c;
            if ((c = res_->index_->at(++cur_ts_)) != NULL)
              cur_list_ = c->at(res_->char_id_);
          }
        }
      }

      int64_t cur_idx_;
      uint64_t cur_ts_;
      slog::entry_list* cur_list_;

      const result* res_;
    };

    result(const uint64_t max_rid, const uint32_t char_id,
           const time_range range, const time_char_index* index)
      : max_rid_(max_rid), char_id_(char_id), range_(range), index_(index) {}

    iterator begin() {
      return iterator(this);
    }

    iterator end() {
      return iterator(0, range_.second + 1);
    }

    size_t size() {
      size_t cnt = 0;
      for (iterator it = begin(); it != end(); it++)
        cnt++;
      return cnt;
    }

   private:
    const uint64_t max_rid_;
    const uint32_t char_id_;
    const time_range range_;
    const time_char_index* index_;
  };

  /**
    * Constructor to initialize complex character index.
    */
  complex_character_index() {
    index_ = new time_char_index();
  }

  char_index* get(uint64_t ts) {
    return index_->get(ts);
  }

  result filter(const uint64_t max_rid, const uint32_t char_id, const time_range range) {
    return result(max_rid, char_id, range, index_);
  }

 private:
  /* Time-based complex character index */
  time_char_index* index_;
};

}

#endif  // COMPLEX_CHARACTER_INDEX_H_