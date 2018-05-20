#include <memory>
#include <strstream>
#include <thread>
#include <glog/logging.h>
#include <kspp/kspp.h>
#include <kspp/topology.h>
#include <kspp/connect/postgres/postgres_consumer.h>
#include <kspp/avro/avro_generic.h>

#pragma once

namespace kspp {
  class postgres_generic_avro_source : public partition_source<void, kspp::GenericAvro> {
    static constexpr const char *PROCESSOR_NAME = "postgres_avro_source";
  public:
    postgres_generic_avro_source(topology &t,
                                  int32_t partition,
                                  std::string table,
                                  std::string host,
                                  int port,
                                  std::string user,
                                  std::string password,
                                  std::string database,
                                  std::string id_column,
                                  std::string ts_column,
                                  std::shared_ptr<kspp::avro_schema_registry>,
                                  std::chrono::seconds poll_intervall,
                                  size_t max_items_in_fetch);

    virtual ~postgres_generic_avro_source() {
      close();
    }

    std::string log_name() const override {
      return PROCESSOR_NAME;
    }

    void start(int64_t offset) override {
      _impl.start(offset);
      _started = true;
    }

    void close() override {
      /*
       * if (_commit_chain.last_good_offset() >= 0 && _impl.commited() < _commit_chain.last_good_offset())
        _impl.commit(_commit_chain.last_good_offset(), true);
        */
      _impl.close();
    }

    bool eof() const override {
      return _impl.eof();
    }

    void commit(bool flush) override {
      /*
       * if (_commit_chain.last_good_offset() >= 0)
        _impl.commit(_commit_chain.last_good_offset(), flush);
        */
    }

    // TBD if we store last offset and end of stream offset we can use this...
    size_t queue_size() const override {
      return _impl.queue().size();
    }

    int64_t next_event_time() const override {
      return _impl.queue().next_event_time();
    }

    size_t process(int64_t tick) override {
      if (_impl.queue().size() == 0)
        return 0;
      size_t processed = 0;
      while (!_impl.queue().empty()) {
        auto p = _impl.queue().front();
        if (p == nullptr || p->event_time() > tick)
          return processed;
        _impl.queue().pop_front();
        this->send_to_sinks(p);
        ++(this->_processed_count);
        ++processed;
        this->_lag.add_event_time(tick, p->event_time());
      }
      return processed;
    }

    std::string topic() const override {
      return _impl.table();
    }

  protected:
    //void thread_f();
    bool _started;
    bool _exit;
    postgres_consumer _impl;
    std::shared_ptr<kspp::avro_schema_registry> _schema_registry;
    std::shared_ptr<avro::ValidSchema> _schema;
    int32_t _schema_id;
    commit_chain _commit_chain;
    metric_counter _parse_errors;
    metric_evaluator _commit_chain_size;
  };
}

