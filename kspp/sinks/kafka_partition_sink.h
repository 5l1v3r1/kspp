#include <assert.h>
#include <memory>
#include <functional>
#include <sstream>
#include <kspp/kspp.h>
#include <kspp/impl/sinks/kafka_producer.h>
#pragma once

namespace kspp {
  // SINGLE PARTITION PRODUCER
  // this is just to only override the necessary key value specifications
  template<class K, class V, class CODEC>
  class kafka_partition_sink_base : public partition_sink<K, V>
  {
  protected:
    kafka_partition_sink_base(std::string brokers, std::string topic, int32_t partition, std::chrono::milliseconds max_buffering_time, std::shared_ptr<CODEC> codec)
      : partition_sink<K, V>(partition)
      , _codec(codec)
      , _impl(brokers, topic, max_buffering_time)
      , _fixed_partition(partition)
      , _in_count("in_count")
      , _lag() {
      this->add_metric(&_in_count);
      this->add_metric(&_lag);
    }

    virtual ~kafka_partition_sink_base() {
      close();
    }

    virtual std::string name() const {
      return "kafka_partition_sink(" + _impl.topic() + "#" + std::to_string(_fixed_partition) + ")-codec(" + CODEC::name() + ")[" + type_name<K>::get() + ", " + type_name<V>::get() + "]";
    }

    virtual std::string processor_name() const { return "kafka_partition_sink(" + _impl.topic() + ")"; }

    virtual void close() {
      return _impl.close();
    }

    virtual size_t queue_len() {
      return _impl.queue_len();
    }

    virtual void poll(int timeout) {
      return _impl.poll(timeout);
    }

    // pure sink cannot suck data from upstream...
    virtual bool process_one(int64_t tick) {
      return false;
    }

    virtual void commit(bool flush) {
      // noop
    }

    virtual void flush() {
      while (true) {
        auto ec = _impl.flush(1000);
        if (ec == 0)
          break;
      }
    }

    virtual bool eof() const {
      return true;
    }

  protected:
    kafka_producer          _impl;
    std::shared_ptr<CODEC>  _codec;
    size_t                  _fixed_partition;
    metric_counter          _in_count;
    metric_lag              _lag;
  };

  template<class K, class V, class CODEC>
  class kafka_partition_sink : public kafka_partition_sink_base<K, V, CODEC>
  {
  public:
    kafka_partition_sink(topology_base& topology, std::string topic, std::shared_ptr<CODEC> codec = std::make_shared<CODEC>())
      : kafka_partition_sink_base<K, V, CODEC>(topology.brokers(), topic, topology.partition(), topology.max_buffering_time(), codec) {}

  protected:
    virtual int _produce(std::shared_ptr<ktransaction<K, V>> transaction) {
      void* kp = nullptr;
      void* vp = nullptr;
      size_t ksize = 0;
      size_t vsize = 0;

      std::stringstream ks;
      ksize = this->_codec->encode(transaction->record()->key, ks);
      kp = malloc(ksize);  // must match the free in kafka_producer TBD change to new[] and a memory pool
      ks.read((char*)kp, ksize);

      if (transaction->record()->value) {
        std::stringstream vs;
        vsize = this->_codec->encode(*transaction->record()->value, vs);
        vp = malloc(vsize);  // must match the free in kafka_producer TBD change to new[] and a memory pool
        vs.read((char*)vp, vsize);
      }
      ++(this->_in_count);
      _lag.add_event_time(kspp::milliseconds_since_epoch(), transaction->event_time());
      return this->_impl.produce((uint32_t) this->_fixed_partition, kafka_producer::FREE, kp, ksize, vp, vsize, transaction->event_time(), transaction->id());
    }
  };

  // value only topic
  template<class V, class CODEC>
  class kafka_partition_sink<void, V, CODEC> : public kafka_partition_sink_base<void, V, CODEC>
  {
  public:
    kafka_partition_sink(topology_base& topology, std::string topic, std::shared_ptr<CODEC> codec = std::make_shared<CODEC>())
      : kafka_partition_sink_base<void, V, CODEC>(topology.brokers(), topic, topology.partition(), topology.max_buffering_time(), codec) {}

  protected:
    virtual int _produce(std::shared_ptr<ktransaction<void, V>> transaction) {
      void* vp = nullptr;
      size_t vsize = 0;

      if (transaction->record()->value) {
        std::stringstream vs;
        vsize = this->_codec->encode(*transaction->record()->value, vs);
        vp = malloc(vsize);  // must match the free in kafka_producer TBD change to new[] and a memory pool
        vs.read((char*)vp, vsize);
      }
      ++(this->_in_count);
      _lag.add_event_time(kspp::milliseconds_since_epoch(), transaction->event_time());
      return this->_impl.produce((uint32_t) this->_fixed_partition, kafka_producer::FREE, nullptr, 0, vp, vsize, transaction->event_time(), transaction->id());
    }
  };

  // key only topic
  template<class K, class CODEC>
  class kafka_partition_sink<K, void, CODEC> : public kafka_partition_sink_base<K, void, CODEC>
  {
  public:
    kafka_partition_sink(topology_base& topology, std::string topic, std::shared_ptr<CODEC> codec = std::make_shared<CODEC>())
      : kafka_partition_sink_base<K, void, CODEC>(topology.brokers(), topic, topology.partition(), topology.max_buffering_time(), codec) {}

  protected:
    virtual int _produce(std::shared_ptr<ktransaction<K, void>> transaction) {
      void* kp = nullptr;
      size_t ksize = 0;

      std::stringstream ks;
      ksize = this->_codec->encode(transaction->record()->key, ks);
      kp = malloc(ksize);  // must match the free in kafka_producer TBD change to new[] and a memory pool
      ks.read((char*)kp, ksize);
      ++(this->_in_count);
      _lag.add_event_time(kspp::milliseconds_since_epoch(), transaction->event_time());
      return this->_impl.produce((uint32_t) this->_fixed_partition, kafka_producer::FREE, kp, ksize, nullptr, 0, transaction->event_time(), transaction->id());
    }
  };
};

