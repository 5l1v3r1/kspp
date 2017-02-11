#include <type_traits>
#include <chrono>
#include <memory>
#include <cstdint>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <chrono>
#include <regex>
#include <strstream>
#include <boost/filesystem.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/log/trivial.hpp>
#include <boost/algorithm/string/replace.hpp>
#include "metrics.h"
#include "type_name.h"

#pragma once
namespace kspp {
inline int64_t milliseconds_since_epoch() {
  return std::chrono::duration_cast<std::chrono::milliseconds>
    (std::chrono::system_clock::now().time_since_epoch()).count();
}

inline std::string sanitize_filename(std::string s) {
  auto e = std::regex("[/?<>\\:*|\"]");
  s = std::regex_replace(s, e, "_");
  return s;
}

template<class K, class V>
struct krecord
{
  krecord() : event_time(-1), offset(-1) {}
  krecord(const K& k) : event_time(milliseconds_since_epoch()), offset(-1), key(k) {}
  krecord(const K& k, const V& v) : event_time(milliseconds_since_epoch()), offset(-1), key(k), value(std::make_shared<V>(v)) {}
  krecord(const K& k, std::shared_ptr<V> v) : event_time(milliseconds_since_epoch()), offset(-1), key(k), value(v) {}
  krecord(const K& k, std::shared_ptr<V> v, int64_t ts) : event_time(ts), offset(-1), key(k), value(v) {}

  K                  key;
  std::shared_ptr<V> value;
  int64_t            event_time;
  int64_t            offset;
};

template<class V>
struct krecord<void, V>
{
  krecord(const V& v) : event_time(milliseconds_since_epoch()), offset(-1), value(std::make_shared<V>(v)) {}
  krecord(std::shared_ptr<V> v) : event_time(milliseconds_since_epoch()), offset(-1), value(v) {}
  krecord(std::shared_ptr<V> v, int64_t ts) : event_time(ts), offset(-1), value(v) {}
  std::shared_ptr<V> value;
  int64_t            event_time;
  int64_t            offset;
};

template<class K>
struct krecord<K, void>
{
  krecord() : event_time(-1), offset(-1) {}
  krecord(const K& k) : event_time(milliseconds_since_epoch()), offset(-1), key(k) {}
  krecord(const K& k, int64_t ts) : event_time(ts), offset(-1), key(k) {}
  K                  key;
  int64_t            event_time;
  int64_t            offset;
};

class processor
{
  public:
  virtual ~processor() {}

  const std::vector<metric*>& get_metrics() const {
    return _metrics;
  }

  inline std::string record_type_name() const {
    return "[" + key_type_name() + "," + value_type_name() + "]";
  }

  virtual std::string key_type_name() const = 0;
  virtual std::string value_type_name() const = 0;

  protected:
  // must be valid for processor lifetime  (cannot be removed)
  void add_metric(metric* p) {
    _metrics.push_back(p);
  }

  /*
  int64_t identity() const {
    return (int64_t) this;
  }
  */



  //const size_t          _partition; or -1 if not valid??
  //partition_processor*  _upstream;
  std::vector<metric*>  _metrics;
};

class topic_processor : public processor
{
  public:
  virtual ~topic_processor() {}
  //virtual void init(processor_context*) {}
  virtual std::string name() const = 0;
  virtual std::string processor_name() const { return "topic_processor"; }

  virtual void poll(int timeout) {}
  virtual bool eof() const = 0;

  /**
  * Process an input record
  */
  virtual bool process_one() = 0;
  virtual void punctuate(int64_t timestamp) {}
  virtual void close() = 0;


  virtual void flush() {
    while (!eof())
      if (!process_one()) {
        //using namespace std::chrono_literals;
        //std::this_thread::sleep_for(10ms);
        ;
      }
    //if (_upstream)   TBD!!!!!
    //  _upstream->flush();
    while (!eof())
      if (!process_one()) {
        //using namespace std::chrono_literals;
        //std::this_thread::sleep_for(10ms);
        ;
      }
    punctuate(milliseconds_since_epoch());
  }

