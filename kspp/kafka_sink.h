#include <assert.h>
#include <memory>
#include <functional>
#include <sstream>
#include "kspp_defs.h"
#include "kafka_producer.h"
#pragma once

namespace csi {

  template<class K>
  class kafka_partitioner_base
  {
  public:
    using partitioner = typename std::function<uint32_t(const K& key)>;
  };

  template<>
  class kafka_partitioner_base<void>
  {
  public:
    using partitioner = typename std::function<uint32_t(void)>;
  };

  template<class K, class V, class CODEC>
  class kafka_sink_base : public topic_sink<K, V, CODEC>
  {
  public:
    enum { MAX_KEY_SIZE = 1000 };

    using partitioner = typename kafka_partitioner_base<K>::partitioner;

    virtual ~kafka_sink_base() {
      close();
    }

    virtual std::string name() const {
      return "kafka_sink-" + _impl.topic();
    }

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
    virtual bool process_one() {
      return false;
    }
  protected:
    kafka_sink_base(std::string brokers, std::string topic, partitioner p, std::shared_ptr<CODEC> codec = std::make_shared<CODEC>())
      : topic_sink(codec)
      , _impl(brokers, topic)
      , _partitioner(p) {}

    kafka_sink_base(std::string brokers, std::string topic, std::shared_ptr<CODEC> codec = std::make_shared<CODEC>())
      : topic_sink(codec)
      , _impl(brokers, topic) {}

    kafka_producer          _impl;
    partitioner             _partitioner;
  };


  template<class K, class V, class CODEC>
  class kafka_sink : public kafka_sink_base<K, V, CODEC>
  {
  public:
    enum { MAX_KEY_SIZE = 1000 };

    using partitioner = typename kafka_partitioner_base<K>::partitioner;

    kafka_sink(std::string brokers, std::string topic, partitioner p, std::shared_ptr<CODEC> codec = std::make_shared<CODEC>())
      : kafka_sink_base(brokers, topic, p, codec) {}

    kafka_sink(std::string brokers, std::string topic, std::shared_ptr<CODEC> codec = std::make_shared<CODEC>())
      : kafka_sink_base(brokers, topic, codec) {}

    virtual int produce(std::shared_ptr<krecord<K, V>> r) {
      if (_partitioner)
        return produce(_partitioner(r->key), r);
      else
        return produce(get_partition_hash(r->key, codec()), r);
    }

    virtual int produce(uint32_t partition, std::shared_ptr<krecord<K, V>> r) {
      void* kp = NULL;
      void* vp = NULL;
      size_t ksize = 0;
      size_t vsize = 0;

      std::stringstream ks;
      ksize = _codec->encode(r->key, ks);
      kp = malloc(ksize);
      ks.read((char*)kp, ksize);

      if (r->value) {
        std::stringstream vs;
        vsize = _codec->encode(*r->value, vs);
        vp = malloc(vsize);
        vs.read((char*)vp, vsize);
      }
      return _impl.produce(partition, kafka_producer::FREE, kp, ksize, vp, vsize);
    }
  };

  //<null, VALUE>
  template<class V, class CODEC>
  class kafka_sink<void, V, CODEC> : public kafka_sink_base<void, V, CODEC>
  {
  public:
    kafka_sink(std::string brokers, std::string topic, std::shared_ptr<CODEC> codec = std::make_shared<CODEC>())
      : kafka_sink_base(brokers, topic, partition, codec) {}

    virtual int produce(uint32_t partition, std::shared_ptr<krecord<void, V>> r) {
      void* vp = NULL;
      size_t vsize = 0;

      if (r->value) {
        std::stringstream vs;
        vsize = _codec->encode(*r->value, vs);
        vp = malloc(vsize);
        vs.read((char*)vp, vsize);
      }
      return _impl.produce(partition, kafka_producer::FREE, NULL, 0, vp, vsize);
    }
  };

  // <key, NULL>
  template<class K, class CODEC>
  class kafka_sink<K, void, CODEC> : public kafka_sink_base<K, void, CODEC>
  {
  public:
    using partitioner = typename kafka_partitioner_base<K>::partitioner;

    kafka_sink(std::string brokers, std::string topic, partitioner p, std::shared_ptr<CODEC> codec = std::make_shared<CODEC>()) :
      : kafka_sink_base(brokers, topic, p, codec) {}

    kafka_sink(std::string brokers, std::string topic, std::shared_ptr<CODEC> codec = std::make_shared<CODEC>()) :
      : kafka_sink_base(brokers, topic, codec) {}

