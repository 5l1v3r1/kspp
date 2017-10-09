#include <kspp/impl/sources/kafka_consumer.h>
#include <thread>
#include <chrono>
#include <glog/logging.h>
#include <kspp/impl/kafka_utils.h>
#include <kspp/impl/rd_kafka_utils.h>
#include <kspp/kspp.h>

using namespace std::chrono_literals;
namespace kspp {
  kafka_consumer::kafka_consumer(std::shared_ptr<cluster_config> config, std::string topic, int32_t partition, std::string consumer_group)
      : _config(config)
      , _topic(topic)
      , _partition(partition)
      , _consumer_group(consumer_group)
      , _can_be_committed(-1)
      , _last_committed(-1)
      , _max_pending_commits(5000)
      , _eof(false)
      , _msg_cnt(0)
      , _msg_bytes(0)
      , _closed(false) {
    _topic_partition.push_back(RdKafka::TopicPartition::create(_topic, _partition));

    /*
* Create configuration objects
*/
    std::unique_ptr<RdKafka::Conf> conf(RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL));
    /*
    * Set configuration properties
    */
    try {
      set_broker_config(conf.get(), _config);

      set_config(conf.get(), "api.version.request", "true");
      set_config(conf.get(), "socket.nagle.disable", "true");
      set_config(conf.get(), "fetch.wait.max.ms", std::to_string(_config->get_consumer_buffering_time().count()));
      //set_config(conf.get(), "queue.buffering.max.ms", "100");
      //set_config(conf.get(), "socket.blocking.max.ms", "100");
      set_config(conf.get(), "enable.auto.commit", "false");
      set_config(conf.get(), "auto.commit.interval.ms", "5000"); // probably not needed
      set_config(conf.get(), "enable.auto.offset.store", "false");
      set_config(conf.get(), "group.id", _consumer_group);
      set_config(conf.get(), "enable.partition.eof", "true");
      set_config(conf.get(), "log.connection.close", "false");
      //set_config(conf.get(), "socket.max.fails", "1000000");
      //set_config(conf.get(), "message.send.max.retries", "1000000");// probably not needed

      // following are topic configs but they will be passed in default_topic_conf to broker config.
      std::unique_ptr<RdKafka::Conf> tconf(RdKafka::Conf::create(RdKafka::Conf::CONF_TOPIC));
      set_config(tconf.get(), "auto.offset.reset", "earliest");

      set_config(conf.get(), "default_topic_conf", tconf.get());
    }
    catch (std::invalid_argument& e) {
      LOG(FATAL) << "kafka_consumer topic:" << _topic << ":" << _partition << ", bad config " << e.what();
    }