  protected:
};

class partition_processor : public processor
{
  public:
  virtual ~partition_processor() {}
  virtual std::string name() const = 0;
  virtual std::string processor_name() const { return "partition_processor"; }

  size_t depth() const {
    return _upstream ? _upstream->depth() + 1 : 0;
  }

  /**
  * Process an input record
  */
  virtual bool process_one() {
    return _upstream ? _upstream->process_one() : false;
  }

  virtual bool eof() const {
    return _upstream ? _upstream->eof() : true;
  }

  virtual void poll(int timeout) {}
  virtual void punctuate(int64_t timestamp) {}

  virtual void close() {
    if (_upstream)
      _upstream->close();
  }

  inline uint32_t partition() const {
    return (uint32_t) _partition;
  }

  virtual void flush() {
    while (!eof())
      if (!process_one()) {
        //using namespace std::chrono_literals;
        //std::this_thread::sleep_for(10ms);
        ;
      }
    if (_upstream)
      _upstream->flush();
    while (!eof())
      if (!process_one()) {
        //using namespace std::chrono_literals;
        //std::this_thread::sleep_for(10ms);
        ;
      }
    punctuate(milliseconds_since_epoch());
  }

  virtual void start() {
    if (_upstream)
      _upstream->start();
  }

  virtual void start(int64_t offset) {
    if (_upstream)
      _upstream->start(offset);
  }

  virtual void commit() {}
  virtual void flush_offset() {}

  bool is_upstream(const partition_processor* node) const {
    //return _upstream ? _upstream == node ? true : _upstream->is_upstream(node) : false;
    if (_upstream == nullptr)
      return false;
    if (_upstream == node)
      return true;
    return _upstream->is_upstream(node);
  }

  protected:
  partition_processor(partition_processor* upstream, size_t partition)
    : _upstream(upstream)
    , _partition(partition) {}

  const size_t          _partition;
  partition_processor*  _upstream;
};

struct app_info
{
  /**
  * multi instance apps - state stores will be prefixed with instance_id
  * metrics will have instance_id and app_instance_name tags
  */
  app_info(std::string _app_namespace,
           std::string _app_id,
           std::string _app_instance_id,
           std::string _app_instance_name)
    : app_namespace(_app_namespace)
    , app_id(_app_id)
    , app_instance_id(_app_instance_id)
    , app_instance_name(_app_instance_name) {}

  /**
  * single instance apps - state stores will not be prefixed with instance_id
  * metrics will not have instance_id or app_instance_name tag ??
  * maybe they should be marked as single instance?
  */
  app_info(std::string _app_namespace,
           std::string _app_id)
    : app_namespace(_app_namespace)
    , app_id(_app_id) {}

  std::string identity() const {
    if (app_instance_id.size() == 0)
      return app_namespace + "::" + app_id;
    else
      return app_namespace + "::" + app_id + "#" + app_instance_id;
  }

  const std::string app_namespace;
  const std::string app_id;
  const std::string app_instance_id;
  const std::string app_instance_name;
};

inline std::string to_string(const app_info& obj) {
  return obj.identity();
}

class topology_base
{
  protected:
  topology_base(std::shared_ptr<app_info> ai,
                std::string topology_id,
                int32_t partition,
                std::string brokers,
                boost::filesystem::path root_path)
    : _app_info(ai)
    , _is_init(false)
    , _topology_id(topology_id)
    , _partition(partition)
    , _brokers(brokers)
    , _root_path(root_path) {
    BOOST_LOG_TRIVIAL(info) << "topology created, name:" << name();
  }

  virtual ~topology_base() {
    BOOST_LOG_TRIVIAL(info) << "topology, name:" << name() << " terminated";
    // output stats
  }

  public:
  std::string app_id() const {
    return _app_info->identity();
  }

  std::string topology_id() const {
    return _topology_id;
  }

  int32_t partition() const {
    return _partition;
  }

  std::string brokers() const {
    return _brokers;
  }

