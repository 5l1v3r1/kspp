#include <memory>
#include <deque>
#include <functional>
#include <boost/optional.hpp>
#include <kspp/kspp.h>


#pragma once

namespace kspp {
  template<class LEFT, class RIGHT>
  class left_join{
  public:
    typedef std::pair<LEFT, boost::optional<RIGHT>> value_type;
  };

  template<class LEFT, class RIGHT>
  class inner_join {
  public:
    typedef std::pair<LEFT, RIGHT> value_type;
  };

  template<class LEFT, class RIGHT>
  class outer_join {
  public:
    typedef std::pair<boost::optional<LEFT>, boost::optional<RIGHT>> value_type;
  };

  template<class K, class LEFT, class RIGHT>
  class kstream_left_join : public event_consumer<K, LEFT>, public partition_source<K, typename left_join<LEFT, RIGHT>::value_type> {
  public:
    typedef typename left_join<LEFT, RIGHT>::value_type value_type;

    kstream_left_join(topology &t,
              std::shared_ptr<partition_source<K, LEFT>> left,
    std::shared_ptr<materialized_source<K, RIGHT>> right)
    : event_consumer<K, LEFT>()
    , partition_source<K, value_type>(left.get(), left->partition())
    , _left_stream (left)
    , _right_table(right) {
      _left_stream->add_sink([this](auto r) { this->_queue.push_back(r); });
      this->add_metric(&_lag);
    }

    ~kstream_left_join() {
      close();
    }

    std::string simple_name() const override {
      return "kstream_left_join";
    }

    void start(int64_t offset) override {
       _left_stream->start(offset);
       _right_table->start(offset);
      }

    void close() override {
      _left_stream->close();
      _right_table->close();
    }

    size_t queue_size() const override {
      return event_consumer<K, LEFT>::queue_size();
    }

    size_t process(int64_t tick) override {
      if (_right_table->process(tick) > 0)
        _right_table->commit(false);

      _left_stream->process(tick);

      size_t processed = 0;
      // reuse event time & commit it from event stream
      while (this->_queue.next_event_time() <= tick) {
        auto left = this->_queue.pop_and_get();
        _lag.add_event_time(tick, left->event_time());
        ++processed;
        // null values from left should be ignored
        if (left->record() && left->record()->value()) {
          auto right_record = _right_table->get(left->record()->key());

          boost::optional<RIGHT> right_val;
          if (right_record && right_record->value())
            right_val = *right_record->value();

          auto value = std::make_shared<value_type>(*left->record()->value(), right_val);
          auto record = std::make_shared<krecord<K, value_type>>(left->record()->key(), value, left->event_time());
          this->send_to_sinks(std::make_shared<kspp::kevent<K, value_type>>(record, left->id()));
        } else {
          // no output on left null
        }
      }
      return processed;
    }

    void commit(bool flush) override {
      _right_table->commit(flush);
      _left_stream->commit(flush);
    }

    bool eof() const override {
      return _right_table->eof() && _left_stream->eof();
    }

  private:
    std::shared_ptr<partition_source<K, LEFT>>   _left_stream;
    std::shared_ptr<materialized_source<K, RIGHT>> _right_table;
    metric_lag _lag;
  };

  template<class K, class LEFT, class RIGHT>
  class kstream_inner_join : public event_consumer<K, LEFT>, public partition_source<K, typename inner_join<LEFT, RIGHT>::value_type> {
  public:
    typedef typename inner_join<LEFT, RIGHT>::value_type value_type;

    kstream_inner_join(topology &t,
               std::shared_ptr<partition_source<K, LEFT>>
               left,
               std::shared_ptr<materialized_source<K, RIGHT>> right)
    : event_consumer<K, LEFT>()
    , partition_source<K, value_type>(left.get(), left->partition())
    , _left_stream (left)
    , _right_table(right) {
      _left_stream->add_sink([this](auto r) { this->_queue.push_back(r); });
      this->add_metric(&_lag);
    }

    ~kstream_inner_join() {
      close();
    }

    std::string simple_name() const override {
      return "kstream_inner_join";
    }

    void start(int64_t offset) override {
      _left_stream->start(offset);
      _right_table->start(offset);
    }

    void close() override {
      _right_table->close();
      _left_stream->close();
    }

    size_t queue_size() const override {
      return event_consumer<K, LEFT >::queue_size();
    }

    size_t process(int64_t tick) override {
      if (_right_table->process(tick) > 0)
        _right_table->commit(false);

      _left_stream->process(tick);

      size_t processed = 0;
      // reuse event time & commit it from event stream
      while (this->_queue.next_event_time() <= tick) {
        auto left = this->_queue.pop_and_get();
        _lag.add_event_time(tick, left->event_time());
        ++processed;
        // null values from left should be ignored
        if (left->record() && left->record()->value()) {
          auto right_record = _right_table->get(left->record()->key());
          // null values from right should be ignored
          if (right_record && right_record->value()) {
            //auto right_val = std::make_shared<tableV>(*right_record->value());
            auto value = std::make_shared<value_type>(*left->record()->value(), *right_record->value());
            auto record = std::make_shared<krecord<K, value_type>>(left->record()->key(), value, left->event_time());
            this->send_to_sinks(std::make_shared<kspp::kevent<K, value_type>>(record, left->id()));
          }
        } else {
          // no output on left null
        }
      }
      return processed;
    }

