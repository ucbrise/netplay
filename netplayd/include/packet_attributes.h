#ifndef PACKET_ATTRIBUTES_H_
#define PACKET_ATTRIBUTES_H_

#include <cstdint>

#include <rte_mbuf.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_tcp.h>
#include <rte_udp.h>
#include <rte_lpm.h>

namespace netplay {

/* Entire packet header */

struct packet_header {
  typedef void* value_type;

  static inline value_type get(void* pkt) {
    return pkt;
  }
};

/* Ethernet attributes */
struct ether_s_addr {
  typedef struct ether_addr value_type;

  static inline value_type get(void* pkt) {
    struct ether_hdr *eth = (struct ether_hdr *) pkt;
    return eth->s_addr;
  }
};

struct ether_d_addr {
  typedef struct ether_addr value_type;

  static inline value_type get(void* pkt) {
    struct ether_hdr *eth = (struct ether_hdr *) pkt;
    return eth->d_addr;
  }
};

struct ether_type {
  typedef uint16_t value_type;

  static inline value_type get(void* pkt) {
    struct ether_hdr *eth = (struct ether_hdr *) pkt;
    return eth->ether_type;
  }
};

/* IPv4 attributes */
struct ipv4_version_ihl {
  typedef uint8_t value_type;

  static inline value_type get(void* pkt) {
    struct ether_hdr *eth = (struct ether_hdr *) pkt;
    struct ipv4_hdr *ip = (struct ipv4_hdr *) (eth + 1);
    return ip->version_ihl;
  }
};

struct ipv4_tos {
  typedef uint8_t value_type;

  static inline value_type get(void* pkt) {
    struct ether_hdr *eth = (struct ether_hdr *) pkt;
    struct ipv4_hdr *ip = (struct ipv4_hdr *) (eth + 1);
    return ip->type_of_service;
  }
};

struct ipv4_total_length {
  typedef uint16_t value_type;

  static inline value_type get(void* pkt) {
    struct ether_hdr *eth = (struct ether_hdr *) pkt;
    struct ipv4_hdr *ip = (struct ipv4_hdr *) (eth + 1);
    return ip->total_length;
  }
};

struct ipv4_packet_id {
  typedef uint16_t value_type;

  static inline value_type get(void* pkt) {
    struct ether_hdr *eth = (struct ether_hdr *) pkt;
    struct ipv4_hdr *ip = (struct ipv4_hdr *) (eth + 1);
    return ip->packet_id; 
  }
};

struct ipv4_fragment_offset {
  typedef uint16_t value_type;

  static inline value_type get(void* pkt) {
    struct ether_hdr *eth = (struct ether_hdr *) pkt;
    struct ipv4_hdr *ip = (struct ipv4_hdr *) (eth + 1);
    return ip->fragment_offset; 
  }
};

struct ipv4_ttl {
  typedef uint8_t value_type;

  static inline value_type get(void* pkt) {
    struct ether_hdr *eth = (struct ether_hdr *) pkt;
    struct ipv4_hdr *ip = (struct ipv4_hdr *) (eth + 1);
    return ip->time_to_live; 
  }
};

struct ipv4_next_proto_id {
  typedef uint8_t value_type;

  static inline value_type get(void* pkt) {
    struct ether_hdr *eth = (struct ether_hdr *) pkt;
    struct ipv4_hdr *ip = (struct ipv4_hdr *) (eth + 1);
    return ip->next_proto_id; 
  }
};

struct ipv4_hdr_checksum {
  typedef uint16_t value_type;

  static inline value_type get(void* pkt) {
    struct ether_hdr *eth = (struct ether_hdr *) pkt;
    struct ipv4_hdr *ip = (struct ipv4_hdr *) (eth + 1);
    return ip->hdr_checksum; 
  }
};

struct ipv4_src_addr {
  typedef uint32_t value_type;

  static inline value_type get(void* pkt) {
    struct ether_hdr *eth = (struct ether_hdr *) pkt;
    struct ipv4_hdr *ip = (struct ipv4_hdr *) (eth + 1);
    return ip->src_addr; 
  }
};

struct ipv4_dst_addr {
  typedef uint32_t value_type;

  static inline value_type get(void* pkt) {
    struct ether_hdr *eth = (struct ether_hdr *) pkt;
    struct ipv4_hdr *ip = (struct ipv4_hdr *) (eth + 1);
    return ip->dst_addr; 
  }
};

/* TCP attributes */
struct tcp_src_port {
  typedef uint16_t value_type;

  static inline value_type get(void* pkt) {
    struct ether_hdr *eth = (struct ether_hdr *) pkt;
    struct ipv4_hdr *ip = (struct ipv4_hdr *) (eth + 1);
    struct tcp_hdr *tcp = (struct tcp_hdr *) (ip + 1);
    return tcp->src_port; 
  }
};

struct tcp_dst_port {
  typedef uint16_t value_type;

