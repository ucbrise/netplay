#ifndef CHARACTER_BUILDER_H_
#define CHARACTER_BUILDER_H_

#include "packetstore.h"
#include "expression.h"
#include "query_parser.h"
#include "netplay_utils.h"

namespace netplay {

class complex_character {
 public:
  complex_character(packet_store* store, uint32_t id) {
    store_ = store;
    id_ = id;
  }

  complex_character(const complex_character& other) {
    store_ = other.store_;
    id_ = other.id_;
  }

  template<typename aggregate_type>
  typename aggregate_type::result_type execute(const uint64_t ts_beg, const uint64_t ts_end) {
    return store_->query_character<aggregate_type>(id_, ts_beg, ts_end);
  }

 private:
  uint32_t id_;
  packet_store* store_;
};

class character_builder {
 public:
  character_builder(packet_store* store, const std::string& exp) {
    store_ = store;
    parser p(exp);
    exp_ = p.parse();
  }

  ~character_builder() {
    free_expression(exp_);
  }

  complex_character build() {
    packet_store::handle* handle = store_->get_handle();
    auto f = netplay_utils::build_filter_list(handle, exp_);
    delete handle;
    uint32_t id = store_->add_complex_character(f);
    return complex_character(store_, id);
  }

 private:
  expression* exp_;
  packet_store* store_;
};

}

#endif  // CHARACTER_BUILDER_H_