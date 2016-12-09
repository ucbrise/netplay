#ifndef AGGREGATES_H_
#define AGGREGATES_H_

#include <unordered_set>
#include <type_traits>

namespace netplay {

namespace aggregate {

template<typename T>
struct result_set {
  typedef std::unordered_set<typename T::value_type> result_type;
  typedef T attribute_type;
  typedef std::unordered_set<typename T::value_type> input_type;

  static inline result_type aggregate(const input_type& value) {
    return value;
  }
};

template<typename T>
struct count {
  typedef size_t result_type;
  typedef T attribute_type;

  template<typename R>
  static inline result_type aggregate(const std::unordered_set<R>& value) {
    return value.size();
  }
};

template<typename T>
struct sum {
  typedef typename T::value_type result_type;
  static_assert(std::is_arithmetic<result_type>::value,
                "Result type must be numeric");
  typedef T attribute_type;
  typedef std::unordered_set<typename T::value_type> input_type;

  static inline result_type aggregate(const input_type& value) {
    result_type sum = 0;
    for (const result_type& x : value)
      sum += x;
    return sum;
  }
};

template<typename T>
struct maximum {
  typedef typename T::value_type result_type;
  static_assert(std::is_arithmetic<result_type>::value,
                "Result type must be numeric");
  typedef T attribute_type;
  typedef std::unordered_set<typename T::value_type> input_type;

  static inline result_type aggregate_type(const input_type& value) {
    result_type max = 0;
    for (const result_type& x : value)
      if (x > max) max = x;
    return max;
  }
};

template<typename T>
struct minimum {
  typedef typename T::value_type result_type;
  static_assert(std::is_arithmetic<result_type>::value,
                "Result type must be numeric");
  typedef T attribute_type;
  typedef std::unordered_set<typename T::value_type> input_type;

  static inline result_type aggregate_type(const input_type& value) {
    if (value.is_empty())
      return static_cast<result_type>(-1);
    result_type min = *(value.begin());
    for (const result_type& x : value)
      if (x < min) min = x;
    return min;
  }
};

}

}

#endif  // AGGREGATES_H_