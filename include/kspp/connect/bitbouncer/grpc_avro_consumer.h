#include <chrono>
#include <memory>
#include <kspp/impl/queue.h>
#include <kspp/topology.h>
#include <kspp/avro/generic_avro.h>
#include <grpcpp/grpcpp.h>
#include <glog/logging.h>
#include <bb_streaming.grpc.pb.h>
#include "grpc_avro_schema_resolver.h"
#include "grpc_avro_serdes.h"

#pragma once

namespace kspp {
  static auto s_null_schema = std::make_shared<const avro::ValidSchema>(avro::compileJsonSchemaFromString("{\"type\":\"null\"}"));

  template<class K, class V>
  class grpc_avro_consumer_base {
  public:
    grpc_avro_consumer_base(int32_t partition,
                       std::string topic_name,
                       std::string offset_storage_path,
                       std::string uri,
                       std::string api_key,
                       std::string secret_access_key)
        : _offset_storage_path(offset_storage_path)
        , _topic_name(topic_name)
        , _partition(partition)
        , _commit_chain(topic_name, partition)
        , _bg([this](){_thread();})
        , _bg_ping([this](){_thread2();})
        , _uri(uri)
        , _api_key(api_key)
        , _secret_access_key(secret_access_key) {
      if (_offset_storage_path.size()){
        boost::filesystem::create_directories(boost::filesystem::path(_offset_storage_path).parent_path());
      }

      grpc::ChannelArguments channelArgs;
      kspp::set_channel_args(channelArgs);
      auto channel_creds = grpc::SslCredentials(grpc::SslCredentialsOptions());
      _channel = grpc::CreateCustomChannel(_uri, channel_creds, channelArgs);
    }

    virtual ~grpc_avro_consumer_base() {
      if (!_closed)
        close();
      //stop_thread();
      LOG(INFO) << "grpc_avro_consumer " << _topic_name << " exiting";
    }

    void close(){
      _exit = true;
      if (_closed)
        return;
      _closed = true;
      LOG(INFO) << "grpc_avro_consumer " << _topic_name << ", closed - consumed " << _msg_cnt << " messages";
    }

    inline bool eof() const {
      return (_incomming_msg.size() == 0) && _eof;
    }

    inline std::string logical_name() const {
      return _topic_name;
    }

    inline int32_t partition() const {
      return _partition;
    }

    void start(int64_t offset) {
      if (offset == kspp::OFFSET_STORED) {

        if (boost::filesystem::exists(_offset_storage_path)) {
          std::ifstream is(_offset_storage_path.generic_string(), std::ios::binary);
          int64_t tmp;
          is.read((char *) &tmp, sizeof(int64_t));
          if (is.good()) {
            // if we are rescraping we must assume that this offset were at eof
            LOG(INFO) << "grpc_avro_consumer " << _topic_name  << ", start(OFFSET_STORED) - > ts:" << tmp;
            _next_offset = tmp;
          } else {
            LOG(INFO) << "grpc_avro_consumer " << _topic_name  <<", start(OFFSET_STORED), bad file " << _offset_storage_path << ", starting from OFFSET_BEGINNING";
            _next_offset = kspp::OFFSET_BEGINNING;
          }
        } else {
          LOG(INFO) << "grpc_avro_consumer " << _topic_name  << ", start(OFFSET_STORED), missing file " << _offset_storage_path << ", starting from OFFSET_BEGINNING";
          _next_offset = kspp::OFFSET_BEGINNING;
        }
      } else if (offset == kspp::OFFSET_BEGINNING) {
        DLOG(INFO) << "grpc_avro_consumer " << _topic_name  << " starting from OFFSET_BEGINNING";
        _next_offset = offset;
      } else if (offset == kspp::OFFSET_END) {
        DLOG(INFO) << "grpc_avro_consumer " << _topic_name  << " starting from OFFSET_END";
        _next_offset = offset;
      } else {
        LOG(INFO) << "grpc_avro_consumer " << _topic_name  <<  " starting from fixed offset: " << offset;
        _next_offset = offset;
      }
      _start_running = true;
    }

    inline event_queue<K, V>& queue(){
      return _incomming_msg;
    };

    inline const event_queue<K, V>& queue() const {
      return _incomming_msg;
    };

    void commit(bool flush) {
      int64_t offset = _commit_chain.last_good_offset();
      if (offset>0)
        commit(offset, flush);
    }

    inline int64_t offset() const {
      return _commit_chain.last_good_offset();
    }

    inline bool good() const {
      return _good;
    }

  protected:
    void stop_thread(){
      _exit = true;

      if (_bg_ping.joinable())
        _bg_ping.join();

      if (_bg.joinable())
        _bg.join();
    }