  static inline value_type get(void* pkt) {
    struct ether_hdr *eth = (struct ether_hdr *) pkt;
    struct ipv4_hdr *ip = (struct ipv4_hdr *) (eth + 1);
    struct tcp_hdr *tcp = (struct tcp_hdr *) (ip + 1);
    return tcp->dst_port; 
  }
};

struct tcp_sent_seq {
  typedef uint32_t value_type;

  static inline value_type get(void* pkt) {
    struct ether_hdr *eth = (struct ether_hdr *) pkt;
    struct ipv4_hdr *ip = (struct ipv4_hdr *) (eth + 1);
    struct tcp_hdr *tcp = (struct tcp_hdr *) (ip + 1);
    return tcp->sent_seq; 
  }
};

struct tcp_recv_ack {
  typedef uint32_t value_type;

  static inline value_type get(void* pkt) {
    struct ether_hdr *eth = (struct ether_hdr *) pkt;
    struct ipv4_hdr *ip = (struct ipv4_hdr *) (eth + 1);
    struct tcp_hdr *tcp = (struct tcp_hdr *) (ip + 1);
    return tcp->recv_ack; 
  }
};

struct tcp_data_off {
  typedef uint8_t value_type;

  static inline value_type get(void* pkt) {
    struct ether_hdr *eth = (struct ether_hdr *) pkt;
    struct ipv4_hdr *ip = (struct ipv4_hdr *) (eth + 1);
    struct tcp_hdr *tcp = (struct tcp_hdr *) (ip + 1);
    return tcp->data_off; 
  }
};

struct tcp_flags {
  typedef uint8_t value_type;

  static inline value_type get(void* pkt) {
    struct ether_hdr *eth = (struct ether_hdr *) pkt;
    struct ipv4_hdr *ip = (struct ipv4_hdr *) (eth + 1);
    struct tcp_hdr *tcp = (struct tcp_hdr *) (ip + 1);
    return tcp->tcp_flags; 
  }
};

struct tcp_rx_win {
  typedef uint16_t value_type;

  static inline value_type get(void* pkt) {
    struct ether_hdr *eth = (struct ether_hdr *) pkt;
    struct ipv4_hdr *ip = (struct ipv4_hdr *) (eth + 1);
    struct tcp_hdr *tcp = (struct tcp_hdr *) (ip + 1);
    return tcp->rx_win;
  }
};

struct tcp_checksum {
  typedef uint16_t value_type;

  static inline value_type get(void* pkt) {
    struct ether_hdr *eth = (struct ether_hdr *) pkt;
    struct ipv4_hdr *ip = (struct ipv4_hdr *) (eth + 1);
    struct tcp_hdr *tcp = (struct tcp_hdr *) (ip + 1);
    return tcp->cksum;
  }
};

struct tcp_urp {
  typedef uint16_t value_type;

  static inline value_type get(void* pkt) {
    struct ether_hdr *eth = (struct ether_hdr *) pkt;
    struct ipv4_hdr *ip = (struct ipv4_hdr *) (eth + 1);
    struct tcp_hdr *tcp = (struct tcp_hdr *) (ip + 1);
    return tcp->tcp_urp; 
  }
};

/* UDP attributes */
struct udp_src_port {
  typedef uint16_t value_type;

  static inline value_type get(void* pkt) {
    struct ether_hdr *eth = (struct ether_hdr *) pkt;
    struct ipv4_hdr *ip = (struct ipv4_hdr *) (eth + 1);
    struct udp_hdr *udp = (struct udp_hdr *) (ip + 1);
    return udp->src_port;
  }
};

struct udp_dst_port {
  typedef uint16_t value_type;

  static inline value_type get(void* pkt) {
    struct ether_hdr *eth = (struct ether_hdr *) pkt;
    struct ipv4_hdr *ip = (struct ipv4_hdr *) (eth + 1);
    struct udp_hdr *udp = (struct udp_hdr *) (ip + 1);
    return udp->dst_port;
  }
};

struct udp_dgram_len {
  typedef uint16_t value_type;

  static inline value_type get(void* pkt) {
    struct ether_hdr *eth = (struct ether_hdr *) pkt;
    struct ipv4_hdr *ip = (struct ipv4_hdr *) (eth + 1);
    struct udp_hdr *udp = (struct udp_hdr *) (ip + 1);
    return udp->dgram_len;
  }
};

struct udp_dgram_checksum {
  typedef uint16_t value_type;

  static inline value_type get(void* pkt) {
    struct ether_hdr *eth = (struct ether_hdr *) pkt;
    struct ipv4_hdr *ip = (struct ipv4_hdr *) (eth + 1);
    struct udp_hdr *udp = (struct udp_hdr *) (ip + 1);
    return udp->dgram_cksum;
  }
};


}

#endif  // PACKET_ATTRIBUTES_H_