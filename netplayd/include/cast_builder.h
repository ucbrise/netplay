#ifndef CAST_BUILDER_H_
#define CAST_BUILDER_H_

#include "aggregates.h"
#include "expression.h"
#include "packet_attributes.h"
#include "query_plan.h"
#include "query_planner.h"

namespace netplay {

class cast {
 public:
  cast(expression* exp, packet_store* store) {
    handle_ = store->get_handle();
    plan_ = query_planner::plan(handle_, exp);
  }

  ~cast() {
    delete handle_;
  }

  template<typename aggregate_type>
  typename aggregate_type::result_type execute() {
    return handle_->execute_cast<aggregate_type>(plan_);
  }

 private:
  query_plan plan_;
  packet_store::handle* handle_;
};

class cast_builder {
 public:
  cast_builder(packet_store* store, const std::string& exp) {
    store_ = store;
    handle_ = store->get_handle();
    parser p(exp);
    exp_ = p.parse();
  }

  ~cast_builder() {
    free_expression(exp_);
    delete handle_;
  }

  cast build() {
    return cast(exp_, store_);
  }

 private:
  packet_store* store_;
  packet_store::handle* handle_;
  expression* exp_;
};

}

#endif  // CAST_BUILDER_H_