    /*
    * Create consumer using accumulated global configuration.
    */
    std::string errstr;
    _consumer = std::unique_ptr<RdKafka::KafkaConsumer>(RdKafka::KafkaConsumer::create(conf.get(), errstr));
    if (!_consumer) {
      LOG(FATAL) << "kafka_consumer topic:" << _topic << ":" << _partition << ", failed to create consumer, reason: " << errstr;
    }
    LOG(INFO) << "kafka_consumer topic:" << _topic << ":" << _partition << ", created";
    // really try to make sure the partition & group exist before we continue
    wait_for_partition(_consumer.get(), _topic, _partition);
    //kspp::kafka::wait_for_group(brokers, consumer_group); something seems to wrong in rdkafka master.... TODO
  }

  kafka_consumer::~kafka_consumer() {
    if (!_closed)
      close();
    close();

    for (auto i : _topic_partition) // should be exactly 1
      delete i;
  }

  void kafka_consumer::close() {
    if (_closed)
      return;
    _closed = true;
    if (_consumer) {
      _consumer->close();
      LOG(INFO) << "kafka_consumer topic:" << _topic << ":" << _partition << ", closed - consumed " << _msg_cnt << " messages (" << _msg_bytes << " bytes)";
    }
    _consumer = nullptr;
  }

  void kafka_consumer::start(int64_t offset) {
    if (offset == kspp::OFFSET_STORED) {
      //just make shure we're not in for any surprises since this is a runtime variable in rdkafka...
      assert(kspp::OFFSET_STORED == RdKafka::Topic::OFFSET_STORED);

      if (kspp::kafka::group_exists(_config, _consumer_group)) {
        DLOG(INFO) << "kafka_consumer::start topic:" << _topic << ":" << _partition  << " consumer group: " << _consumer_group << " starting from OFFSET_STORED";
      } else {
        //non existing consumer group means start from beginning
        LOG(INFO) << "kafka_consumer::start topic:" << _topic << ":" << _partition  << " consumer group: " << _consumer_group << " missing, OFFSET_STORED failed -> starting from OFFSET_BEGINNING";
        offset = kspp::OFFSET_BEGINNING;
      }
    } else if (offset == kspp::OFFSET_BEGINNING) {
      DLOG(INFO) << "kafka_consumer::start topic:" << _topic << ":" << _partition  << " consumer group: " << _consumer_group << " starting from OFFSET_BEGINNING";
    } else if (offset == kspp::OFFSET_END) {
      DLOG(INFO) << "kafka_consumer::start topic:" << _topic << ":" << _partition  << " consumer group: " << _consumer_group << " starting from OFFSET_END";
    } else {
      DLOG(INFO) << "kafka_consumer::start topic:" << _topic << ":" << _partition  << " consumer group: " << _consumer_group << " starting from fixed offset: " << offset;
    }

    /*
    * Subscribe to topics
    */
    _topic_partition[0]->set_offset(offset);
    RdKafka::ErrorCode err0 = _consumer->assign(_topic_partition);
    if (err0) {
      LOG(FATAL) << "kafka_consumer topic:" << _topic << ":" << _partition << ", failed to subscribe, reason:" << RdKafka::err2str(err0);
    }

    update_eof();
  }

  /*void kafka_consumer::start() {
    if (kspp::kafka::group_exists2(_config, _consumer_group)) {
      DLOG(INFO) << "kafka_consumer::start group_exists: " <<  _consumer_group;
      start(RdKafka::Topic::OFFSET_STORED);
    }
    else {
      start(RdKafka::Topic::OFFSET_BEGINNING);
    }
  }
   */

  void kafka_consumer::stop() {
    if (_consumer) {
      RdKafka::ErrorCode err = _consumer->unassign();
      if (err) {
        LOG(FATAL) << "kafka_consumer::stop topic:"
                   << _topic
                   << ":"
                   << _partition
                   << ", failed to stop, reason:"
                   << RdKafka::err2str(err);
      }
    }
  }

  int kafka_consumer::update_eof(){
    int64_t low = 0;
    int64_t high = 0;
    RdKafka::ErrorCode ec = _consumer->query_watermark_offsets(_topic, _partition, &low, &high, 1000);
    if (ec) {
      LOG(ERROR) << "kafka_consumer topic:" << _topic << ":" << _partition << ", consumer.query_watermark_offsets failed, reason:" << RdKafka::err2str(ec);
      return ec;
    }

    if (low == high) {
      _eof = true;
      LOG(INFO) << "kafka_consumer topic:" << _topic << ":" << _partition << " [empty], eof at:" << high;
    } else {
      auto ec = _consumer->position(_topic_partition);
      if (ec) {
        LOG(ERROR) << "kafka_consumer topic:" << _topic << ":" << _partition << ", consumer.position failed, reason:" << RdKafka::err2str(ec);
        return ec;
      }
      auto cursor = _topic_partition[0]->offset();
      _eof = (cursor + 1 == high);
      LOG(INFO) << "kafka_consumer topic:" << _topic << ":" << _partition << " cursor: " << cursor << ", eof at:" << high;
    }
    return 0;
  }

  std::unique_ptr<RdKafka::Message> kafka_consumer::consume() {
    if (_closed || _consumer == nullptr) {
      LOG(ERROR) << "topic:" << _topic << ":" << _partition << ", consume failed: closed()";
      return nullptr; // already closed
    }

    std::unique_ptr<RdKafka::Message> msg(_consumer->consume(0));

    switch (msg->err()) {
      case RdKafka::ERR_NO_ERROR:
        _eof = false;
        _msg_cnt++;
        _msg_bytes += msg->len() + msg->key_len();
        return msg;

      case RdKafka::ERR__TIMED_OUT:
        break;

      case RdKafka::ERR__PARTITION_EOF:
        _eof = true;
        break;

      case RdKafka::ERR__UNKNOWN_TOPIC:
      case RdKafka::ERR__UNKNOWN_PARTITION:
        _eof = true;
        LOG(ERROR) << "kafka_consumer topic:" << _topic << ":" << _partition << ", consume failed: " << msg->errstr();
        break;

      default:
        /* Errors */
        _eof = true;
        LOG(ERROR) << "kafka_consumer topic:" << _topic << ":" << _partition << ", consume failed: " << msg->errstr();
    }
    return nullptr;
  }

  int32_t kafka_consumer::commit(int64_t offset, bool flush) {
    if (offset < 0) // not valid
      return 0;

    if (_closed || _consumer == nullptr) {
      LOG(ERROR) << "kafka_consumer topic:" << _topic << ":" << _partition << ", commit on closed consumer";
      return -1; // already closed
    }

    // you should actually write offset + 1, since a new consumer will start at offset.
    offset = offset + 1;

    if (offset == _last_committed) // already done
      return 0;

    _can_be_committed = offset;
    RdKafka::ErrorCode ec = RdKafka::ERR_NO_ERROR;
    if (flush) {
      LOG(INFO) << "kafka_consumer topic:" << _topic << ":" << _partition << ", commiting(flush)" << ", offset:" << _can_be_committed;
      _topic_partition[0]->set_offset(_can_be_committed);
      ec = _consumer->commitSync(_topic_partition);
      if (ec == RdKafka::ERR_NO_ERROR) {
        _last_committed = _can_be_committed;
      } else {
        LOG(ERROR) << "kafka_consumer topic:" << _topic << ":" << _partition << ", failed to commit, reason:" << RdKafka::err2str(ec);
      }
    } else if ((_last_committed + _max_pending_commits) < _can_be_committed) {
      DLOG(INFO) << "kafka_consumer topic:" << _topic << ":" << _partition << ", lazy commit:" << ", offset:" << _can_be_committed;
      _topic_partition[0]->set_offset(_can_be_committed);
      ec = _consumer->commitAsync(_topic_partition);
      if (ec == RdKafka::ERR_NO_ERROR) {
        _last_committed = _can_be_committed;
      } else {
        LOG(ERROR) << "kafka_consumer topic:" << _topic << ":" << _partition << ", failed to commit, reason:" << RdKafka::err2str(ec);
      }
    }
    // TBD add time based autocommit
    return ec;
  }
} // namespace