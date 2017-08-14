#include <kspp/kspp.h>
#pragma once
namespace kspp {
  template<class K, class V>
  class delay : public partition_source<K, V> {
  public:
    typedef std::function<bool(std::shared_ptr < kevent < K, V >> record)> predicate; // return true to keep

    delay(topology_base &topology, std::shared_ptr <partition_source<K, V>> source, int ms)
            : partition_source<K, V>(source->partition()), _source(source), _delay(ms) {
      _source->add_sink([this](auto r) {
        this->_queue.push_back(r);
      });
      this->add_metric(&_lag);
    }

    ~delay() {
      close();
    }

    std::string simple_name() const override {
      return "delay";
    }

    void start() override {
      _source->start();
    }

    void start(int64_t offset) override {
      _source->start(offset);
    }

    void close() override {
      _source->close();
    }

    bool process_one(int64_t tick) override {
      if (this->_queue.size() == 0)
        _source->process_one();

      if (this->_queue.size() == 0)
        return false;

      auto r = this->_queue.front();
      _lag.add_event_time(tick, r->event_time());
      if (r->event_time + _delay > tick) {
        this->_queue.pop_front();
        this->send_to_sinks(r);
        return true;
      }

      return false;
    }

    void commit(flush) override {
      _source->commit(flush);
    }

    size_t queue_len() const override {
      return this->_queue.size();
    }

    bool eof() const override  {
      return (queue_len() == 0) && _source->eof());
    }

  private:
    std::shared_ptr <partition_source<K, V>> _source;
    int _delay;
    metric_lag _lag;
  };
} // namespace