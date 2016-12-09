#ifndef CHARACTER_BUILDER_H_
#define CHARACTER_BUILDER_H_

#include "packetstore.h"
#include "expression.h"
#include "query_parser.h"
#include "netplay_utils.h"

namespace netplay {

class character_builder {
 public:
  character_builder(packet_store* store, const std::string& exp) {
    handle_ = store->get_handle();
    parser p(exp);
    exp_ = p.parse();
  }

  ~character_builder() {
    free_expression(exp_);
    delete handle_;
  }

  filter_list build() {
    return netplay_utils::build_filter_list(handle_, exp_);
  }

 private:
  expression* exp_;
  packet_store::handle* handle_;
};

}

#endif  // CHARACTER_BUILDER_H_