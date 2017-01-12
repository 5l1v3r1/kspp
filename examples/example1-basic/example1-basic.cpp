#include <iostream>
#include <string>
#include <chrono>
#include <kspp/codecs/binary_codec.h>
#include <kspp/topology_builder.h>
#include <kspp/algorithm.h>

#define PARTITION 0

struct page_view_data
{
  int64_t     time;
  int64_t     user_id;
  std::string url;
};

struct user_profile_data
{
  int64_t     last_modified_time;
  int64_t     user_id;
  std::string email;
};

struct page_view_decorated
{
  int64_t     time;
  int64_t     user_id;
  std::string url;
  std::string email;
};


std::string to_string(const page_view_data& pd) {
  return std::string("time:") + std::to_string(pd.time) + ", userid:" + std::to_string(pd.user_id) + ", url:" + pd.url;
}

std::string to_string(const user_profile_data& pd) {
  return std::string("last_modified_time:") + std::to_string(pd.last_modified_time) + ", userid:" + std::to_string(pd.user_id) + ", email:" + pd.email;
}

std::string to_string(const page_view_decorated& pd) {
  return std::string("time:") + std::to_string(pd.time) + ", userid:" + std::to_string(pd.user_id) + ", url:" + pd.url + ", email:" + pd.email;
}


namespace kspp {
inline size_t binary_encode(const page_view_data& obj, std::ostream& dst) {
  size_t sz = 0;
  sz += kspp::binary_encode(obj.time, dst);
  sz += kspp::binary_encode(obj.user_id, dst);
  sz += kspp::binary_encode(obj.url, dst);
  return dst.good() ? sz : 0;
}

inline size_t binary_decode(std::istream& src, page_view_data& obj) {
  size_t sz = 0;
  sz += kspp::binary_decode(src, obj.time);
  sz += kspp::binary_decode(src, obj.user_id);
  sz += kspp::binary_decode(src, obj.url);
  return src.good() ? sz : 0;
}

inline size_t binary_encode(const user_profile_data& obj, std::ostream& dst) {
  size_t sz = 0;
  sz += kspp::binary_encode(obj.last_modified_time, dst);
  sz += kspp::binary_encode(obj.user_id, dst);
  sz += kspp::binary_encode(obj.email, dst);
  return dst.good() ? sz : 0;
}

inline size_t binary_decode(std::istream& src, user_profile_data& obj) {
  size_t sz = 0;
  sz += kspp::binary_decode(src, obj.last_modified_time);
  sz += kspp::binary_decode(src, obj.user_id);
  sz += kspp::binary_decode(src, obj.email);
  return src.good() ? sz : 0;
}

inline size_t binary_encode(const std::pair<int64_t, int64_t>& obj, std::ostream& dst) {
  size_t sz = 0;
  sz += kspp::binary_encode(obj.first, dst);
  sz += kspp::binary_encode(obj.second, dst);
  return dst.good() ? sz : 0;
}

inline size_t binary_decode(std::istream& src, std::pair<int64_t, int64_t>& obj) {
  size_t sz = 0;
  sz += kspp::binary_decode(src, obj.first);
  sz += kspp::binary_decode(src, obj.second);
  return src.good() ? sz : 0;
}

inline size_t binary_encode(const page_view_decorated& obj, std::ostream& dst) {
  size_t sz = 0;
  sz += kspp::binary_encode(obj.time, dst);
  sz += kspp::binary_encode(obj.user_id, dst);
  sz += kspp::binary_encode(obj.url, dst);
  sz += kspp::binary_encode(obj.email, dst);
  return dst.good() ? sz : 0;
}

inline size_t binary_decode(std::istream& src, page_view_decorated& obj) {
  size_t sz = 0;
  sz += kspp::binary_decode(src, obj.time);
  sz += kspp::binary_decode(src, obj.user_id);
  sz += kspp::binary_decode(src, obj.url);
  sz += kspp::binary_decode(src, obj.email);
  return src.good() ? sz : 0;
}

template<> size_t kspp::text_codec::encode(const page_view_data& src, std::ostream& dst) {
  auto s = to_string(src);
  dst << s;
  return s.size();
}

template<> size_t kspp::text_codec::encode(const user_profile_data& src, std::ostream& dst) {
  auto s = to_string(src);
  dst << s;
  return s.size();
}

template<> size_t kspp::text_codec::encode(const page_view_decorated& src, std::ostream& dst) {
  auto s = to_string(src);
  dst << s;
  return s.size();
}
}; // namespace kspp