  std::string name() const {
    return "[" + _app_info->identity() + "]" + _topology_id + "[p" + std::to_string(_partition) + "]";
  }
  // the metrics should look like this...
  //cpu_load_short, host=server01, region=us-west value=0.64 1434055562000000000
  //metric_name,app_id={app_id}},topology={{_topology_id}},depth={{depth}},processor_type={{processor_name()}},record_type="
  //order tags descending
  std::string escape_influx(std::string s) {
    std::string s2 = boost::replace_all_copy(s, " ", "\\ ");
    std::string s3 = boost::replace_all_copy(s2, ",", "\\,");
    std::string s4 = boost::replace_all_copy(s3, "=", "\\=");
    /*
    static const std::string toReplace = " ,="; // character class that matches . and -
    std::string replacement = ";";
    std::string processedString = boost::replace_all_regex_copy(someString, boost::regex(toReplace), replacement);
    */
    return s4;
  }

  void init_metrics() {
    for (auto i : _partition_processors) {
      for (auto j : i->get_metrics()) {
        j->_logged_name = j->_simple_name
          + ",app_id=" + escape_influx(_app_info->app_id)
          + ",app_instance_id=" + escape_influx(_app_info->app_instance_id.size() ? _app_info->app_instance_id : "single-instance")
          + ",app_instance_name=" + escape_influx(_app_info->app_instance_name.size() ? _app_info->app_instance_name : "noname")
          + ",app_namespace=" + escape_influx(_app_info->app_namespace)
          + ",depth=" + std::to_string(i->depth())
          + ",key_type=" + escape_influx(i->key_type_name())
          + ",partition=" + std::to_string(i->partition())
          + ",processor_type=" + escape_influx(i->processor_name())
          + ",topology=" + escape_influx(_topology_id)
          + ",value_type=" + escape_influx(i->value_type_name());
      }
    }

    for (auto i : _topic_processors) {
      for (auto j : i->get_metrics()) {
        j->_logged_name = j->_simple_name
          + ",app_id=" + escape_influx(_app_info->app_id)
          + ",app_instance_id=" + escape_influx(_app_info->app_instance_id.size() ? _app_info->app_instance_id : "single-instance")
          + ",app_instance_name=" + escape_influx(_app_info->app_instance_name.size() ? _app_info->app_instance_name : "noname")
          + ",app_namespace=" + escape_influx(_app_info->app_namespace)
          + ",key_type=" + escape_influx(i->key_type_name())
          + ",processor_type=" + escape_influx(i->processor_name())
          + ",topology=" + escape_influx(_topology_id)
          + ",value_type=" + escape_influx(i->value_type_name());
      }
    }
  }

  void for_each_metrics(std::function<void(kspp::metric&)> f) {
    for (auto i : _partition_processors)
      for (auto j : i->get_metrics())
        f(*j);

    for (auto i : _topic_processors)
      for (auto j : i->get_metrics())
        f(*j);
  }

  void init() {
    _top_partition_processors.clear();

    for (auto i : _partition_processors) {
      bool upstream_of_something = false;
      for (auto j : _partition_processors) {
        if (j->is_upstream(i.get()))
          upstream_of_something = true;
      }
      if (!upstream_of_something) {
        BOOST_LOG_TRIVIAL(info) << "topology << " << name() << ": adding " << i->name() << " to top";
        _top_partition_processors.push_back(i);
      } else {
        BOOST_LOG_TRIVIAL(info) << "topology << " << name() << ": skipping poll of " << i->name();
      }
    }
    _is_init = true;
  }

  bool eof() {
    for (auto&& i : _top_partition_processors) {
      if (!i->eof())
        return false;
    }
    return true;
  }

  int process_one() {
    // maybe we should check sinks here an return 0 if we need to wait...

    int res = 0;
    for (auto&& i : _top_partition_processors) {
      res += i->process_one();
    }
    // is this nessessary??? are those only sinks??
    for (auto i : _topic_processors)
      res += i->process_one();
    return res;
  }

  void close() {
    for (auto i : _topic_processors)
      i->close();
    for (auto i : _partition_processors)
      i->close();
  }

