#include <memory>
#include <strstream>
#include <thread>
#include <glog/logging.h>
#include <kspp/kspp.h>
#include <kspp/topology.h>
#include <kspp/connect/postgres/postgres_producer.h>
#include <kspp/avro/avro_generic.h>
#pragma once

namespace kspp {
  class postgres_generic_avro_sink : public topic_sink<void, kspp::GenericAvro> {
    static constexpr const char* PROCESSOR_NAME = "postgres_avro_sink";
  public:
    postgres_generic_avro_sink(topology &t,
                                 std::string table,
                                 std::string connect_string,
                                 std::string id_column,
                                 std::shared_ptr<kspp::avro_schema_registry>);

    virtual ~postgres_generic_avro_sink() {
      close();
    }

    std::string log_name() const override {
      return PROCESSOR_NAME;
    }

    void close() override {
      if (!_exit) {
        _exit = true;
        _thread.join();
      }

      _impl.close();
    }

    bool eof() const override {
      return _incomming_msg.size()==0 && _impl.eof();
    }

    // TBD if we store last offset and end of stream offset we can use this...
    size_t queue_size() const override {
      return _incomming_msg.size();
    }

    int64_t next_event_time() const override {
      return _incomming_msg.next_event_time();
    }

    size_t process(int64_t tick) override {
      if (_incomming_msg.size() == 0)
        return 0;
      size_t processed=0;
      while(!_incomming_msg.empty()) {
        auto p = _incomming_msg.front();
        if (p==nullptr || p->event_time() > tick)
          return processed;
        _incomming_msg.pop_front();
        //this->send_to_sinks(p); // TODO not implemted yet
        ++(this->_processed_count);
        ++processed;
        this->_lag.add_event_time(tick, p->event_time());
      }
      return processed;
    }

    std::string topic() const override {
      return _impl.table();
    }


    void flush() override {
      while (!eof()) {
        process(kspp::milliseconds_since_epoch());
        poll(0);
      }
      while (true) {
        int ec=0; // TODO fixme
        //auto ec = _impl.flush(1000);
        if (ec == 0)
          break;
      }
    }

  protected:
    void parse(const PGresult* ref);
    void thread_f();

    bool _started;
    bool _exit;
    std::thread _thread;
    event_queue<void, kspp::GenericAvro> _incomming_msg;
    postgres_producer _impl;
    std::shared_ptr<kspp::avro_schema_registry> _schema_registry;
    std::shared_ptr<avro::ValidSchema> _schema;
    int32_t _schema_id;
  };
}

