#ifndef QUERY_PLAN_H_
#define QUERY_PLAN_H_

#include "packet_filter.h"

namespace netplay {

struct clause_plan {
  index_filter idx_filter;
  packet_filter pkt_filter;
  bool valid;
  bool perform_pkt_filter;
};

typedef std::vector<clause_plan> query_plan;

void print_query_plan(const query_plan& plan) {
  for (const auto& p : plan) {
    index_filter f = p.idx_filter;
    fprintf(stderr, "idx-filter: (%" PRIu32 ", %" PRIu64 ", %" PRIu64 ")\t",
            f.index_id, f.tok_range.first, f.tok_range.second);
  }
  fprintf(stderr, "\n");
}

}

#endif  // QUERY_PLAN_H_