  void start() {
    if (!_is_init)
      init();
    for (auto&& i : _top_partition_processors)
      i->start();
    //for (auto i : _topic_processors) // those are only sinks??
    //  i->start();
  }

  void start(int offset) {
    if (!_is_init)
      init();
    for (auto&& i : _top_partition_processors)
      i->start(offset);
    //for (auto i : _topic_processors) // those are only sinks??
    //  i->start(offset);
  }

  /*void flush() {
    for (auto i : _top_partition_processors)
      i->flush();
  }
  */

  void flush() {
    while (!eof())
      if (!process_one()) {
        //using namespace std::chrono_literals;
        //std::this_thread::sleep_for(10ms);
        ;
      }
    for (auto i : _top_partition_processors)
      i->flush();

    while (!eof())
      if (!process_one()) {
        //using namespace std::chrono_literals;
        //std::this_thread::sleep_for(10ms);
        ;
      }
  }

  boost::filesystem::path get_storage_path() {
    boost::filesystem::path top_of_topology(_root_path);
    top_of_topology /= sanitize_filename(_app_info->identity());
    top_of_topology /= sanitize_filename(_topology_id);
    BOOST_LOG_TRIVIAL(debug) << "topology " << _app_info->identity() << ": creating local storage at " << top_of_topology;
    auto res = boost::filesystem::create_directories(top_of_topology);
    // seems to be a bug in boost - always return false...
    //if (!res)
    //  BOOST_LOG_TRIVIAL(error) << "topology " << _app_info->identity() << ": failed to create local storage at " << top_of_topology;
    // so we check if the directory exists after instead...
    if (!boost::filesystem::exists(top_of_topology))
      BOOST_LOG_TRIVIAL(error) << "topology " << _app_info->identity() << ": failed to create local storage at " << top_of_topology;
    return top_of_topology;
  }

  protected:
  bool                                              _is_init;
  std::shared_ptr<app_info>                         _app_info;
  std::string                                       _topology_id;
  int32_t                                           _partition;
  std::string                                       _brokers;
  boost::filesystem::path                           _root_path;
  std::vector<std::shared_ptr<partition_processor>> _partition_processors;
  std::vector<std::shared_ptr<topic_processor>>     _topic_processors;
  std::vector<std::shared_ptr<partition_processor>> _top_partition_processors;
};


template<class K, class V>
class partition_sink : public partition_processor
{
  public:
  typedef K key_type;
  typedef V value_type;
  typedef kspp::krecord<K, V> record_type;

  virtual int produce(std::shared_ptr<krecord<K, V>> r) = 0;
  inline int produce(const K& key, const V& value) {
    return produce(std::make_shared<krecord<K, V>>(key, value));
  }
  virtual std::string key_type_name() const {
    return type_name<K>::get();
  }

  virtual std::string value_type_name() const {
    return type_name<V>::get();
  }

  virtual size_t queue_len() = 0;
  protected:
  partition_sink(size_t partition)
    : partition_processor(nullptr, partition) {}
};

// specialisation for void key
template<class V>
class partition_sink<void, V> : public partition_processor
{
  public:
  typedef void key_type;
  typedef V value_type;
  typedef kspp::krecord<void, V> record_type;

  virtual std::string key_type_name() const {
    return "void";
  }

  virtual std::string value_type_name() const {
    return type_name<V>::get();
  }

  virtual int produce(std::shared_ptr<krecord<void, V>> r) = 0;
  inline int produce(const V& value) {
    return produce(std::make_shared<krecord<void, V>>(value));
  }

  virtual size_t queue_len() = 0;
  protected:
  partition_sink(size_t partition)
    : partition_processor(nullptr, partition) {}
};

// specialisation for void value
template<class K>
class partition_sink<K, void> : public partition_processor
{
  public:
  typedef K key_type;
  typedef void value_type;
  typedef kspp::krecord<K, void> record_type;

  virtual std::string key_type_name() const {
    return type_name<K>::get();
  }

  virtual std::string value_type_name() const {
    return "void";
  }

