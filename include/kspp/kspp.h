#include <type_traits>
#include <chrono>
#include <memory>
#include <cstdint>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <chrono>
#include <strstream>
#include <boost/filesystem.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <glog/logging.h>
#include <kspp/metrics/metrics.h>
#include <kspp/kevent.h>
#include <kspp/type_name.h>
#include <kspp/app_info.h>
#include <kspp/impl/queue.h>
#include <kspp/impl/hash/murmurhash2.h>

#pragma once
namespace kspp {

// move to kspp_utils.h
  std::string sanitize_filename(std::string s);

  std::vector<int> parse_partition_list(std::string s);

  std::vector<int> get_partition_list(int32_t nr_of_partitions);

  class processor {
  public:
    virtual ~processor() {}

    const std::vector<metric *> &get_metrics() const {
      return _metrics;
    }

    // not fast but useful for debugging
    int64_t get_metric(std::string name) {
      for (auto &&i : _metrics) {
        if (i->_name == name)
          return i->value();
      }
      return -1;
    }

    inline std::string record_type_name() const {
      return "[" + key_type_name() + "," + value_type_name() + "]";
    }

    virtual std::string key_type_name() const = 0;

    virtual std::string value_type_name() const = 0;

    virtual std::string simple_name() const = 0;

    /**
    * Process an input record
    */
    virtual bool process_one(int64_t tick) = 0;

    /**
    * returns the inbound queue len
    */
    virtual size_t queue_len() const {
      return 0;
    }

    /**
    * Do periodic work - will be called infrequently
    * use this to clean out allocated resources that is no longer needed
    */
    virtual void garbage_collect(int64_t tick) {}

  protected:
    // must be valid for processor lifetime  (cannot be removed)
    void add_metric(metric *p) {
      _metrics.push_back(p);
    }

    std::vector<metric *> _metrics;
  };

// this seems to be only sinks - rename to a better name (topic_sinks taken...)
// topic_sink_processor??
  class generic_sink : public processor {
  public:
    virtual ~generic_sink() {}


    virtual void poll(int timeout) {} //TBD =0 to force sinks to process its queue
    virtual bool eof() const = 0;

    virtual void punctuate(int64_t timestamp) {}

    virtual void close() = 0;

    virtual void flush() = 0;
    //virtual void flush() {
    //  while (!eof())
    //    if (!process_one(kspp::milliseconds_since_epoch())) {
    //      ;
    //    }
    //  //if (_upstream)   TBD!!!!!
    //  //  _upstream->flush();
    //  while (!eof())
    //    if (!process_one(kspp::milliseconds_since_epoch())) {
    //      ;
    //    }
    //  punctuate(milliseconds_since_epoch());
    //}

  protected:
  };

  class partition_processor : public processor {
  public:
    virtual ~partition_processor() {}

    size_t depth() const {
      size_t depth =0;
      for(auto i : upstream_) {
        std::max<size_t>(depth, i->depth()+1);
      }
      return depth;
    }

    virtual bool eof() const {
      for(auto i : upstream_) {
        if (i->eof()==false)
          return false;
      }
      return true;
    }

    virtual void poll(int timeout) {}

    virtual void punctuate(int64_t timestamp) {}

    virtual void close() {
      for(auto i : upstream_){
        i->close();
       }
      upstream_.clear();
    }

    inline int32_t partition() const {
      return _partition;
    }

    virtual void flush() {
      while (!eof())
        if (!process_one(kspp::milliseconds_since_epoch())) { ;
        }
      for(auto i : upstream_)
        i->flush();
      while (!eof())
        if (!process_one(kspp::milliseconds_since_epoch())) { ;
        }
      punctuate(milliseconds_since_epoch());
    }

    virtual void start() {
      for(auto i : upstream_)
        i->start();
    }

    virtual void start(int64_t offset) {
      for(auto i : upstream_)
        i->start(offset);
    }

    virtual void commit(bool flush) = 0;

    bool is_upstream(const partition_processor *node) const {
      // direct children?
      for(auto i : upstream_)
      {
        if (i == node)
          return true;
      }

      // further up?
      for(auto i : upstream_){
        if (i->is_upstream(node))
          return true;
      }
      return false;
     }

  protected:
    partition_processor(partition_processor *upstream, int32_t partition)
            : _partition(partition) {
      if (upstream)
        upstream_.push_back(upstream);
    }

    /*
     * partition_processor(std::vector<partition_processor*> upstream, int32_t partition)
            : _partition(partition) {
      upstream_.insert(upstream_.end(), upstream.begin(), upstream.end());
    }
     */

    void add_upstream(partition_processor* p){
      upstream_.push_back(p);
    }


    std::vector<partition_processor*> upstream_;
    const int32_t _partition;
  };

