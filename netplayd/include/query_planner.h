#ifndef QUERY_PLANNER_H_
#define QUERY_PLANNER_H_

#include <inttypes.h>

#include <ctime>

#include "packetstore.h"
#include "query_parser.h"
#include "expression.h"
#include "query_plan.h"
#include "netplay_utils.h"

namespace netplay {

class query_planner {
 public:
  typedef std::vector<index_filter> clause;
  typedef clause::iterator clause_iterator;

  static query_plan plan(packet_store::handle* h, expression* e) {
    uint32_t now = std::time(NULL);

    query_plan _plan;

    if (e->type == expression_type::PREDICATE) {
      clause_plan _cplan;
      _cplan.valid = true;
      _cplan.perform_pkt_filter = false;
      _cplan.idx_filter = netplay_utils::build_index_filter(h, (predicate*) e, now);
      _plan.push_back(_cplan);
    } else if (e->type == expression_type::AND) {
      conjunction* c = (conjunction*) e;
      clause _clause;
      for (expression* child : c->children) {
        if (child->type != expression_type::PREDICATE)
          throw parse_exception("Filter expression not in DNF");
        _clause.push_back(netplay_utils::build_index_filter(h, (predicate*) child, now));
      }
      clause_plan _cplan = build_clause_plan(h, _clause);
      if (_cplan.valid)
        _plan.push_back(_cplan);
    } else if (e->type == expression_type::OR) {
      disjunction *d = (disjunction*) e;
      for (expression* dchild : d->children) {
        if (dchild->type != expression_type::AND)
          throw parse_exception("Filter expression not in DNF");
        conjunction* c = (conjunction*) dchild;
        clause _clause;
        for (expression* cchild : c->children) {
          if (cchild->type != expression_type::PREDICATE)
            throw parse_exception("Filter expression not in DNF");
          _clause.push_back(netplay_utils::build_index_filter(h, (predicate*) cchild, now));
        }
        clause_plan _cplan = build_clause_plan(h, _clause);
        if (_cplan.valid)
          _plan.push_back(_cplan);
      }
    }

    return _plan;
  }

 private:
  static index_filter extract_min_filter(const packet_store::handle* h,
                                        clause& clause) {
    clause_iterator min_f;
    uint64_t min_count = UINT64_MAX;

    for (clause_iterator i = clause.begin(); i != clause.end(); i++) {
      uint64_t cnt;
      if ((cnt = h->approx_pkt_count(i->index_id, i->tok_range.first, i->tok_range.second)) < min_count) {
        min_count = cnt;
        min_f = i;
      }
    }
    index_filter f = *min_f;
    clause.erase(min_f);
    return f;
  }

  static clause_plan build_clause_plan(const packet_store::handle* h,
                                       clause& clause) {
    clause_plan _plan;

    // First reduce the clause
    _plan.valid = netplay_utils::reduce_clause(clause);

    if (_plan.valid) {
      /* Get the min cardinality filter */
      _plan.idx_filter = extract_min_filter(h, clause);
      _plan.perform_pkt_filter = !clause.empty();
      _plan.pkt_filter = netplay_utils::build_packet_filter(h, clause);
    }

    return _plan;
  }
};

}

#endif  // QUERY_PLANNER_H_