#ifndef TIEREDINDEX_H_
#define TIEREDINDEX_H_

#include <atomic>
#include <array>

#include "entrylist.h"

namespace slog {
template<typename T, size_t SIZE = 65536>
class indexlet {
 public:
  typedef std::atomic<T*> atomic_ref;

  indexlet() {
    for (uint32_t i = 0; i < SIZE; i++) {
      idx_[i].store(NULL, std::memory_order_release);
    }
  }

  ~indexlet() {
    for (uint32_t i = 0; i < SIZE; i++) {
      delete idx_[i].load(std::memory_order_acquire);
    }
  }

  T* operator[](const uint32_t i) {
    if (idx_[i].load(std::memory_order_acquire) == NULL) {
      T* item = new T();
      T* null_ptr = NULL;

      // Only one thread will be successful in replacing the NULL reference with newly
      // allocated item.
      if (!std::atomic_compare_exchange_strong_explicit(
          &idx_[i], &null_ptr, item, std::memory_order_release,
          std::memory_order_acquire)) {
        // All other threads will deallocate the newly allocated item.
        delete item;
      }
    }

    return idx_[i].load(std::memory_order_acquire);
  }

  T* at(const uint32_t i) const {
    return idx_[i].load(std::memory_order_acquire);
  }

  size_t size() {
    return SIZE;
  }

  size_t storage_size() {
    size_t tot_size = SIZE * sizeof(atomic_ref);
    for (uint32_t i = 0; i < SIZE; i++) {
      if (idx_[i].load(std::memory_order_acquire) != NULL) {
        tot_size += idx_[i].load(std::memory_order_acquire)->storage_size();
      }
    }
    return tot_size;
  }

 private:
  std::array<atomic_ref, SIZE> idx_;
};

template<size_t SIZE, typename value_type = entry_list>
class __index_depth1 {
 public:
  value_type* get(const uint64_t key) {
    return idx_[key];
  }

  value_type* operator[](const uint64_t i) {
    return idx_[i];
  }

  value_type* at(const uint64_t key) const {
    return idx_.at(key);
  }  

  void add_entry(const uint64_t key, const uint64_t val) {
    value_type* list = get(key);
    list->push_back(val);
  }

  size_t max_size() {
    return SIZE;
  }

  size_t storage_size() {
    return idx_.storage_size();
  }

 protected:
  indexlet<value_type, SIZE> idx_;
};

template<size_t SIZE1, size_t SIZE2, typename value_type = entry_list>
class __index_depth2 {
 public:
  value_type* get(const uint64_t key) {
    __index_depth1 <SIZE2, value_type>* ilet = idx_[key / SIZE2];
    return ilet->get(key % SIZE2);
  }

  __index_depth1 <SIZE2, value_type>* operator[](const uint64_t i) {
    return idx_[i];
  }

  value_type* at(const uint64_t key) const {
    __index_depth1 <SIZE2, value_type>* ilet = idx_.at(key / SIZE2);
    if (ilet)
      return ilet->at(key % SIZE2);
    return NULL;
  }

  void add_entry(const uint64_t key, const uint64_t val) {
    value_type* list = get(key);
    list->push_back(val);
  }

  size_t max_size() {
    return SIZE1 * SIZE2;
  }

  size_t storage_size() {
    return idx_.storage_size();
  }

 private:
  indexlet<__index_depth1 <SIZE2, value_type>, SIZE1> idx_;
};

template<size_t SIZE1, size_t SIZE2, size_t SIZE3, typename value_type = entry_list>
class __index_depth3 {
 public:
  value_type* get(const uint64_t key) {
    __index_depth2 <SIZE2, SIZE3, value_type>* ilet = idx_[key / (SIZE2 * SIZE3)];
    return ilet->get(key % (SIZE2 * SIZE3));
  }

  __index_depth2 <SIZE2, SIZE3, value_type>* operator[](const uint64_t i) {
    return idx_[i];
  }

  value_type* at(const uint64_t key) const {
    __index_depth2 <SIZE2, SIZE3, value_type>* ilet = idx_.at(key / (SIZE2 * SIZE3));
    if (ilet)
      return ilet->at(key % (SIZE2 * SIZE3));
    return NULL;
  }

  void add_entry(const uint64_t key, const uint64_t val) {
    value_type* list = get(key);
    list->push_back(val);
  }

  size_t max_size() {
    return SIZE1 * SIZE2 * SIZE3;
  }

  size_t storage_size() {
    return idx_.storage_size();
  }

 private:
  indexlet<__index_depth2 <SIZE2, SIZE3, value_type>, SIZE1> idx_;
};

template<size_t SIZE1, size_t SIZE2, size_t SIZE3, size_t SIZE4, typename value_type = entry_list>
class __index_depth4 {
 public:
  value_type* get(const uint64_t key) {
    __index_depth3 <SIZE2, SIZE3, SIZE4, value_type>* ilet = idx_[key / (SIZE2 * SIZE3 * SIZE4)];
    return ilet->get(key % (SIZE2 * SIZE3 * SIZE4));
  }

  __index_depth3 <SIZE2, SIZE3, SIZE4, value_type>* operator[](const uint64_t i) {
    return idx_[i];
  }

  value_type* at(const uint64_t key) const {
    __index_depth3 <SIZE2, SIZE3, SIZE4, value_type>* ilet = idx_.at(key / (SIZE2 * SIZE3 * SIZE4));
    if (ilet)
      return ilet->at(key % (SIZE2 * SIZE3 * SIZE4));
    return NULL;
  }

  void add_entry(const uint64_t key, uint64_t val) {
    value_type* list = get(key);
    list->push_back(val);
  }

  size_t max_size() {
    return SIZE1 * SIZE2 * SIZE3;
  }

  size_t storage_size() {
    return idx_.storage_size();
  }

 private:
  indexlet<__index_depth3 <SIZE2, SIZE3, SIZE4, value_type>, SIZE1> idx_;
};

typedef __index_depth1 <256> __index1;
typedef __index_depth1 <65536> __index2;
typedef __index_depth2 <65536, 256> __index3;
typedef __index_depth2 <65536, 65536> __index4;
typedef __index_depth3 <65536, 65536, 256> __index5;
typedef __index_depth3 <65536, 65536, 65536> __index6;
typedef __index_depth4 <65536, 65536, 65536, 256> __index7;
typedef __index_depth4 <65536, 65536, 65536, 65536> __index8;

}
#endif /* TIEREDINDEX_H_ */