  class topology_base {
  protected:
    topology_base(std::shared_ptr<app_info> ai,
                  std::string topology_id,
                  std::string brokers,
                  std::chrono::milliseconds max_buffering,
                  boost::filesystem::path root_path);

    virtual ~topology_base();

  public:
    std::string app_id() const;

    std::string group_id() const;

    std::string topology_id() const;

    std::string brokers() const;

    std::string name() const;

    std::chrono::milliseconds max_buffering_time() const;

    void init_metrics();

    void for_each_metrics(std::function<void(kspp::metric &)> f);

    void init();

    bool eof();

    int process_one();

    void close();

    void start();

    void start(int offset);

    void commit(bool force);

    void flush();

    boost::filesystem::path get_storage_path();

  protected:
    bool _is_init;
    std::shared_ptr<app_info> _app_info;
    std::string _topology_id;
    std::string _brokers;
    std::chrono::milliseconds _max_buffering_time;
    boost::filesystem::path _root_path;
    std::vector<std::shared_ptr<partition_processor>> _partition_processors;
    std::vector<std::shared_ptr<generic_sink>> _sinks;
    std::vector<std::shared_ptr<partition_processor>> _top_partition_processors;
    int64_t _next_gc_ts;
    //next sink queue check loop count;
    //last sink queue size
  };

  template<class K, class V>
  class event_consumer {
  public:
    event_consumer() {
    }

    inline size_t queue_len() const {
      return _queue.size();
    }

  protected:
    kspp::event_queue<kevent<K, V>> _queue;
  };

  template<class K, class V>
  class partition_sink : public event_consumer<K, V>, public partition_processor {
  public:
    typedef K key_type;
    typedef V value_type;
    typedef kspp::kevent<K, V> record_type;

    virtual std::string key_type_name() const {
      return type_name<K>::get();
    }

    virtual std::string value_type_name() const {
      return type_name<V>::get();
    }

    inline void produce(std::shared_ptr<krecord<K, V>> r) {
      this->_queue.push_back(std::make_shared<kevent<K, V>>(r));
    }

    inline void produce(std::shared_ptr<kevent<K, V>> ev) {
      this->_queue.push_back(ev);
    }

    inline void produce(const K &key, const V &value, int64_t ts = milliseconds_since_epoch()) {
      this->_queue.push_back(std::make_shared<kevent<K, V>>(std::make_shared<krecord<K, V>>(key, value, ts)));
    }

  protected:
    partition_sink(int32_t partition)
            : event_consumer<K, V>(), partition_processor(nullptr, partition) {}

    //kspp::event_queue<kevent<K, V>> _queue;
  };

// specialisation for void key
  template<class V>
  class partition_sink<void, V> : public event_consumer<void, V>, public partition_processor {
  public:
    typedef void key_type;
    typedef V value_type;
    typedef kspp::kevent<void, V> record_type;

    virtual std::string key_type_name() const {
      return "void";
    }

    virtual std::string value_type_name() const {
      return type_name<V>::get();
    }

    inline void produce(std::shared_ptr<krecord<void, V>> r) {
      this->_queue.push_back(std::make_shared<kevent<void, V>>(r));
    }

    inline void produce(std::shared_ptr<kevent<void, V>> ev) {
      this->_queue.push_back(ev);
    }

    inline void produce(const V &value, int64_t ts = milliseconds_since_epoch()) {
      this->_queue.push_back(std::make_shared<kevent<void, V>>(std::make_shared<krecord<void, V>>(value, ts)));
    }

  protected:
    partition_sink(int32_t partition)
            : event_consumer<void, V>(), partition_processor(nullptr, partition) {
    }

    //kspp::event_queue<kevent<void, V>> _queue;
  };

// specialisation for void value
  template<class K>
  class partition_sink<K, void> : public event_consumer<K, void>, public partition_processor {
  public:
    typedef K key_type;
    typedef void value_type;
    typedef kspp::kevent<K, void> record_type;

    std::string key_type_name() const override {
      return type_name<K>::get();
    }

    std::string value_type_name() const override {
      return "void";
    }

    inline void produce(std::shared_ptr<krecord<K, void>> r) {
      this->_queue.push_back(std::make_shared<kevent<K, void>>(r));
    }

    inline void produce(std::shared_ptr<kevent<K, void>> ev) {
      this->_queue.push_back(ev);
    }

    inline void produce(const K &key, int64_t ts = milliseconds_since_epoch()) {
      this->_queue.push_back(std::make_shared<kevent<K, void>>(std::make_shared<krecord<K, void>>(key, ts)));
    }

  protected:
    explicit partition_sink(int32_t partition)
            : event_consumer<K, void>(), partition_processor(nullptr, partition) {}
  };

