#include <functional>
#include <boost/log/trivial.hpp>
#include <kspp/kspp.h>
#include <deque>
#pragma once

namespace kspp {
  template<class SK, class SV, class RK, class RV>
  class flat_map : public partition_source<RK, RV>
  {
  public:
    typedef std::function<void(std::shared_ptr<krecord<SK, SV>> record, flat_map* self)> extractor;

    flat_map(topology_base& topology, std::shared_ptr<partition_source<SK, SV>> source, extractor f)
      : partition_source<RK, RV>(source.get(), source->partition())
      , _source(source)
      , _extractor(f)
      , _in_count("in_count") {
      _source->add_sink([this](auto r) {
        _queue.push_back(r);
      });
      this->add_metric(&_in_count);
      this->add_metric(&_lag);
    }

    ~flat_map() {
      close();
    }

    std::string name() const {
      return _source->name() + "-flat_map()[" + type_name<RK>::get() + ", " + type_name<RV>::get() + "]";
    }

    virtual std::string processor_name() const {
      return "flat_map";
    }

    virtual void start() {
      _source->start();
    }

    virtual void start(int64_t offset) {
      _source->start(offset);
    }

    virtual void close() {
      _source->close();
    }

    virtual bool process_one(int64_t tick) {
      _source->process_one(tick);
      bool processed = (_queue.size() > 0);
      while (_queue.size()) {
        auto trans = _queue.front();
        _queue.pop_front();
        _lag.add_event_time(tick, trans->event_time());
        _currrent_id = trans->id();
        _extractor(trans->record(), this);
        ++_in_count;
      }
      return processed;
    }

    virtual void commit(bool flush) {
      _source->commit(flush);
    }

    virtual bool eof() const {
      return _source->eof() && (_queue.size() == 0);
    }

    virtual size_t queue_len() {
      return _queue.size();
    }

    /**
    * use from from extractor callback
    */
    inline void push_back(std::shared_ptr<krecord<RK, RV>> record) {
      this->send_to_sinks(std::make_shared<ktransaction<RK, RV>>(record, _currrent_id));
    }

  private:
    std::shared_ptr<partition_source<SK, SV>>         _source;
    extractor                                         _extractor;
    std::shared_ptr<commit_chain::autocommit_marker>  _currrent_id;
    std::deque<std::shared_ptr<ktransaction<SK, SV>>> _queue;
    metric_counter                                    _in_count;
    metric_lag                                        _lag;
  };
}

