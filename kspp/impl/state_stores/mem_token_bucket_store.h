#include <map>
#include <chrono>
#include <cstdint>
#include <kspp/kspp.h>
#include "token_bucket_store.h"
#pragma once

namespace kspp {
template<class K>
class mem_token_bucket_store : public token_bucket_store<K>
{
  public:
  mem_token_bucket_store(std::chrono::milliseconds agetime, size_t capacity)
    : token_bucket_store<K>()
    , _config(agetime.count(), capacity) {
  }
    
  virtual ~mem_token_bucket_store() {
  }

  virtual void close() {
  }

  /**
  * Adds count to bucket
  * returns true if bucket has capacity
  */
  virtual bool consume(const K& key, int64_t timestamp) {
    typename std::map<K, std::shared_ptr<bucket>>::iterator item = _buckets.find(key);
    if (item == _buckets.end()) {
      auto b = std::make_shared<bucket>(_config.capacity);
      bool res = b->consume_one(&_config, timestamp);
      _buckets[key] = b;
      return res;
    }
    return item->second->consume_one(&_config, timestamp);
  }
  /**
  * Deletes a counter
  */
  virtual void del(const K& key) {
    _buckets.erase(key);
  }
  
  /**
  * Returns the counter for the given key
  */
  virtual size_t get(const K& key) {
    typename std::map<K, std::shared_ptr<bucket>>::iterator item = _buckets.find(key);
    return (item == _buckets.end()) ? _config.capacity : item->second->token();
  }

  virtual size_t size() const {
    return _buckets.size();
  }

  /**
  * erases all counters
  */
  virtual void erase() {
    _buckets.clear();
  }

  //virtual typename kspp::materialized_source<K, size_t>::iterator begin(void) {}
  //virtual typename kspp::materialized_source<K, size_t>::iterator end() {}


  protected:

  struct config
  {
    config(int64_t filltime_, size_t capacity_)
      : filltime(filltime_)
      , capacity(capacity_)
      , min_tick(std::max<int64_t>(1, filltime_/capacity_))
      , fillrate_per_ms(((double) capacity)/filltime) {
    }
    
    int64_t filltime;
    size_t  capacity;
    int64_t min_tick;
    double  fillrate_per_ms;
  };

  class bucket
  {
    public:
    bucket(size_t capacity)
      : _tokens(capacity)
      , _tstamp(0) {
    }

    inline bool consume_one(const config* conf, int64_t ts) {
      __age(conf, ts);
      if (_tokens ==0)
        return false;
      _tokens -= 1;
      return true;
    }

    inline size_t token() const {
      return _tokens;
    }

    protected:
    void __age(const config* conf, int64_t ts) {
      auto delta_ts = ts - _tstamp;
      size_t delta_count = (size_t) (delta_ts * conf->fillrate_per_ms);
      if (delta_count>0) { // no ageing on negative deltas
        _tstamp = ts;
        _tokens = std::min<size_t>(conf->capacity, _tokens + delta_count);
      }
    }

    //virtual typename kspp::materialized_source<K, size_t>::iterator begin(void) = 0;
    //virtual typename kspp::materialized_source<K, size_t>::iterator end() = 0;
    // global

    size_t  _tokens;
    int64_t _tstamp;
  }; // class bucket

  protected:
  const config                         _config;
  std::map<K, std::shared_ptr<bucket>> _buckets;
};

};