  virtual int produce(std::shared_ptr<krecord<K, void>> r) = 0;
  inline int produce(const K& key) {
    return produce(std::make_shared<krecord<K, void>>(key));
  }

  virtual size_t queue_len() = 0;
  protected:
  partition_sink(size_t partition)
    : partition_processor(nullptr, partition) {}
};


inline uint32_t djb_hash(const char *str, size_t len) {
  uint32_t hash = 5381;
  for (size_t i = 0; i < len; i++)
    hash = ((hash << 5) + hash) + str[i];
  return hash;
}

template<class PK, class CODEC>
inline uint32_t get_partition_hash(const PK& key, std::shared_ptr<CODEC> codec) {
  enum { MAX_KEY_SIZE = 1000 };
  uint32_t partition_hash = 0;
  char key_buf[MAX_KEY_SIZE];
  size_t ksize = 0;
  std::strstream s(key_buf, MAX_KEY_SIZE);
  ksize = codec->encode(key, s);
  partition_hash = djb_hash(key_buf, ksize);
  return partition_hash;
}

template<class PK, class CODEC>
inline uint32_t get_partition(const PK& key, size_t nr_of_partitions, std::shared_ptr<CODEC> codec) {
  auto hash = get_partition_hash <PK, CODEC>(key, codec);
  return hash % nr_of_partitions;
}

/**
  we need this class to get rid of the codec for templates..
*/
template<class K, class V>
class topic_sink_base : public topic_processor
{
  public:
  typedef K key_type;
  typedef V value_type;
  typedef kspp::krecord<K, V> record_type;

  virtual int produce(std::shared_ptr<krecord<K, V>> r) = 0;

  virtual std::string key_type_name() const {
    return type_name<K>::get();
  }

  virtual std::string value_type_name() const {
    return type_name<V>::get();
  }

  virtual size_t queue_len() = 0;
};

template<class K, class V, class CODEC>
class topic_sink : public topic_sink_base<K, V>
{
  public:
  virtual int produce(std::shared_ptr<krecord<K, V>> r) = 0;
  virtual int produce(uint32_t partition_hash, std::shared_ptr<krecord<K, V>> r) = 0;
  inline  int produce(uint32_t partition_hash, const K& key, const V& value) {
    return produce(partition_hash, std::make_shared<krecord<K, V>>(key, value));
  }

  inline std::shared_ptr<CODEC> codec() {
    return _codec;
  }
  protected:
  topic_sink(std::shared_ptr<CODEC> codec)
    :_codec(codec) {}

  std::shared_ptr<CODEC> _codec;
};

// specialisation for void key
template<class V, class CODEC>
class topic_sink<void, V, CODEC> : public topic_sink_base<void, V>
{
  public:
  virtual int produce(std::shared_ptr<krecord<void, V>> r) = 0;
  virtual int produce(uint32_t partition_hash, std::shared_ptr<krecord<void, V>> r) = 0;
  inline  int produce(uint32_t partition_hash, const V& value) {
    return produce(partition_hash, std::make_shared<krecord<void, V>>(value));
  }

  inline std::shared_ptr<CODEC> codec() {
    return _codec;
  }

  protected:
  topic_sink(std::shared_ptr<CODEC> codec)
    :_codec(codec) {}

  std::shared_ptr<CODEC> _codec;
};

// specialisation for void value
template<class K, class CODEC>
class topic_sink<K, void, CODEC> : public topic_sink_base<K, void>
{
  public:
  virtual int produce(std::shared_ptr<krecord<K, void>> r) = 0;
  inline  int produce(uint32_t partition_hash, const K& key) {
    return produce(partition_hash, std::make_shared<krecord<K, void>>(key));
  }

  inline std::shared_ptr<CODEC> codec() {
    return _codec;
  }

  protected:
  topic_sink(std::shared_ptr<CODEC> codec)
    :_codec(codec) {}

  std::shared_ptr<CODEC> _codec;
};

template<class K, class V>
class partition_source : public partition_processor
{
  public:
  using sink_function = typename std::function<void(std::shared_ptr<krecord<K, V>>)>;