    void commit(bool flush) override {
      _right_table->commit(flush);
      _left_stream->commit(flush);
    }

    bool eof() const override {
      return _right_table->eof() && _left_stream->eof();
    }

  private:
    std::shared_ptr<partition_source<K, LEFT>>   _left_stream;
    std::shared_ptr<materialized_source<K, RIGHT>> _right_table;
    metric_lag _lag;
  };


  template<class K, class LEFT, class RIGHT>
  class ktable_left_join
      : public event_consumer<K, LEFT>, public partition_source<K, typename left_join<LEFT, RIGHT>::value_type> {
  public:
    typedef typename left_join<LEFT, RIGHT>::value_type value_type;

    ktable_left_join(topology &t,
                     std::shared_ptr<materialized_source < K, LEFT>> left,
    std::shared_ptr<materialized_source<K, RIGHT>> right)
    : event_consumer<K, LEFT>()
    , partition_source<K, value_type>(left.get(), left->partition())
    , _left_table (left)
    , _right_table(right) {
      _left_table->add_sink([this](auto r) { this->_queue.push_back(r); });
      _right_table->add_sink([this](auto r) { this->_queue.push_back(r); });
      this->add_metric(&_lag);
    }

    ~ktable_left_join() {
      close();
    }

    std::string simple_name() const override {
      return "ktable_left_join";
    }

    void start(int64_t offset) override {
      // if we request begin - should we restart table here???
      //it seems that we should retain whatever's in the cache in as many cases as possible
      _right_table->start(offset);
      _left_table->start(offset);
    }

    void close() override {
      _right_table->close();
      _left_table->close();
    }

    size_t queue_size() const override {
      return event_consumer<K, LEFT > ::queue_size();
    }

    size_t process(int64_t tick) override {
      _right_table->process(tick);
      _left_table->process(tick);

      size_t processed = 0;
      // reuse event time & commit it from event stream
      //
      while (this->_queue.next_event_time() <= tick) {
        auto ev = this->_queue.pop_and_get();
        _lag.add_event_time(tick, ev->event_time());
        ++processed;
        // null values from left should be ignored

        auto left_record = _left_table->get(ev->record()->key());

        if (left_record && left_record->value()) {
          //std::shared_ptr<leftV> left_val = std::make_shared<leftV>(*left_record->value());

          auto right_record = _right_table->get(ev->record()->key());
          boost::optional<RIGHT> right_val;
          if (right_record && right_record->value())
            right_val = *right_record->value();

          auto value = std::make_shared<value_type>(*left_record->value(), right_val);
          auto record = std::make_shared<krecord<K, value_type>>(ev->record()->key(), value, ev->event_time());
          this->send_to_sinks(std::make_shared<kspp::kevent<K, value_type>>(record, ev->id()));
        } else {
          // null output if left is not found
          auto record = std::make_shared<krecord<K, value_type>>(ev->record()->key(), nullptr, ev->event_time());
          this->send_to_sinks(std::make_shared<kspp::kevent<K, value_type>>(record, ev->id()));
        }
      }
      return processed;
    }

    void commit(bool flush) override {
      _right_table->commit(flush);
      _left_table->commit(flush);
    }

    bool eof() const override {
      return _right_table->eof() && _left_table->eof();
    }

  private:
    std::shared_ptr<materialized_source<K, LEFT>>  _left_table;
    std::shared_ptr<materialized_source<K, RIGHT>> _right_table;
    metric_lag _lag;
  };


  template<class K, class LEFT, class RIGHT>
  class ktable_inner_join : public event_consumer<K, LEFT>, public partition_source<K, typename inner_join<LEFT, RIGHT>::value_type> {
  public:
    typedef typename inner_join<LEFT, RIGHT>::value_type value_type;

    ktable_inner_join(topology &t,
                      std::shared_ptr<materialized_source<K, LEFT>> left,
    std::shared_ptr<materialized_source<K, RIGHT>> right)
    : event_consumer<K, LEFT>()
    , partition_source<K, value_type>(left.get(), left->partition())
    , _left_table (left)
    , _right_table(right) {
      _left_table->add_sink([this](auto r)  { this->_queue.push_back(r); });
      _right_table->add_sink([this](auto r) { this->_queue.push_back(r); });
      this->add_metric(&_lag);
    }

    ~ktable_inner_join() {
      close();
    }

    std::string simple_name() const override {
      return "ktable_inner_join";
    }

