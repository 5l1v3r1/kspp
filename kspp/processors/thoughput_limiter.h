#include <kspp/state_stores/mem_token_bucket_store.h>
#include <chrono>
#pragma once

// this should be a template on storage type 
// ie mem_bucket or rocksdb version
// or virtual ptr to storage to be passed in constructor
// right now this is processing time rate limiting 
// how do we swap betweeen processing and event time??? TBD
namespace kspp {
template<class K, class V>
class thoughput_limiter : public partition_source<K, V>
{
  public:
  thoughput_limiter(topology_base& topology, std::shared_ptr<partition_source<K, V>> source, double messages_per_sec)
    : partition_source<K, V>(source.get(), source->partition())
    , _source(source)
    , _token_bucket(std::make_shared<mem_token_bucket_store<int, size_t>>(std::chrono::milliseconds((int) (1000.0 / messages_per_sec)), 1)) {
    _source->add_sink([this](auto r) {
      _queue.push_back(r);
    });
  }

  ~thoughput_limiter() {
    close();
  }

  virtual std::string processor_name() const {
    return "thoughput_limiter";
  }

  std::string name() const {
    return _source->name() + "-thoughput_limiter";
  }

  virtual void start() {
    _source->start();
  }

  virtual void start(int64_t offset) {
    _source->start(offset);

    if (offset == -2)
      _token_bucket->clear();
  }

  virtual void close() {
    _source->close();
  }

  virtual bool process_one(int64_t tick) {
    if (_queue.size() == 0)
      _source->process_one(tick);

    if (_queue.size()) {
      auto trans = _queue.front();
      _lag.add_event_time(tick, trans->event_time());
      if (_token_bucket->consume(0, tick)) {
        _queue.pop_front();
        this->send_to_sinks(trans);
        return true;
      }
    }
    return false;
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

  private:
  std::shared_ptr<partition_source<K, V>>              _source;
  event_queue<kevent<K, V>>                            _queue;
  std::shared_ptr<mem_token_bucket_store<int, size_t>> _token_bucket;
  metric_lag                                           _lag;
};
}; // namespace