  partition_source(partition_processor* upstream, size_t partition)
    : partition_processor(upstream, partition)
    , _out_messages("out_message_count") {
    this->add_metric(&_out_messages);
  }

  virtual std::string key_type_name() const {
    return type_name<K>::get();
  }

  virtual std::string value_type_name() const {
    return type_name<V>::get();
  }


  /*
  void add_sink(std::shared_ptr<partition_sink<K, V>> sink) {
    assert(sink.get() != nullptr);
    add_sink([sink](auto e) {
      sink->produce(e);
    });
  }
  */

  template<class SINK>
  typename std::enable_if<std::is_base_of<kspp::partition_sink<K, V>, SINK>::value, void>::type
    add_sink(SINK* sink) {
    add_sink([sink](auto e) {
      sink->produce(e);
    });
  }

  template<class SINK>
  typename std::enable_if<std::is_base_of<kspp::partition_sink<K, V>, SINK>::value, void>::type
    add_sink(std::shared_ptr<SINK> sink) {
    add_sink([sink](auto e) {
      sink->produce(e);
    });
  }

  template<class SINK>
  typename std::enable_if<std::is_base_of<kspp::topic_sink_base<K, V>, SINK>::value, void>::type
    add_sink(std::shared_ptr<SINK> sink) {
    add_sink([sink](auto e) {
      sink->produce(e);
    });
  }

  void add_sink(sink_function sink) {
    _sinks.push_back(sink);
  }

  protected:

  virtual void send_to_sinks(std::shared_ptr<krecord<K, V>> p) {
    if (!p)
      return;
    ++_out_messages;
    for (auto f : _sinks)
      f(p);
  }

  metric_counter             _out_messages;
  std::vector<sink_function> _sinks;
};

template<class K, class V>
class kmaterialized_source_iterator_impl
{
  public:
  virtual ~kmaterialized_source_iterator_impl() {}
  virtual void next() = 0;
  virtual std::shared_ptr<krecord<K, V>> item() const = 0;
  virtual bool valid() const = 0;
  virtual bool operator==(const kmaterialized_source_iterator_impl& other) const = 0;
  bool operator!=(const kmaterialized_source_iterator_impl& other) const { return !(*this == other); }
};

template<class K, class V>
class materialized_partition_source : public partition_source<K, V>
{
  public:
  class iterator : public std::iterator <
    std::forward_iterator_tag,      // iterator_category
    std::shared_ptr<krecord<K, V>>, // value_type
    long,                           // difference_type
    std::shared_ptr<krecord<K, V>>*,// pointer
    std::shared_ptr<krecord<K, V>>  // reference
  >
  {
    std::shared_ptr<kmaterialized_source_iterator_impl<K, V>> _impl;
    public:
    explicit iterator(std::shared_ptr<kmaterialized_source_iterator_impl<K, V>> impl) : _impl(impl) {}
    iterator& operator++() { _impl->next(); return *this; }
    iterator operator++(int) { iterator retval = *this; ++(*this); return retval; }
    bool operator==(const iterator& other) const { return *_impl == *other._impl; }
    bool operator!=(const iterator& other) const { return !(*this == other); }
    std::shared_ptr<krecord<K, V>> operator*() const { return _impl->item(); }
  };
  virtual iterator begin() = 0;
  virtual iterator end() = 0;
  virtual std::shared_ptr<krecord<K, V>> get(const K& key) = 0;

  materialized_partition_source(partition_processor* upstream, size_t partition)
    : partition_source<K, V>(upstream, partition) {}
};

template<class K, class V>
class kstream_partition : public partition_source<K, V>
{
  public:
  kstream_partition(partition_processor* upstream)
    : partition_source<K, V>(upstream, upstream->partition()) {}
};

template<class K, class V>
class ktable_partition : public materialized_partition_source<K, V>
{
  public:
  ktable_partition(partition_processor* upstream)
    : materialized_partition_source<K, V>(upstream, upstream->partition()) {}
};
}; // namespace