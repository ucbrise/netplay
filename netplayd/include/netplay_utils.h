#ifndef NETPLAY_UTILS_H_
#define NETPLAY_UTILS_H_

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

class netplay_utils {
 public:

  static index_filter build_index_filter(const packet_store::handle* h,
                                         const predicate* p, const uint32_t now) {
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
      return time_filter(h->timestamp_idx(), p->op, p->value, now);
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
                                  const std::string & time_string, const uint32_t now) {
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

};

#endif  // NETPLAY_UTILS_H_