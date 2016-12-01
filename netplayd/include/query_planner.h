#ifndef QUERY_PLANNER_H_
#define QUERY_PLANNER_H_

#include <inttypes.h>

#include <ctime>

#include "expression.h"
#include "query_plan.h"

namespace netplay {

static const uint32_t ip_prefix_mask[33] = {
  0x00000000U, 0x80000000U, 0xC0000000U,
  0xE0000000U, 0xF0000000U, 0xF8000000U,
  0xFC000000U, 0xFE000000U, 0xFF000000U,
  0xFF800000U, 0xFFC00000U, 0xFFE00000U,
  0xFFF00000U, 0xFFF80000U, 0xFFFC0000U,
  0xFFFE0000U, 0xFFFF0000U, 0xFFFF8000U,
  0xFFFFC000U, 0xFFFFE000U, 0xFFFFF000U,
  0xFFFFF800U, 0xFFFFFC00U, 0xFFFFFE00U,
  0xFFFFFF00U, 0xFFFFFF80U, 0xFFFFFFC0U,
  0xFFFFFFE0U, 0xFFFFFFF0U, 0xFFFFFFF8U,
  0xFFFFFFFCU, 0xFFFFFFFEU, 0xFFFFFFFFU
};

class query_planner {
 public:
  typedef std::vector<index_filter> clause;
  typedef clause::iterator clause_iterator;

  static uint32_t now;

  static query_plan plan(packet_store::handle* h, expression* e) {
    now = std::time(NULL);

    query_plan _plan;

    if (e->type == expression_type::PREDICATE) {
      clause_plan _cplan;
      _cplan.valid = true;
      _cplan.perform_pkt_filter = false;
      _cplan.idx_filter = build_index_filter(h, (predicate*) e);
      _plan.push_back(_cplan);
    } else if (e->type == expression_type::AND) {
      conjunction* c = (conjunction*) e;
      clause _clause;
      for (expression* child : c->children) {
        if (child->type != expression_type::PREDICATE)
          throw parse_exception("Filter expression not in DNF");
        _clause.push_back(build_index_filter(h, (predicate*) child));
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
          _clause.push_back(build_index_filter(h, (predicate*) cchild));
        }
        clause_plan _cplan = build_clause_plan(h, _clause);
        if (_cplan.valid)
          _plan.push_back(_cplan);
      }
    }

    return _plan;
  }

 private:
  static bool reduce_clause(clause& clause) {
    for (clause_iterator i = clause.begin(); i != clause.end();) {
      uint32_t index_id = i->index_id;
      index_filter::range tok_range = i->tok_range;

      for (clause_iterator j = i + 1; j != clause.end(); ) {
        if (index_id == j->index_id) {
          tok_range.first = std::max(tok_range.first, j->tok_range.first);
          tok_range.second = std::min(tok_range.second, j->tok_range.second);
          j = clause.erase(j);
        } else {
          j++;
        }
      }

      if (tok_range.first <= tok_range.end) {
        i->tok_range = tok_range;
        i++;
      } else {
        return false;
      }
    }
    return true;
  }

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

  static packet_filter build_packet_filter(const packet_store::handle* h,
      const clause& clause) {
    packet_filter pf;
    pf.src_addr = packet_filter::range(0, UINT64_MAX);
    pf.dst_addr = packet_filter::range(0, UINT64_MAX);
    pf.src_port = packet_filter::range(0, UINT64_MAX);
    pf.dst_port = packet_filter::range(0, UINT64_MAX);
    pf.timestamp = packet_filter::range(0, UINT64_MAX);

    for (const index_filter& f : clause) {
      if (f.index_id == h->srcip_idx())
        pf.src_addr = f.tok_range;
      else if (f.index_id == h->dstip_idx())
        pf.dst_addr = f.tok_range;
      else if (f.index_id == h->srcport_idx())
        pf.src_port = f.tok_range;
      else if (f.index_id == h->dstport_idx())
        pf.dst_port = f.tok_range;
      else if (f.index_id == h->timestamp_idx())
        pf.timestamp = f.tok_range;
      else
        throw parse_exception("Invalid idx id " + std::to_string(f.index_id));
    }

    return pf;
  }

  static clause_plan build_clause_plan(const packet_store::handle* h,
                                       clause& clause) {
    clause_plan _plan;

    // First reduce the clause
    _plan.valid = reduce_clause(clause);

    if (_plan.valid) {
      /* Get the min cardinality filter */
      _plan.idx_filter = extract_min_filter(h, clause);
      _plan.perform_pkt_filter = !clause.empty();
      _plan.pkt_filter = build_packet_filter(clause);
    }

    return _plan;
  }

