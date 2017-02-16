#include <kspp/impl/state_stores/token_bucket.h>
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
    , _token_bucket(std::make_shared<mem_token_bucket<int>>(std::chrono::milliseconds((int) (1000.0 / messages_per_sec)), 1)) {
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
      _token_bucket->erase();
  }

  virtual void close() {
    _source->close();
  }

  virtual bool process_one() {
    if (_queue.size() == 0)
      _source->process_one();

    if (_queue.size()) {
      auto r = _queue.front();
      _lag.add_event_time(r->event_time);
      if (_token_bucket->consume(0, milliseconds_since_epoch())) {
        _queue.pop_front();
        this->send_to_sinks(r);
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
  std::shared_ptr<partition_source<K, V>>    _source;
  std::deque<std::shared_ptr<krecord<K, V>>> _queue;
  std::shared_ptr<token_bucket<int>>         _token_bucket;
  metric_lag                                 _lag;
};
}; // namespace