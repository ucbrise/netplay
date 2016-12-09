#ifndef AGGREGATES_H_
#define AGGREGATES_H_

#include <unordered_set>
#include <type_traits>

#include "complex_character_index.h"
#include "datalog.h"
#include "offsetlog.h"

namespace netplay {

namespace aggregate {

template<typename T>
struct result_set {
  typedef std::unordered_set<typename T::value_type> result_type;
  typedef T attribute_type;

  template<typename container_type>
  static inline result_type aggregate(container_type& container) {
    return container;
  }
};

template<typename T>
struct count {
  typedef size_t result_type;
  typedef T attribute_type;

  template<typename container_type>
  static inline result_type aggregate(container_type& container) {
    return container.size();
  }
};

template<typename T>
struct sum {
  typedef typename T::value_type result_type;
  static_assert(std::is_arithmetic<result_type>::value,
                "Result type must be numeric");
  typedef T attribute_type;

  template<typename container_type>
  static inline result_type aggregate(container_type& container) {
    result_type s = 0;
    for (result_type x : container)
      s += x;
    return s;
  }
};

template<typename T>
struct maximum {
  typedef typename T::value_type result_type;
  static_assert(std::is_arithmetic<result_type>::value,
                "Result type must be numeric");
  typedef T attribute_type;

  template<typename container_type>
  static inline result_type aggregate_type(container_type& value) {
    result_type m = std::numeric_limits<result_type>::min();
    for (result_type x : value)
      if (x > m) m = x;
    return m;
  }
};

template<typename T>
struct minimum {
  typedef typename T::value_type result_type;
  static_assert(std::is_arithmetic<result_type>::value,
                "Result type must be numeric");
  typedef T attribute_type;

  template<typename container_type>
  static inline result_type aggregate(container_type& container) {
    result_type m = std::numeric_limits<result_type>::max();
    for (result_type x : container)
      if (x < m) m = x;
    return m;
  }
};

}

}

#endif  // AGGREGATES_H_