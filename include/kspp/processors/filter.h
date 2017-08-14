#include <kspp/kspp.h>
#pragma once
namespace kspp {
  template<class K, class V>
  class filter : public event_consumer<K, V>, public partition_source<K, V> {
  public:
    typedef std::function<bool(std::shared_ptr<const krecord <K, V>> record)> predicate; // return true to keep

    filter(topology_base &topology, std::shared_ptr<partition_source < K, V>> source, predicate f)
    : event_consumer<K, V>()
    , partition_source<K, V>(source.get(), source->partition())
    , _source(source)
    , _predicate(f)
    , _predicate_false("predicate_false") {
      _source->add_sink([this](auto r) {
        this->_queue.push_back(r);
      });
      this->add_metric(&_lag);
      this->add_metric(&_predicate_false);
    }

    ~filter() {
      close();
    }

    std::string simple_name() const override {
      return "filter";
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
        _source->process_one(tick);
      bool processed = (this->_queue.size() > 0);
      while (this->_queue.size()) {
        auto ev = this->_queue.front();
        this->_queue.pop_front();
        _lag.add_event_time(tick, ev->event_time());
        if (_predicate(ev->record())) {
          this->send_to_sinks(ev);
        } else {
          ++_predicate_false;
        }
      }
      return processed;
    }

    void commit(bool flush) override {
      _source->commit(flush);
    }

    bool eof() const override {
      return (queue_len() == 0) && _source->eof();
    }

    size_t queue_len() const override {
      return event_consumer < K, V > ::queue_len();
    }

  private:
    std::shared_ptr<partition_source < K, V>> _source;
    predicate _predicate;
    metric_lag _lag;
    metric_counter _predicate_false;
  };
} // namespace