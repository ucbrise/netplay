#ifndef QUERY_UTILS_H_
#define QUERY_UTILS_H_

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

  static slog::filter_query expression_to_filter_query(packet_store::handle* h, const std::string& exp) {
    now = std::time(NULL);
    slog::filter_query query;
    assert(h != NULL);
    assert(exp.size() != 0);
    return query;
  }

 private:
  static slog::basic_filter predicate_to_basic_filter(packet_store::handle* h,
      predicate* p) {

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
    if (op == "in") {
      auto range = ip_range(ip_string);
      return slog::basic_filter(index_id, range.first, range.second);
    } else if (op == "!in") {
      auto range = ip_range(ip_string);
      return slog::basic_filter(index_id, range.first, range.second);
    } else if (op == "==") {
      uint32_t ip = ip_string_to_uint32(ip_string.c_str());
      return slog::basic_filter(index_id, ip, ip);
    } else if (op == "!=") {
      uint32_t ip = ip_string_to_uint32(ip_string.c_str());
      return slog::basic_filter(index_id, ip, ip);
    } else {
      throw parse_exception("IP ranges are specified with \'in\' operator and prefix notation");
    }
  }

  static std::pair<uint64_t, uint64_t> ip_range(const std::string& ip_string) {
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

    return std::pair<uint64_t, uint64_t>(ip & prefix_mask[prefix],
                                         ip & prefix_mask[32]);
  }

  static uint32_t ip_string_to_uint32(const char* ip_string) {
    unsigned char ipbytes[4];
    sscanf(ip_string, "%hhu.%hhu.%hhu.%hhu", &ipbytes[3], &ipbytes[2], &ipbytes[1], &ipbytes[0]);
    return ipbytes[0] | ipbytes[1] << 8 | ipbytes[2] << 16 | ipbytes[3] << 24;
  }

  static slog::basic_filter port_filter(uint32_t index_id, const std::string& op,
                                        const std::string& port_string) {
    uint16_t port = 0;
    try {
      port = std::stoi(port_string);
    } catch (std::exception& e) {
      throw parse_exception("Malformed port string: " + port_string);
    }

    if (op == "==") {
      return slog::basic_filter(index_id, port, port);
    } else if (op == "!=") {
      return slog::basic_filter(index_id, port, port);
    } else if (op == "<") {
      return slog::basic_filter(index_id, 0, port - 1);
    } else if (op == "<=") {
      return slog::basic_filter(index_id, 0, port);
    } else if (op == ">") {
      return slog::basic_filter(index_id, port + 1, UINT16_MAX);
    } else if (op == ">=") {
      return slog::basic_filter(index_id, port, UINT16_MAX);
    } else {
      throw parse_exception("Port ranges are expressed with <,>,<=,>= operators");
    }
  }

  static slog::basic_filter time_filter(uint32_t index_id, const std::string& op, 
    const std::string& time_string) {
    size_t loc = time_string.find("now");
    uint32_t time = 0;
    if (loc != std::string::npos) {
      if (loc != 0)
        throw parse_exception("Malformed time value; format: now[-value]");

      if (time_string.length() == 3) {
        time = now;
        
        if (op.find(">") != std::string::npos)
          throw parse_exception("Cannot see into the future");
      } else if (time_string[4] == '-') {
        try {
          uint32_t secs = std::stoi(time_string.substr(5));
          time = now - secs;
        } catch (std::exception& e) {
          throw parse_exception("Malformed time value; format: now[-value]");  
        }
      } else {
        throw parse_exception("Malformed time value; format: now[-value]");
      }
    } else {
      try {
        time = std::stol(time_string);
      } catch (std::exception& e) {
        throw parse_exception("Malformed time value; format: now[-value]");
      }
    }

    if (op == "==") {
      return slog::basic_filter(index_id, time, time);
    } else if (op == "!=") {
      return slog::basic_filter(index_id, time, time);
    } else if (op == "<") {
      return slog::basic_filter(index_id, 0, time - 1);
    } else if (op == "<=") {
      return slog::basic_filter(index_id, 0, time);
    } else if (op == ">") {
      return slog::basic_filter(index_id, time - 1, now);
    } else if (op == ">=") {
      return slog::basic_filter(index_id, time, now);
    }
  }
};

}

#endif // QUERY_UTILS_H_