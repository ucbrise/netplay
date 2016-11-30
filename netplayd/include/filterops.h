#ifndef SLOG_FILTEROPS_H_
#define SLOG_FILTEROPS_H_

#include <inttypes.h>

namespace netplay {

struct basic_filter {
 public:
  basic_filter() {
    index_id_ = UINT32_MAX;
    token_beg_ = UINT64_MAX;
    token_end_ = UINT64_MAX;
  }

  basic_filter(uint32_t index_id, unsigned char* token_beg,
               unsigned char* token_end) {
    index_id_ = index_id;
    token_beg_ = *(uint64_t*) token_beg;
    token_end_ = *(uint64_t*) token_end;
  }

  basic_filter(uint32_t index_id, uint64_t token_beg, uint64_t token_end) {
    index_id_ = index_id;
    token_beg_ = token_beg;
    token_end_ = token_end;
  }

  basic_filter(uint32_t index_id, unsigned char* token)
    : basic_filter(index_id, token, token) {
  }

  basic_filter(uint32_t index_id, uint64_t token)
    : basic_filter(index_id, token, token) {
  }

  basic_filter& operator=(const basic_filter& filter) {
    index_id_ = filter.index_id_;
    token_beg_ = filter.token_beg_;
    token_end_ = filter.token_end_;
    return *this;
  }

  bool operator==(const basic_filter& other) {
    return index_id_ == other.index_id_ && token_beg_ == other.token_beg_ &&
           token_end_ == other.token_end_;
  }

  uint32_t index_id() const {
    return index_id_;
  }

  uint64_t token_beg() const {
    return token_beg_;
  }

  uint64_t token_end() const {
    return token_end_;
  }

  void token_beg(uint64_t val) {
    token_beg_ = val;
  }

  void token_end(uint64_t val) {
    token_end_ = val;
  }

 protected:
  uint32_t index_id_;
  uint64_t token_beg_;
  uint64_t token_end_;
  /* TODO: Add negation */
};

typedef std::vector<basic_filter> filter_conjunction;
typedef std::vector<filter_conjunction> filter_query;

void print_filter_query(const filter_query& query) {
  fprintf(stderr, "OR(");
  for (size_t i = 0; i < query.size(); i++) {
    fprintf(stderr, "AND(");
    filter_conjunction conj = query[i];
    for (size_t j = 0; j < conj.size(); j++) {
      basic_filter f = conj[j];
      fprintf(stderr, "BasicFilter(%" PRIu32 ": %" PRIu64 ", %" PRIu64 ")",
              f.index_id(), f.token_beg(), f.token_end());
      if (j != conj.size() - 1)
        fprintf(stderr, ", ");
    }
    fprintf(stderr, ")");
    if (i != query.size() - 1)
      fprintf(stderr, ", ");
  }
  fprintf(stderr, ")");
}

}

#endif /* SLOG_FILTEROPS_H_ */
