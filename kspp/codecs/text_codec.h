#include <string>
#include <ostream>
#include <istream>
#include <boost/uuid/uuid.hpp>
#pragma once

namespace csi {
class text_codec
{
  public:
  text_codec() {}

  static std::string name() { return "text"; }

  template<class T>
  size_t encode(const T& src, std::ostream& dst) {
    dst << "?";
    return 1;
    //return encode(this, src, dst);
  }

  template<class T>
  size_t decode(std::istream& src, T& dst) {
    return 0;
    //return decode(this, src, dst);
  }
};

template<> size_t text_codec::encode(const std::string& src, std::ostream& dst) {
  dst << src;
  return src.size();
}

template<> size_t text_codec::decode(std::istream& src, std::string& dst) {
  dst.clear();
  std::getline(src, dst);
  return dst.size();
}

template<> size_t text_codec::encode(const bool& src, std::ostream& dst) {
  dst << src ? "true" : "false";
  return src ? 4 : 5;
}

template<> size_t text_codec::decode(std::istream& src, bool& dst) {
  std::string s;
  std::getline(src, s);
  dst = (s == "true") ? true : false;
  return s.size();
}

template<> size_t text_codec::encode(const int& src, std::ostream& dst) {
  auto s = std::to_string(src);
  dst << s;
  return s.size();
}

template<> size_t text_codec::encode(const int64_t& src, std::ostream& dst) {
  auto s = std::to_string(src);
  dst << s;
  return s.size();
}


template<> size_t text_codec::decode(std::istream& src, int& dst) {
  std::string s;
  std::getline(src, s);
  dst = std::atoi(s.c_str());
  return s.size();
}
};