    void commit(int64_t offset, bool flush) {
      _last_commited_offset = offset;
      if (flush || ((_last_commited_offset - _last_flushed_offset) > 10000)) {
        if (_last_flushed_offset != _last_commited_offset) {
          std::ofstream os(_offset_storage_path.generic_string(), std::ios::binary);
          os.write((char *) &_last_commited_offset, sizeof(int64_t));
          _last_flushed_offset = _last_commited_offset;
          os.flush();
        }
      }
    }

    void _thread() {
      using namespace std::chrono_literals;
      while (!_start_running && !_exit)
        std::this_thread::sleep_for(100ms);

      while(!_exit) {
        size_t msg_in_rpc=0;
        LOG(INFO) << "new rpc";
        _resolver = std::make_shared<grpc_avro_schema_resolver>(_channel, _api_key);
        _serdes = std::make_unique<kspp::grpc_avro_serdes>(_resolver);
        _stub = bitbouncer::streaming::streamprovider::NewStub(_channel);

        grpc::ClientContext context;
        add_api_key_secret(context, _api_key, _secret_access_key);
        bitbouncer::streaming::SubscriptionRequest request;
        request.set_topic(_topic_name);
        request.set_partition(_partition);
        request.set_offset(_next_offset);

        std::shared_ptr<grpc::ClientReader<bitbouncer::streaming::SubscriptionBundle>> stream(_stub->Subscribe(&context, request));
        bitbouncer::streaming::SubscriptionBundle reply;
        while (!_exit) {
          //backpressure
          if (_incomming_msg.size() > 50000) {
            std::this_thread::sleep_for(100ms);
            continue;
          }

          if (!stream->Read(&reply))
            break;

          ++msg_in_rpc;

          size_t sz = reply.data().size();
          for (size_t i = 0; i != sz; ++i) {
            const auto &record = reply.data(i);

            // empty message - read again
            if ((record.value().size() == 0) && record.key().size() == 0) {
              _eof = record.eof();
              continue;
            }

            _next_offset = record.offset(); // TODO this will reconsume last read offset on disconnect but do we know what happens if we ask for an offert that does not yet exists?
            auto krec = decode(record);
            if (krec==nullptr)
              continue;
            auto e = std::make_shared<kevent<K, V>>(krec, _commit_chain.create(_next_offset));
            assert(e.get() != nullptr);
            ++_msg_cnt;
            _incomming_msg.push_back(e);
          }
        }

        if (!_exit) {
          grpc::Status status = stream->Finish();
          if (!status.ok()) {
            LOG(ERROR) << "grpc_avro_consumer rpc failed: " << status.error_message();
          } else {
            LOG(INFO) << "grpc_avro_consumer rpc done ";
          }
        }
        if (!exit) {
          if (msg_in_rpc==0)
            std::this_thread::sleep_for(1000ms);

        }
      } // while /!exit) -> try to connect again
      _good = false;
      LOG(INFO) << "grpc_avro_consumer exiting thread";
    }

    void _thread2() {
      using namespace std::chrono_literals;
      while (!_start_running && !_exit)
        std::this_thread::sleep_for(100ms);

      int64_t next_ping = kspp::milliseconds_since_epoch() + 15000;

      while(true) {
        std::this_thread::sleep_for(500ms);
        if (_exit)
          break;
        if (kspp::milliseconds_since_epoch()<next_ping)
          continue;

        //LOG(INFO) << "new ping";
        next_ping = kspp::milliseconds_since_epoch() + 15000;
        _stub = bitbouncer::streaming::streamprovider::NewStub(_channel);

        grpc::ClientContext context;
        add_api_key_secret(context, _api_key, _secret_access_key);

        bitbouncer::streaming::PingRequest request;
        int64_t t0 = kspp::milliseconds_since_epoch();
        request.set_timestamp(t0);
        bitbouncer::streaming::PingReply reply;
        grpc::Status status = _stub->Ping(&context, request, &reply);

        if (!status.ok()) {
          LOG(WARNING) << "ping failed";
          continue;
        }
        int64_t t1 = kspp::milliseconds_since_epoch();
        int64_t t_avg = (t0 + t1) / 2;
        //LOG(INFO) << "ping took: " << t1 - t0 << " ms estmated ts diff of server: " << reply.timestamp() - t_avg;
      }
      LOG(INFO) << "grpc_avro_consumer exiting ping thread";
    }

    virtual std::shared_ptr<kspp::krecord<K, V>> decode(const bitbouncer::streaming::SubscriptionData& record)=0;

    volatile bool _exit=false;
    volatile bool _start_running=false;
    volatile bool _good=true;
    volatile bool _eof=false;
    bool _closed=false;

    std::thread _bg;
    std::thread _bg_ping;
    const std::string _uri;
    const std::string _topic_name;
    const int32_t _partition;

