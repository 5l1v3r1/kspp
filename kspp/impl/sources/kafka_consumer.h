#include <memory>
#include <librdkafka/rdkafkacpp.h>
#pragma once

namespace kspp {
  class kafka_consumer
  {
  public:
    kafka_consumer(std::string brokers, std::string topic, int32_t partition, std::string consumer_group="");
    ~kafka_consumer();
    void close();

    std::unique_ptr<RdKafka::Message> consume();

    inline bool eof() const {
      return _eof;
    }

    inline std::string topic() const {
      return _topic;
    }

    inline int32_t partition() const {
      return _partition;
    }

    /**
    * start from stored offset
    * if no stored offset exists, starts from beginning
    */
    void start();

    void start(int64_t offset);

    void stop();

    int32_t commit(int64_t offset, bool flush = false);

  private:
    bool init();

    const std::string                  _brokers;
    const std::string                  _topic;
    const int32_t                      _partition;
    const std::string                  _consumer_group;
    std::unique_ptr<RdKafka::Topic>    _rd_topic;
    std::unique_ptr<RdKafka::Consumer> _consumer;
    uint64_t                           _msg_cnt;
    uint64_t                           _msg_bytes;
    bool                               _eof;
    bool                               _closed;
  };
};