  template<class PK, class CODEC>
  inline uint32_t get_partition_hash(const PK &key, std::shared_ptr<CODEC> codec = std::make_shared<CODEC>()) {
    enum { MAX_KEY_SIZE = 1000 };
    uint32_t partition_hash = 0;
    char key_buf[MAX_KEY_SIZE];
    size_t ksize = 0;
    std::strstream s(key_buf, MAX_KEY_SIZE);
    ksize = codec->encode(key, s);
    //partition_hash = djb_hash(key_buf, ksize);
    partition_hash = MurmurHash2(key_buf, (int) ksize, 0x9747b28c);
    return partition_hash;
  }

  template<class PK, class CODEC>
  inline uint32_t
  get_partition(const PK &key, size_t nr_of_partitions, std::shared_ptr<CODEC> codec = std::make_shared<CODEC>()) {
    uint32_t hash = get_partition_hash<PK, CODEC>(key, codec);
    return hash % (uint32_t) nr_of_partitions;
  }

/**
  we need this class to get rid of the codec for templates..
*/
  template<class K, class V>
  class topic_sink : public event_consumer<K, V>, public generic_sink {
  public:
    typedef K key_type;
    typedef V value_type;
    //typedef kspp::kevent<K, V>  event_type;
    //typedef kspp::krecord<K, V> record_type;

    std::string key_type_name() const override {
      return type_name<K>::get();
    }

    std::string value_type_name() const override {
      return type_name<V>::get();
    }

    inline void produce(std::shared_ptr<krecord<K, V>> r) {
      this->_queue.push_back(std::make_shared<kevent<K, V>>(r));
    }

    inline void produce(std::shared_ptr<kevent<K, V>> ev) {
      this->_queue.push_back(ev);
    }

    inline void produce(uint32_t partition_hash, std::shared_ptr<krecord<K, V>> r) {
      auto ev = std::make_shared<kevent<K, V>>(r, nullptr, partition_hash);
      this->_queue.push_back(ev);
    }

    inline void produce(uint32_t partition_hash, std::shared_ptr<kevent<K, V>> t) {
      auto ev2 = std::make_shared<kevent<K, V>>(t->record(), t->id(),
                                                partition_hash); // make new one since change the partition
      this->_queue.push_back(ev2);
    }

    inline void produce(const K &key, const V &value, int64_t ts = milliseconds_since_epoch()) {
      produce(std::make_shared<krecord<K, V>>(key, value, ts));
    }

    inline void
    produce(uint32_t partition_hash, const K &key, const V &value, int64_t ts = milliseconds_since_epoch()) {
      produce(partition_hash, std::make_shared<krecord<K, V>>(key, value, ts));
    }

    size_t queue_len() const override {
      return event_consumer<K, V>::queue_len();
    }

  protected:
    //kspp::event_queue<kevent<K, V>> _queue;
  };

// spec for void key
  template<class V>
  class topic_sink<void, V> : public generic_sink {
  public:
    typedef void key_type;
    typedef V value_type;
    //typedef kspp::kevent<void, V> record_type;

    std::string key_type_name() const override {
      return "void";
    }

    std::string value_type_name() const override {
      return type_name<V>::get();
    }

    inline void produce(std::shared_ptr<krecord<void, V>> r) {
      this->_queue.push_back(std::make_shared<kevent<void, V>>(r));
    }

    inline void produce(std::shared_ptr<kevent<void, V>> ev) {
      this->_queue.push_back(ev);
    }

    inline void produce(uint32_t partition_hash, std::shared_ptr<krecord<void, V>> r) {
      auto ev = std::make_shared<kevent<void, V>>(r);
      ev->_partition_hash = partition_hash;
      this->_queue.push_back(ev);
    }

    inline void produce(uint32_t partition_hash, std::shared_ptr<kevent<void, V>> ev) {
      auto ev2 = std::make_shared<kevent<void, V>>(*ev); // make new one since change the partition
      ev2->_partition_hash = partition_hash;
      this->_queue.push_back(ev2);
    }

    inline void produce(const V &value, int64_t ts = milliseconds_since_epoch()) {
      produce(std::make_shared<krecord<void, V>>(value, ts));
    }

    inline void produce(uint32_t partition_hash, const V &value, int64_t ts = milliseconds_since_epoch()) {
      produce(partition_hash, std::make_shared<krecord<void, V>>(value, ts));
    }

  protected:
    kspp::event_queue<kevent<void, V>> _queue;
  };

// spec for void value
  template<class K>
  class topic_sink<K, void> : public generic_sink {
  public:
    typedef K key_type;
    typedef void value_type;
    //typedef kspp::kevent<K, void> record_type;

    std::string key_type_name() const override {
      return type_name<K>::get();
    }

