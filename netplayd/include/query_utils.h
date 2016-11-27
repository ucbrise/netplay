#ifndef QUERY_UTILS_H_
#define QUERY_UTILS_H_

#include <inttypes.h>

#include <ctime>

#include "filterops.h"
#include "expression.h"
#include "query_parser.h"

namespace netplay {

static const uint32_t prefix_mask[33] = {
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

class query_utils {
 public:
  static uint32_t now;

  static slog::filter_query expression_to_filter_query(packet_store::handle* h,
      const std::string& exp) {
    now = std::time(NULL);
    slog::filter_query query;
    fprintf(stderr, "creating parser...\n");
    parser p(exp);
    fprintf(stderr, "about to parse...\n");
    expression* e = p.parse();
    fprintf(stderr, "parsing done: ");
    print_expression(e);
    fprintf(stderr, "\n");

    if (e->type == expression_type::PREDICATE) {
      fprintf(stderr, "predicate type parsing...\n");
      slog::filter_conjunction c;
      c.push_back(predicate_to_basic_filter(h, (predicate*) e));
      query.push_back(c);
    } else if (e->type == expression_type::AND) {
      fprintf(stderr, "conjunction type parsing...\n");
      slog::filter_conjunction conj;
      conjunction* c = (conjunction*) e;
      for (expression* child : c->children) {
        if (child->type != expression_type::PREDICATE) {
          free_expression(e);
          throw parse_exception("Filter expression not in DNF");
        }
        conj.push_back(predicate_to_basic_filter(h, (predicate*) child));
      }
      query.push_back(conj);
    } else if (e->type == expression_type::OR) {
      fprintf(stderr, "disjunction type parsing...\n");
      disjunction *d = (disjunction*) e;
      for (expression* dchild : d->children) {
        if (dchild->type != expression_type::AND) {
          free_expression(e);
          throw parse_exception("Filter expression not in DNF");
        }
        slog::filter_conjunction conj;
        conjunction* c = (conjunction*) dchild;
        for (expression* cchild : c->children) {
          if (cchild->type != expression_type::PREDICATE) {
            free_expression(e);
            throw parse_exception("Filter expression not in DNF");
          }
          conj.push_back(predicate_to_basic_filter(h, (predicate*) cchild));
        }
        query.push_back(conj);
      }
    }
    
    free_expression(e);

    // TODO: reduce conjunctions

    return query;
  }

 private:
  static slog::basic_filter predicate_to_basic_filter(packet_store::handle* h,
      predicate* p) {
    fprintf(stderr, "converting predicate to filter: ");
    print_expression(p);
    fprintf(stderr, "\n");
    // TODO: replace this with a map lookup
    if (p->attr == "src_ip") {
      return ip_filter(h->srcip_idx(), p->op, p->value);
    } else if (p->attr == "dst_ip") {
      return ip_filter(h->dstip_idx(), p->op, p->value);
    } else if (p->attr == "src_port") {
      return port_filter(h->srcport_idx(), p->op, p->value);
    } else if (p->attr == "dst_port") {
      return port_filter(h->dstport_idx(), p->op, p->value);
    } else if (p->attr == "timestamp") {
      return time_filter(h->timestamp_idx(), p->op, p->value);
    } else {
      throw parse_exception("Invaild attribute: " + p->attr);
    }
  }

  static slog::basic_filter ip_filter(uint32_t index_id, const std::string& op,
                                      const std::string& ip_string) {
    fprintf(stderr, "ip filter: ");
    if (op == "in") {
      auto range = ip_range(ip_string);
      fprintf(stderr, "range: %" PRIu32 ", %" PRIu32 "\n", range.first, range.second);
      return slog::basic_filter(index_id, range.first, range.second);
    } else if (op == "!in") {
      auto range = ip_range(ip_string);
      fprintf(stderr, "range: %" PRIu32 ", %" PRIu32 "\n", range.first, range.second);
      return slog::basic_filter(index_id, range.first, range.second);
    } else if (op == "==") {
      uint32_t ip = ip_string_to_uint32(ip_string.c_str());
      fprintf(stderr, "range: %" PRIu32 ", %" PRIu32 "\n", ip, ip);
      return slog::basic_filter(index_id, ip, ip);
    } else if (op == "!=") {
      uint32_t ip = ip_string_to_uint32(ip_string.c_str());
      fprintf(stderr, "range: %" PRIu32 ", %" PRIu32 "\n", ip, ip);
      return slog::basic_filter(index_id, ip, ip);
    } else {
      throw parse_exception("Specify IP ranges with prefix notation");
    }
  }

  static std::pair<uint32_t, uint32_t> ip_range(const std::string& ip_string) {
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

    return std::pair<uint32_t, uint32_t>(ip & prefix_mask[prefix],
                                         ip | (~prefix_mask[32 - prefix]));
  }

  static uint32_t ip_string_to_uint32(const char* ip) {
    unsigned char tmp[4];
    sscanf(ip, "%hhu.%hhu.%hhu.%hhu", &tmp[3], &tmp[2], &tmp[1], &tmp[0]);
    return tmp[0] | tmp[1] << 8 | tmp[2] << 16 | tmp[3] << 24;
  }

  static slog::basic_filter port_filter(uint32_t index_id, const std::string& op,
                                        const std::string& port_string) {
    uint16_t port = 0;
    fprintf(stderr, "port filter: ");
    try {
      port = std::stoi(port_string);
    } catch (std::exception& e) {
      throw parse_exception("Malformed port string: " + port_string);
    }

    if (op == "==") {
      fprintf(stderr, "range: %" PRIu16 ", %" PRIu16 "\n", port, port);
      return slog::basic_filter(index_id, port, port);
    } else if (op == "!=") {
      fprintf(stderr, "range: %" PRIu16 ", %" PRIu16 "\n", port, port);
      return slog::basic_filter(index_id, port, port);
    } else if (op == "<") {
      fprintf(stderr, "range: %" PRIu16 ", %" PRIu16 "\n", 0, port - 1);
      return slog::basic_filter(index_id, 0, port - 1);
    } else if (op == "<=") {
      fprintf(stderr, "range: %" PRIu16 ", %" PRIu16 "\n", 0, port);
      return slog::basic_filter(index_id, 0, port);
    } else if (op == ">") {
      fprintf(stderr, "range: %" PRIu16 ", %" PRIu16 "\n", port + 1, UINT16_MAX);
      return slog::basic_filter(index_id, port + 1, UINT16_MAX);
    } else if (op == ">=") {
      fprintf(stderr, "range: %" PRIu16 ", %" PRIu16 "\n", port, UINT16_MAX);
      return slog::basic_filter(index_id, port, UINT16_MAX);
    } else {
      throw parse_exception("Specify port ranges with <,>,<=,>= operators");
    }
  }

  static slog::basic_filter time_filter(uint32_t index_id, const std::string& op,
                                        const std::string& time_string) {
    fprintf(stderr, "time filter: ");
    size_t loc = time_string.find("now");
    uint32_t time = 0;
    if (loc != std::string::npos) {
      fprintf(stderr, "has now reference; ");
      if (loc != 0)
        throw parse_exception("Malformed relative time value; format: now[-value]");

      if (time_string.length() == 3) {
        fprintf(stderr, "just now; ");
        time = now;

        if (op.find(">") != std::string::npos)
          throw parse_exception("Cannot see into the future");
      } else {
        fprintf(stderr, "checking if relative: ");
        loc = time_string.find("-");
        fprintf(stderr, "loc: %zu; ", loc);
        if (loc != std::string::npos && loc == 3) {
          fprintf(stderr, "relative to now; ");
          try {
            std::string secs_str = time_string.substr(4);
            fprintf(stderr, "secs = %s ", secs_str.c_str());
            fflush(stderr);
            uint32_t secs = std::stoi(secs_str);
            time = now - secs;
          } catch (std::exception& e) {
            throw parse_exception("Malformed relative time value; format: now[-value]");
          }
        } else {
          throw parse_exception("Malformed relative time value; format: now[-value]");
        }
        time = now;
      }
    } else {
      fprintf(stderr, "no now reference; ");
      try {
        time = std::stol(time_string);
      } catch (std::exception& e) {
        throw parse_exception("Malformed relative time value; format: now[-value]");
      }
    }

    if (op == "==") {
      fprintf(stderr, "range: %" PRIu32 ", %" PRIu32 "\n", time, time);
      return slog::basic_filter(index_id, time, time);
    } else if (op == "!=") {
      fprintf(stderr, "range: %" PRIu32 ", %" PRIu32 "\n", time, time);
      return slog::basic_filter(index_id, time, time);
    } else if (op == "<") {
      fprintf(stderr, "range: %" PRIu32 ", %" PRIu32 "\n", 0, time - 1);
      return slog::basic_filter(index_id, 0, time - 1);
    } else if (op == "<=") {
      fprintf(stderr, "range: %" PRIu32 ", %" PRIu32 "\n", 0, time);
      return slog::basic_filter(index_id, 0, time);
    } else if (op == ">") {
      fprintf(stderr, "range: %" PRIu32 ", %" PRIu32 "\n", time - 1, now);
      return slog::basic_filter(index_id, time - 1, now);
    } else if (op == ">=") {
      fprintf(stderr, "range: %" PRIu32 ", %" PRIu32 "\n", time, now);
      return slog::basic_filter(index_id, time, now);
    } else {
      throw parse_exception("Specify time ranges with <,>,<=,>= operators");
    }
  }
};

}

#endif // QUERY_UTILS_H_