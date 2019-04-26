#include <chrono>
#include <memory>
#include <kspp/impl/queue.h>
#include <kspp/topology.h>
#include <kspp/avro/generic_avro.h>
#include <grpcpp/grpcpp.h>
#include <glog/logging.h>
#include <bitbouncer_streaming.grpc.pb.h>
#include "grpc_avro_schema_resolver.h"
#include "grpc_avro_serdes.h"

#pragma once

namespace kspp {
  template<class K, class V>
  class grpc_avro_consumer {
  public:
    grpc_avro_consumer(int32_t partition,
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
        , _uri(uri)
        , _api_key(api_key)
        , _secret_access_key(secret_access_key) {
      if (_offset_storage_path.size()){
        boost::filesystem::create_directories(boost::filesystem::path(_offset_storage_path).parent_path());
      }
    }

    ~grpc_avro_consumer() {
      _exit = true;
      if (!_closed)
        close();
      if (_start_running)
        _bg.join();
      LOG(INFO) << "grpc_avro_consumer " << _topic_name << " exiting";
    }

    void close(){
      _exit = true;
      if (_closed)
        return;
      _closed = true;
      LOG(INFO) << "grpc_consumer " << _topic_name << ", closed - consumed " << _msg_cnt << " messages";
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
            LOG(INFO) << "grpc_consumer " << _topic_name  << ", start(OFFSET_STORED) - > ts:" << tmp;
            _next_offset = tmp;
          } else {
            LOG(INFO) << "grpc_consumer " << _topic_name  <<", start(OFFSET_STORED), bad file " << _offset_storage_path << ", starting from OFFSET_BEGINNING";
            _next_offset = kspp::OFFSET_BEGINNING;
          }
        } else {
          LOG(INFO) << "grpc_consumer " << _topic_name  << ", start(OFFSET_STORED), missing file " << _offset_storage_path << ", starting from OFFSET_BEGINNING";
          _next_offset = kspp::OFFSET_BEGINNING;
        }
      } else if (offset == kspp::OFFSET_BEGINNING) {
        DLOG(INFO) << "grpc_consumer " << _topic_name  << " starting from OFFSET_BEGINNING";
        _next_offset = offset;
      } else if (offset == kspp::OFFSET_END) {
        DLOG(INFO) << "grpc_consumer " << _topic_name  << " starting from OFFSET_END";
        _next_offset = offset;
      } else {
        LOG(INFO) << "grpc_consumer " << _topic_name  <<  " starting from fixed offset: " << offset;
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

  private:
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

        LOG(INFO) << "connecting";
        grpc::ChannelArguments channelArgs;
        channelArgs.SetInt(GRPC_ARG_KEEPALIVE_TIME_MS, 10000);
        channelArgs.SetInt(GRPC_ARG_KEEPALIVE_TIMEOUT_MS, 10000);
        channelArgs.SetInt(GRPC_ARG_HTTP2_MIN_SENT_PING_INTERVAL_WITHOUT_DATA_MS, 10000);
        channelArgs.SetInt(GRPC_ARG_HTTP2_MAX_PINGS_WITHOUT_DATA, 0);
        auto channel_creds = grpc::SslCredentials(grpc::SslCredentialsOptions());
        _channel = grpc::CreateCustomChannel(_uri, channel_creds, channelArgs);

        _resolver = std::make_shared<grpc_avro_schema_resolver>(_channel, _api_key);
        _serdes = std::make_unique<kspp::grpc_avro_serdes>(_resolver);
        _stub = ksppstreaming::streamprovider::NewStub(_channel);

        grpc::ClientContext context;
        add_api_key_secret(context, _api_key, _secret_access_key);
        ksppstreaming::SubscriptionRequest request;
        request.set_topic(_topic_name);
        request.set_partition(_partition);
        request.set_offset(_next_offset);

        std::shared_ptr<grpc::ClientReader<ksppstreaming::SubscriptionData> > stream(_stub->Subscribe(&context, request));
        ksppstreaming::SubscriptionData reply;
        while (!_exit && stream->Read(&reply)) {
          // empty message - read again
          if ((reply.value().size() == 0) && reply.key().size() == 0) {
            _eof = reply.eof();
            LOG(INFO) << "EOF";
            continue;
          }

          _next_offset = reply.offset(); // TODO this will reconsume last read offset on disconnect but do we know what happens if we ask for an offert that does not yet exists?

          K key;
          std::shared_ptr<V> val;
          size_t r0 = _serdes->decode(reply.key_schema(), reply.key().data(), reply.key().size(), key);
          if (r0==0)
            continue;

          if (reply.value().size()>0) {
            val = std::make_shared<V>();
            auto r1 = _serdes->decode(reply.value_schema(), reply.value().data(), reply.value().size(), *val);
            if (r1==0)
              continue;
          }

          auto record = std::make_shared<krecord<K, V>>(key, val, reply.timestamp());
          // do we have one...
          auto e = std::make_shared<kevent<K, V>>(record, _commit_chain.create(reply.offset()));
          assert(e.get() != nullptr);
          ++_msg_cnt;
          // TODO - backpressure here... stopp reading if wueue gets to big...
          _incomming_msg.push_back(e);
          if (_incomming_msg.size() > 50000)
            std::this_thread::sleep_for(100ms);
        }

        if (!_exit) {
          grpc::Status status = stream->Finish();
          if (!status.ok()) {
            LOG(ERROR) << "ksppstreaming rpc failed: " << status.error_message();
          }
        }
        std::this_thread::sleep_for(10000ms);
      } // while /!exit) -> try to connect again
      _good = false;
      LOG(INFO) << "exiting thread";
    }

    volatile bool _exit=false;
    volatile bool _start_running=false;
    volatile bool _good=true;
    volatile bool _eof=false;
    bool _closed=false;

    std::thread _bg;
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
    std::unique_ptr<ksppstreaming::streamprovider::Stub> _stub;
    std::string _api_key;
    std::string _secret_access_key;
    std::unique_ptr<grpc_avro_serdes> _serdes;
  };
}

