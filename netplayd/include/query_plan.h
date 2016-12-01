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

}

#endif  // QUERY_PLAN_H_