template<class T>
std::string ksource_to_string(const T&  ksource) {
  std::string res = std::to_string(ksource.event_time) + ", " + std::to_string(ksource.key) + ", " + (ksource.value ? to_string(*(ksource.value)) : "NULL");
  return res;
}

int main(int argc, char **argv) {
  auto builder = kspp::topology_builder<kspp::binary_codec>("localhost", "C:\\tmp");

  {
    auto sink = builder.create_kafka_sink<int64_t, page_view_data>("kspp_PageViews", 0);
    kspp::produce<int64_t, page_view_data>(*sink, 1, {1440557383335, 1, "/home?user=1"});
    kspp::produce<int64_t, page_view_data>(*sink, 5, {1440557383345, 5, "/home?user=5"});
    kspp::produce<int64_t, page_view_data>(*sink, 2, {1440557383456, 2, "/profile?user=2"});
    kspp::produce<int64_t, page_view_data>(*sink, 1, {1440557385365, 1, "/profile?user=1"});
    kspp::produce<int64_t, page_view_data>(*sink, 1, {1440557385368, 1, "/profile?user=1"});
  }

  {
    auto sink = builder.create_kafka_sink<int64_t, user_profile_data>("kspp_UserProfile", 0);
    kspp::produce<int64_t, user_profile_data>(*sink, 1, {1440557383335, 1, "user1@aol.com"});
    kspp::produce<int64_t, user_profile_data>(*sink, 5, {1440557383345, 5, "user5@gmail.com"});
    kspp::produce<int64_t, user_profile_data>(*sink, 2, {1440557383456, 2, "user2@yahoo.com"});
    kspp::produce<int64_t, user_profile_data>(*sink, 1, {1440557385365, 1, "user1-new-email-addr@comcast.com"});
  }

  {
    auto pageviews = builder.create_kafka_source<int64_t, page_view_data>("kspp_PageViews", 0);
    auto userprofiles = builder.create_kafka_source<int64_t, user_profile_data>("kspp_UserProfile", 0);

    auto pw_sink = builder.create_stream_sink<int64_t, page_view_data>(pageviews, std::cerr);
    auto up_sink = builder.create_stream_sink<int64_t, user_profile_data>(userprofiles, std::cerr);

    pageviews->start(-2);
    pageviews->flush();

    /*
    while (!pageviews->eof()) {
      pageviews->process_one();
    }
    */

    userprofiles->start(-2);
    userprofiles->flush();
  }

  {
    auto pageviews = builder.create_kstream<int64_t, page_view_data>("example3-pageviews_tmp", "kspp_PageViews", 0);
    auto pw_sink = builder.create_stream_sink<int64_t, page_view_data>(pageviews, std::cerr);
    pageviews->start();
    pageviews->flush();
    pageviews->commit();
  }

  {
    auto stream = builder.create_kafka_source<int64_t, page_view_data>("kspp_PageViews", 0);
    auto table = builder.create_ktable<int64_t, user_profile_data>("example3-join2", "kspp_UserProfile", 0);
    auto join = builder.create_left_join<int64_t, page_view_data, user_profile_data, page_view_decorated>(stream, table, [](const int64_t& key, const page_view_data& left, const user_profile_data& right, page_view_decorated& row) {
      row.user_id = key;
      row.email = right.email;
      row.time = left.time;
      row.url = left.url;
    });
    auto sink = builder.create_kafka_sink<int64_t, page_view_decorated>("kspp_PageViewsDecorated", PARTITION);
    join->start(-2);
    join->add_sink(sink);
    join->flush();
    join->commit();
  }

  std::cerr << "using iterators " << std::endl;
  {
    auto table = builder.create_ktable<int64_t, user_profile_data>("example3-kspp_UserProfile_tmp0", "kspp_UserProfile", PARTITION);
    table->start();
    table->flush();
    for (auto it = table->begin(), end = table->end(); it != end; ++it)
      std::cerr << "item : " << ksource_to_string(**it) << std::endl;
  }

  std::cerr << "using range iterators " << std::endl;
  {
    auto table = builder.create_ktable<int64_t, user_profile_data>("example3-kspp_UserProfile_tmp0", "kspp_UserProfile", PARTITION);
    table->start();
    table->flush();
    for (auto i : *table)
      std::cerr << "item : " << ksource_to_string(*i) << std::endl;
  }
  return 0;
}