    std::string value_type_name() const override {
      return "void";
    }

    inline void produce(std::shared_ptr<krecord<K, void>> r) {
      this->_queue.push_back(std::make_shared<kevent<K, void>>(r));
    }

    inline void produce(std::shared_ptr<kevent<K, void>> ev) {
      this->_queue.push_back(ev);
    }

    inline void produce(uint32_t partition_hash, std::shared_ptr<krecord<K, void>> r) {
      auto ev = std::make_shared<kevent<K, void>>(r);
      ev->_partition_hash = partition_hash;
      this->_queue.push_back(ev);
    }

    inline void produce(uint32_t partition_hash, std::shared_ptr<kevent<K, void>> ev) {
      auto ev2 = std::make_shared<kevent<K, void>>(*ev); // make new one since change the partition
      ev2->_partition_hash = partition_hash;
      this->_queue.push_back(ev2);
    }

    inline void produce(const K &key, int64_t ts = milliseconds_since_epoch()) {
      produce(std::make_shared<krecord<K, void>>(key, ts));
    }

    inline void produce(uint32_t partition_hash, const K &key, int64_t ts = milliseconds_since_epoch()) {
      produce(partition_hash, std::make_shared<krecord<K, void>>(key, ts));
    }

  protected:
    kspp::event_queue<kevent<K, void>> _queue;
  };

  template<class K, class V>
  class partition_source : public partition_processor {
  public:
    using sink_function = typename std::function<void(std::shared_ptr<kevent<K, V>>)>;

    partition_source(partition_processor *upstream, int32_t partition)
            : partition_processor(upstream, partition), _out_messages("out_message_count") {
      this->add_metric(&_out_messages);
    }

    std::string key_type_name() const override {
      return type_name<K>::get();
    }

    std::string value_type_name() const override {
      return type_name<V>::get();
    }

    template<class SINK>
    typename std::enable_if<std::is_base_of<kspp::partition_sink<K, V>, SINK>::value, void>::type
    add_sink(SINK *sink) {
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
    typename std::enable_if<std::is_base_of<kspp::topic_sink<K, V>, SINK>::value, void>::type
    add_sink(std::shared_ptr<SINK> sink) {
      add_sink([sink](auto e) {
        sink->produce(e);
      });
    }

    void add_sink(sink_function sink) {
      _sinks.push_back(sink);
    }

  protected:

    virtual void send_to_sinks(std::shared_ptr<kevent<K, V>> p)  {
      if (!p)
        return;
      ++_out_messages;
      for (auto f : _sinks)
        f(p);
    }

    metric_counter _out_messages;
    std::vector<sink_function> _sinks;
  };

  template<class K, class V>
  class kmaterialized_source_iterator_impl {
  public:
    virtual ~kmaterialized_source_iterator_impl() {}

    virtual void next() = 0;

    virtual std::shared_ptr<const krecord<K, V>> item() const = 0;

    virtual bool valid() const = 0;

    virtual bool operator==(const kmaterialized_source_iterator_impl &other) const = 0;

    bool operator!=(const kmaterialized_source_iterator_impl &other) const { return !(*this == other); }
  };

  template<class K, class V>
  class materialized_source : public partition_source<K, V> {
  public:
    class iterator : public std::iterator<
            std::forward_iterator_tag,            // iterator_category
            std::shared_ptr<const krecord<K, V>>, // value_type
            long,                                 // difference_type
            std::shared_ptr<const krecord<K, V>> *,// pointer
            std::shared_ptr<const krecord<K, V>>  // reference
    > {
      std::shared_ptr<kmaterialized_source_iterator_impl<K, V>> _impl;
    public:
      explicit iterator(std::shared_ptr<kmaterialized_source_iterator_impl<K, V>> impl) : _impl(impl) {}

      iterator &operator++() {
        _impl->next();
        return *this;
      }

      iterator operator++(int) {
        iterator retval = *this;
        ++(*this);
        return retval;
      }

      bool operator==(const iterator &other) const { return *_impl == *other._impl; }

      bool operator!=(const iterator &other) const { return !(*this == other); }

      std::shared_ptr<const krecord<K, V>> operator*() const { return _impl->item(); }
    };

    virtual iterator begin() const = 0;

    virtual iterator end() const = 0;

    virtual std::shared_ptr<const krecord<K, V>> get(const K &key) const = 0;

    materialized_source(partition_processor *upstream, int32_t partition)
            : partition_source<K, V>(upstream, partition) {
    }

    virtual boost::filesystem::path get_storage_path(boost::filesystem::path storage_path) {
      boost::filesystem::path p(std::move(storage_path));
      p /= sanitize_filename(this->simple_name() + this->record_type_name() + "#" + std::to_string(this->partition()));
      return p;
    }
  };
}; // namespace