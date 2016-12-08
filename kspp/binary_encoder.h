#pragma once
#include <boost/uuid/uuid.hpp>
#include <ostream>
#include <istream>

namespace csi {
  template<class T>
  inline size_t binary_encode(const T& a, std::ostream& dst) {
    dst.write((const char*)&a, sizeof(T));
    return sizeof(T);
  }

  template<class T>
  inline size_t binary_decode(std::istream& src, T& dst) {
    src.read((char*)&dst, sizeof(T));
    return src.good() ? sizeof(T) : 0;
  }

  inline size_t binary_encode(const boost::uuids::uuid& a, std::ostream& dst) {
    dst.write((const char*)a.data, 16);
    return 16;
  }

  inline size_t binary_decode(std::istream& src, boost::uuids::uuid& dst) {
    src.read((char*)dst.data, 16);
    return src.good() ? 16 : 0;
  }

  inline size_t binary_encode(const std::string& a, std::ostream& dst) {
    uint32_t sz = (uint32_t) a.size();
    dst.write((const char*) &sz, sizeof(uint32_t));
    dst.write((const char*) a.data(), sz);
    return sz + sizeof(uint32_t);
  }

  inline size_t binary_decode(std::istream& src, std::string& dst) {
    uint32_t sz = 0;
    src.read((char*) &sz, sizeof(uint32_t));
    if (sz > 1048576) // sanity (not more that 1 MB)
      return 0;

    dst.resize(sz);
    src.read((char*) dst.data(), sz);
    return src.good() ? sz + sizeof(uint32_t) : 0;
  }
  
  class binary_codec
  {
  public:
    binary_codec() {}

    template<class T>
    size_t encode(const T& src, std::ostream& dst) {
      return binary_encode(src, dst);
    }

    template<class T>
    inline size_t decode(std::istream& src, T& dst) {
      return binary_decode(src, dst);
    }
  };
};