  static index_filter build_index_filter(const packet_store::handle* h,
                                         const predicate* p) {
    // TODO: replace this with a map lookup
    if (p->attr == "src_ip")
      return ip_filter(h->srcip_idx(), p->op, p->value);
    else if (p->attr == "dst_ip")
      return ip_filter(h->dstip_idx(), p->op, p->value);
    else if (p->attr == "src_port")
      return port_filter(h->srcport_idx(), p->op, p->value);
    else if (p->attr == "dst_port")
      return port_filter(h->dstport_idx(), p->op, p->value);
    else if (p->attr == "timestamp")
      return time_filter(h->timestamp_idx(), p->op, p->value);
    else
      throw parse_exception("Invaild attribute: " + p->attr);
  }

  static index_filter ip_filter(const uint32_t index_id, const std::string& op,
                                const std::string& ip_string) {
    index_filter f;
    f.index_id = index_id;
    if (op == "in") {
      f.tok_range = ip_range(ip_string);
    } else if (op == "!in") {
      f.tok_range = ip_range(ip_string);
    } else if (op == "==") {
      uint32_t ip = ip_string_to_uint32(ip_string.c_str());
      f.tok_range = index_filter::range(ip, ip);
    } else if (op == "!=") {
      uint32_t ip = ip_string_to_uint32(ip_string.c_str());
      f.tok_range = index_filter::range(ip, ip);
    } else {
      throw parse_exception("Specify IP ranges with prefix notation");
    }
    return f;
  }

  static index_filter::range ip_range(const std::string& ip_string) {
    size_t loc = ip_string.find_first_of('/');
    uint32_t ip = 0;
    uint32_t prefix = 32;

    if (loc != std::string::npos) {
      try {
        prefix = std::stoi(ip_string.substr(loc + 1));
      } catch (std::exception& e) {
        throw parse_exception("Malformed IP range: " + ip_string);
      }
      ip = ip_string_to_uint32(ip_string.substr(0, loc).c_str());
    } else {
      throw parse_exception("Malformed IP range: " + ip_string);
    }

    return index_filter::range(ip & ip_prefix_mask[prefix],
                               ip | (~ip_prefix_mask[32 - prefix]));
  }

  static uint32_t ip_string_to_uint32(const char* ip) {
    unsigned char tmp[4];
    sscanf(ip, "%hhu.%hhu.%hhu.%hhu", &tmp[3], &tmp[2], &tmp[1], &tmp[0]);
    return tmp[0] | tmp[1] << 8 | tmp[2] << 16 | tmp[3] << 24;
  }

  static index_filter port_filter(const uint32_t index_id, const std::string& op,
                                  const std::string & port_string) {
    uint16_t port = 0;
    try {
      port = std::stoi(port_string);
    } catch (std::exception& e) {
      throw parse_exception("Malformed port string: " + port_string);
    }

    index_filter f;
    f.index_id = index_id;

    if (op == "==")
      f.tok_range = index_filter::range(port, port);
    else if (op == "!=")
      f.tok_range = index_filter::range(port, port);
    else if (op == "<")
      f.tok_range = index_filter::range(0, port - 1);
    else if (op == "<=")
      f.tok_range = index_filter::range(0, port);
    else if (op == ">")
      f.tok_range = index_filter::range(port + 1, UINT16_MAX);
    else if (op == ">=")
      f.tok_range = index_filter::range(port, UINT16_MAX);
    else
      throw parse_exception("Specify port ranges with <,>,<=,>= operators");

    return f;
  }

  static index_filter time_filter(const uint32_t index_id, const std::string & op,
                                  const std::string & time_string) {
    size_t loc = time_string.find("now");
    uint32_t time = 0;
    if (loc != std::string::npos) {
      // Time relative to "now"
      if (loc != 0)
        throw parse_exception("Malformed relative time value; format: now[-value]");

      if (time_string.length() == 3) {
        time = now;

        if (op.find(">") != std::string::npos)
          throw parse_exception("Cannot see into the future");
      } else {
        loc = time_string.find("-");
        if (loc != std::string::npos && loc == 3) {
          try {
            time = now - std::stoi(time_string.substr(4));
          } catch (std::exception& e) {
            throw parse_exception("Malformed relative time value; format: now[-value]");
          }
        } else {
          throw parse_exception("Malformed relative time value; format: now[-value]");
        }
      }
    } else {
      // Absolute time
      try {
        time = std::stol(time_string);
      } catch (std::exception& e) {
        throw parse_exception("Malformed relative time value; format: now[-value]");
      }
    }

    index_filter f;
    f.index_id = index_id;

    if (op == "==")
      f.tok_range = index_filter::range(time, time);
    else if (op == "!=")
      f.tok_range = index_filter::range(time, time);
    else if (op == "<")
      f.tok_range = index_filter::range(0, time - 1);
    else if (op == "<=")
      f.tok_range = index_filter::range(0, time);
    else if (op == ">")
      f.tok_range = index_filter::range(time + 1, now);
    else if (op == ">=")
      f.tok_range = index_filter::range(time, now);
    else
      throw parse_exception("Specify time ranges with <,>,<=,>= operators");

    return f;
  }
};

#endif  // QUERY_PLANNER_H_