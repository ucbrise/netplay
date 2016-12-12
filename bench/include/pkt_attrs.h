#ifndef PKT_ATTRS_H_
#define PKT_ATTRS_H_

struct pkt_attrs {
  uint32_t sip;
  uint32_t dip;
  uint16_t sport;
  uint16_t dport;
};

#endif  // PKT_ATTRS_H_