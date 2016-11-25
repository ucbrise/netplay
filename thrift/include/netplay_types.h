/**
 * Autogenerated by Thrift Compiler (0.9.3)
 *
 * DO NOT EDIT UNLESS YOU ARE SURE THAT YOU KNOW WHAT YOU ARE DOING
 *  @generated
 */
#ifndef netplay_TYPES_H
#define netplay_TYPES_H

#include <iosfwd>

#include <thrift/Thrift.h>
#include <thrift/TApplicationException.h>
#include <thrift/protocol/TProtocol.h>
#include <thrift/transport/TTransport.h>

#include <thrift/cxxfunctional.h>


namespace netplay { namespace thrift {

class Storage;

typedef struct _Storage__isset {
  _Storage__isset() : dlog_size(false), olog_size(false), idx_sizes(false), stream_sizes(false) {}
  bool dlog_size :1;
  bool olog_size :1;
  bool idx_sizes :1;
  bool stream_sizes :1;
} _Storage__isset;

class Storage {
 public:

  Storage(const Storage&);
  Storage& operator=(const Storage&);
  Storage() : dlog_size(0), olog_size(0) {
  }

  virtual ~Storage() throw();
  int64_t dlog_size;
  int64_t olog_size;
  std::vector<int64_t>  idx_sizes;
  std::vector<int64_t>  stream_sizes;

  _Storage__isset __isset;

  void __set_dlog_size(const int64_t val);

  void __set_olog_size(const int64_t val);

  void __set_idx_sizes(const std::vector<int64_t> & val);

  void __set_stream_sizes(const std::vector<int64_t> & val);

  bool operator == (const Storage & rhs) const
  {
    if (!(dlog_size == rhs.dlog_size))
      return false;
    if (!(olog_size == rhs.olog_size))
      return false;
    if (!(idx_sizes == rhs.idx_sizes))
      return false;
    if (!(stream_sizes == rhs.stream_sizes))
      return false;
    return true;
  }
  bool operator != (const Storage &rhs) const {
    return !(*this == rhs);
  }

  bool operator < (const Storage & ) const;

  uint32_t read(::apache::thrift::protocol::TProtocol* iprot);
  uint32_t write(::apache::thrift::protocol::TProtocol* oprot) const;

  virtual void printTo(std::ostream& out) const;
};

void swap(Storage &a, Storage &b);

inline std::ostream& operator<<(std::ostream& out, const Storage& obj)
{
  obj.printTo(out);
  return out;
}

}} // namespace

#endif
