#ifndef SLOG_FILTERITERATOR_H_
#define SLOG_FILTERITERATOR_H_

#include <cstdint>
#include <iterator>

#include "offsetlog.h"

namespace slog {

typedef std::iterator<std::input_iterator_tag, uint64_t, uint64_t, const uint64_t*, uint64_t> __input_iterator;

template<typename index_type>
class filter_result {
 public:
  class filter_iterator : public __input_iterator {
    typedef uint64_t value_type;
    typedef uint64_t difference_type;
    typedef const uint64_t* pointer;
    typedef uint64_t reference;
   public:
    filter_iterator(filter_result *res) {
      res_ = res;

      cur_tok_ = res_->tok_min_;
      cur_entry_list_ = res_->index_->get(cur_tok_);
      cur_idx_ = -1;

      advance();
    }

    filter_iterator(uint64_t tok, int64_t idx) {
      res_ = NULL;
      cur_entry_list_ = NULL;

      cur_tok_ = tok;
      cur_idx_ = idx;
    }

    reference operator*() const {
      return cur_entry_list_->get(cur_idx_);
    }

    filter_iterator& operator++() {
      while (cur_tok_ != res_->tok_max_ + 1 &&
             !res_->olog_->is_valid(cur_entry_list_->get(cur_idx_), res_->max_rid_))
        advance();
      return *this;
    }

    filter_iterator operator++(int) {
      filter_iterator it = *this;
      ++(*this);
      return it;
    }

    bool operator==(filter_iterator other) const {
      return (cur_tok_ == other.cur_tok_) && (cur_idx_ == other.cur_idx_);
    }

    bool operator!=(filter_iterator other) const {
      return !(*this == other);
    }

   private:
    void advance() {
      if (cur_tok_ == res_->tok_max_ + 1)
        return;
      cur_idx_++;
      if (cur_entry_list_ == NULL || cur_idx_ == cur_entry_list_->size()) {
        cur_idx_ = 0;
        while ((cur_entry_list_ = res_->index_->get(++cur_tok_)) == NULL
               && cur_tok_ <= res_->tok_max_);
      }
    }

    entry_list* cur_entry_list_;
    uint64_t cur_tok_;
    int64_t cur_idx_;
    filter_result *res_;
  };

  filter_result(const offsetlog* olog, const index_type* index, const uint64_t tok_min,
                const uint64_t tok_max, const uint64_t max_rid) {
    olog_ = olog;
    index_ = index;
    tok_min_ = tok_min;
    tok_max_ = tok_max;
    max_rid_ = max_rid;
  }

  filter_iterator begin() {
    return filter_iterator(this);
  }

  filter_iterator end() {
    return filter_iterator(tok_max_, 0);
  }

 private:
  const index_type* index_;
  const offsetlog* olog_;
  uint64_t tok_min_;
  uint64_t tok_max_;
  uint64_t max_rid_;
};

}

#endif  // SLOG_FILTERITERATOR_H_