    void start(int64_t offset) override {
      // if we request begin - should we restart table here???
      //it seems that we should retain whatever's in the cache in as many cases as possible
      _right_table->start(offset);
      _left_table->start(offset);
    }

    void close() override {
      _right_table->close();
      _left_table->close();
    }

    size_t queue_size() const override {
      return event_consumer<K, LEFT > ::queue_size();
    }

    size_t process(int64_t tick) override {
      _right_table->process(tick);
      _left_table->process(tick);

      size_t processed = 0;
      // reuse event time & commit it from event stream
      //
      while (this->_queue.next_event_time() <= tick) {
        auto ev = this->_queue.pop_and_get();
        _lag.add_event_time(tick, ev->event_time());
        ++processed;
        // null values from left should be ignored

        auto left_record = _left_table->get(ev->record()->key());
        auto right_record = _right_table->get(ev->record()->key());

        if (left_record && left_record->value() && right_record && right_record->value()) {
          auto value = std::make_shared<value_type>(*left_record->value(), *right_record->value());
          auto record = std::make_shared<krecord<K, value_type>>(ev->record()->key(), value, ev->event_time());
          this->send_to_sinks(std::make_shared<kspp::kevent<K, value_type>>(record, ev->id()));
        } else {
          // null output if left is not found
          auto record = std::make_shared<krecord<K, value_type>>(ev->record()->key(), nullptr, ev->event_time());
          this->send_to_sinks(std::make_shared<kspp::kevent<K, value_type>>(record, ev->id()));
        }
      }
      return processed;
    }


    void commit(bool flush) override {
      _right_table->commit(flush);
      _left_table->commit(flush);
    }

    bool eof() const override {
      return _right_table->eof() && _left_table->eof();
    }

  private:
    std::shared_ptr<materialized_source<K, LEFT>>  _left_table;
    std::shared_ptr<materialized_source<K, RIGHT>> _right_table;
    metric_lag _lag;
  };


  template<class K, class LEFT, class RIGHT>
  class ktable_outer_join
      : public event_consumer<K, LEFT>, public partition_source<K, typename outer_join<LEFT, RIGHT>::value_type> {
  public:
    typedef typename outer_join<LEFT, RIGHT>::value_type value_type;

    ktable_outer_join(topology &t, std::shared_ptr<materialized_source<K, LEFT>> left, std::shared_ptr<materialized_source<K, RIGHT>> right)
    : event_consumer<K, LEFT>()
    , partition_source<K, value_type>(left.get(), left->partition())
    , _left_table (left)
    , _right_table(right) {
      _left_table->add_sink([this](auto r)  { this->_queue.push_back(r); });
      _right_table->add_sink([this](auto r) { this->_queue.push_back(r); });
      this->add_metric(&_lag);
    }

    ~ktable_outer_join() {
      close();
    }

    std::string simple_name() const override {
      return "ktable_outer_join";
    }

    void start(int64_t offset) override {
      // if we request begin - should we restart table here???
      //it seems that we should retain whatever's in the cache in as many cases as possible
      _right_table->start(offset);
      _left_table->start(offset);
    }

    void close() override {
      _right_table->close();
      _left_table->close();
    }

    size_t queue_size() const override {
      return event_consumer<K, LEFT>::queue_size();
    }

    size_t process(int64_t tick) override {
      _right_table->process(tick);
      _left_table->process(tick);

      size_t processed = 0;
      // reuse event time & commit it from event stream
      //
      while (this->_queue.next_event_time() <= tick) {
        auto ev = this->_queue.pop_and_get();
        _lag.add_event_time(tick, ev->event_time());
        ++processed;
        // null values from left should be ignored

        auto right_record = _right_table->get(ev->record()->key());
        auto left_record = _left_table->get(ev->record()->key());

        if (right_record || left_record) {
          boost::optional<RIGHT> right_val;
          boost::optional<LEFT> left_val;

          if (right_record && right_record->value())
            right_val = *right_record->value();

          if (left_record && left_record->value())
            left_val = *left_record->value();

          auto value = std::make_shared<value_type>(left_val, right_val);
          auto record = std::make_shared<krecord<K, value_type>>(ev->record()->key(), value, ev->event_time());
          this->send_to_sinks(std::make_shared<kspp::kevent<K, value_type>>(record, ev->id()));
        } else {
          // null output if left or right is not found
          auto record = std::make_shared<krecord<K, value_type>>(ev->record()->key(), nullptr, ev->event_time());
          this->send_to_sinks(std::make_shared<kspp::kevent<K, value_type>>(record, ev->id()));
        }
      }
      return processed;
    }


    void commit(bool flush) override {
      _right_table->commit(flush);
      _left_table->commit(flush);
    }

    bool eof() const override {
      return _right_table->eof() && _left_table->eof();
    }

  private:
    std::shared_ptr<materialized_source<K, LEFT>>  _left_table;
    std::shared_ptr<materialized_source<K, RIGHT>> _right_table;
    metric_lag _lag;
  };
}