    int64_t _next_offset = kspp::OFFSET_BEGINNING; // not same as comit
    boost::filesystem::path _offset_storage_path;
    commit_chain _commit_chain;
    int64_t _last_commited_offset=0;
    int64_t _last_flushed_offset=0;
    event_queue<K, V> _incomming_msg;
    uint64_t _msg_cnt=0;
    std::shared_ptr<grpc::Channel> _channel;
    std::shared_ptr<grpc_avro_schema_resolver> _resolver;
    std::unique_ptr<bitbouncer::streaming::streamprovider::Stub> _stub;
    std::string _api_key;
    std::string _secret_access_key;
    std::unique_ptr<grpc_avro_serdes> _serdes;
  };


  template<class K, class V>
class grpc_avro_consumer : public grpc_avro_consumer_base<K, V> {
  public:
    grpc_avro_consumer(int32_t partition,
                       std::string topic_name,
                       std::string offset_storage_path,
                       std::string uri,
                       std::string api_key,
                       std::string secret_access_key)
        : grpc_avro_consumer_base<K,V>(partition, topic_name, offset_storage_path, uri, api_key, secret_access_key) {
    }

  virtual ~grpc_avro_consumer(){
    this->stop_thread();
  }

  std::shared_ptr<kspp::krecord<K, V>> decode(const bitbouncer::streaming::SubscriptionData& record) override{
     K key;
     std::shared_ptr<V> val;
     size_t r0 = this->_serdes->decode(record.key_schema(), record.key().data(), record.key().size(), key);
     if (r0 == 0)
       return nullptr;

     if (record.value().size() > 0) {
       val = std::make_shared<V>();
       auto r1 = this->_serdes->decode(record.value_schema(), record.value().data(), record.value().size(), *val);
       if (r1 == 0)
         return nullptr;
     }
     return std::make_shared<krecord<K, V>>(key, val, record.timestamp());
   }
  };

  // this is a special case of generic stuff where the key might be null
  template<class V>
  class grpc_avro_consumer<kspp::generic_avro, V> : public grpc_avro_consumer_base<kspp::generic_avro, V> {
  public:
    grpc_avro_consumer(int32_t partition,
                       std::string topic_name,
                       std::string offset_storage_path,
                       std::string uri,
                       std::string api_key,
                       std::string secret_access_key)
        : grpc_avro_consumer_base<kspp::generic_avro,V>(partition, topic_name, offset_storage_path, uri, api_key, secret_access_key) {
    }

    virtual ~grpc_avro_consumer(){
      this->stop_thread();
    }

    std::shared_ptr<kspp::krecord<kspp::generic_avro, V>> decode(const bitbouncer::streaming::SubscriptionData& record) override{
      kspp::generic_avro key;
      std::shared_ptr<V> val;
      if (record.key().size()==0) {
        key.create(s_null_schema, 0);
      } else {
        size_t r0 = this->_serdes->decode(record.key_schema(), record.key().data(), record.key().size(), key);
        if (r0 == 0)
          return nullptr;
      }

      if (record.value().size() > 0) {
        val = std::make_shared<V>();
        auto r1 = this->_serdes->decode(record.value_schema(), record.value().data(), record.value().size(), *val);
        if (r1 == 0)
          return nullptr;
      }
      return std::make_shared<krecord<kspp::generic_avro, V>>(key, val, record.timestamp());
    }
  };



  template<class V>
  class grpc_avro_consumer<void, V> : public grpc_avro_consumer_base<void, V> {
  public:
    grpc_avro_consumer(int32_t partition,
                       std::string topic_name,
                       std::string offset_storage_path,
                       std::string uri,
                       std::string api_key,
                       std::string secret_access_key)
        : grpc_avro_consumer_base<void,V>(partition, topic_name, offset_storage_path, uri, api_key, secret_access_key) {
    }

    virtual ~grpc_avro_consumer(){
      this->stop_thread();
    }

    std::shared_ptr<kspp::krecord<void, V>> decode(const bitbouncer::streaming::SubscriptionData& record) override{
      std::shared_ptr<V> val;
      if (record.value().size() > 0) {
        val = std::make_shared<V>();
        auto r1 = this->_serdes->decode(record.value_schema(), record.value().data(), record.value().size(), *val);
        if (r1 == 0)
          return nullptr;
      }
      return std::make_shared<krecord<void, V>>(val, record.timestamp());
    }
  };

  template<class K>
  class grpc_avro_consumer<K, void> : public grpc_avro_consumer_base<K, void> {
  public:
    grpc_avro_consumer(int32_t partition,
                       std::string topic_name,
                       std::string offset_storage_path,
                       std::string uri,
                       std::string api_key,
                       std::string secret_access_key)
        : grpc_avro_consumer_base<K,void>(partition, topic_name, offset_storage_path, uri, api_key, secret_access_key) {
    }

    std::shared_ptr<kspp::krecord<K, void>> decode(const bitbouncer::streaming::SubscriptionData& record) override{
      K key;
      size_t r0 = this->_serdes->decode(record.key_schema(), record.key().data(), record.key().size(), key);
      if (r0 == 0)
        return nullptr;
      return std::make_shared<krecord<K, void>>(key, record.timestamp());
    }
  };
}