    virtual int produce(std::shared_ptr<krecord<K, void>> r) {
      void* kp = NULL;
      size_t ksize = 0;

      std::stringstream ks;
      ksize = _codec->encode(r->key, ks);
      kp = malloc(ksize);
      ks.read((char*)kp, ksize);

      if (_partitioner)
        return produce(_partitioner(r->key), r);
      else
        return produce(get_partition_hash(r->key, codec()), r);
    }

    virtual int produce(uint32_t partition, std::shared_ptr<krecord<K, void>> r) {
      void* kp = NULL;
      size_t ksize = 0;

      std::stringstream ks;
      ksize = _codec->encode(r->key, ks);
      kp = malloc(ksize);
      ks.read((char*)kp, ksize);
      return _impl.produce(partition, kafka_producer::FREE, kp, ksize, NULL, 0);
    }
  };


  // SINGLE PARTITION PRODUCER

  // this is just to only override the nessesary key value specifications
  template<class K, class V, class CODEC>
  class kafka_single_partition_sink_base : public partition_sink<K, V>
  {
  protected:
    kafka_single_partition_sink_base(std::string brokers, std::string topic, size_t partition, std::shared_ptr<CODEC> codec = std::make_shared<CODEC>())
      : partition_sink(partition)
      , _codec(codec)
      , _impl(brokers, topic)
      , _fixed_partition(partition) {}

    virtual ~kafka_single_partition_sink_base() {
      close();
    }

    virtual std::string name() const {
      return "kafka_sink-" + _impl.topic() + "_" + std::to_string(_fixed_partition);
    }

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
    virtual bool process_one() {
      return false;
    }

  protected:
    kafka_producer          _impl;
    std::shared_ptr<CODEC>  _codec;
    size_t                  _fixed_partition;
  };

  template<class K, class V, class CODEC>
  class kafka_single_partition_sink : public kafka_single_partition_sink_base<K, V, CODEC>
  {
  public:
    kafka_single_partition_sink(std::string brokers, std::string topic, size_t partition, std::shared_ptr<CODEC> codec = std::make_shared<CODEC>())
      : kafka_single_partition_sink_base(brokers, topic, partition, codec) {}

    virtual int produce(std::shared_ptr<krecord<K, V>> r) {
      void* kp = NULL;
      void* vp = NULL;
      size_t ksize = 0;
      size_t vsize = 0;

      std::stringstream ks;
      ksize = _codec->encode(r->key, ks);
      kp = malloc(ksize);
      ks.read((char*)kp, ksize);

      if (r->value) {
        std::stringstream vs;
        vsize = _codec->encode(*r->value, vs);
        vp = malloc(vsize);
        vs.read((char*)vp, vsize);
      }
      return _impl.produce((uint32_t) _fixed_partition, kafka_producer::FREE, kp, ksize, vp, vsize);
    }
  };

  // value only topic
  template<class V, class CODEC>
  class kafka_single_partition_sink<void, V, CODEC> : public kafka_single_partition_sink_base<void, V, CODEC>
  {
  public:
    kafka_single_partition_sink(std::string brokers, std::string topic, size_t partition, std::shared_ptr<CODEC> codec = std::make_shared<CODEC>())
      : kafka_single_partition_sink_base(brokers, topic, partition, codec) {}

    virtual int produce(std::shared_ptr<krecord<void, V>> r) {
      void* vp = NULL;
      size_t vsize = 0;

      if (r->value) {
        std::stringstream vs;
        vsize = _codec->encode(*r->value, vs);
        vp = malloc(vsize);
        vs.read((char*)vp, vsize);
      }
      return _impl.produce((uint32_t) _fixed_partition, kafka_producer::FREE, NULL, 0, vp, vsize);
    }
  };

  // key only topic
  template<class K, class CODEC>
  class kafka_single_partition_sink<K, void, CODEC> : public kafka_single_partition_sink_base<K, void, CODEC>
  {
  public:
    kafka_single_partition_sink(std::string brokers, std::string topic, size_t partition, std::shared_ptr<CODEC> codec = std::make_shared<CODEC>())
      : kafka_single_partition_sink_base(brokers, topic, partition, codec) {}

    virtual int produce(std::shared_ptr<krecord<K, void>> r) {
      void* kp = NULL;
      size_t ksize = 0;

      std::stringstream ks;
      ksize = _codec->encode(r->key, ks);
      kp = malloc(ksize);
      ks.read((char*)kp, ksize);
      return _impl.produce((uint32_t)  _fixed_partition, kafka_producer::FREE, kp, ksize, NULL, 0);
    }
  };
};

