#include <chrono>
#include <memory>
#include <kspp/impl/queue.h>
#include <kspp/topology.h>
#include <kspp/avro/generic_avro.h>
#include <kspp/connect/postgres/postgres_connection.h>
#pragma once

namespace kspp {
  class postgres_consumer {
  public:
    postgres_consumer(int32_t partition,
                       std::string table,
                       std::string consumer_group,
                       const kspp::connect::connection_params& cp,
                       std::string id_column,
                       std::string ts_column,
                       std::shared_ptr<kspp::avro_schema_registry>,
                       std::chrono::seconds poll_intervall,
                       size_t max_items_in_fetch);

    ~postgres_consumer();

    bool initialize();

    void close();

    inline bool eof() const {
      return (_incomming_msg.size() == 0) && _eof;
    }

    inline std::string table() const {
      return _table;
    }

    inline int32_t partition() const {
      return _partition;
    }

    void start(int64_t offset);

    void subscribe();

    bool is_query_running() const { return !_eof; }

    inline event_queue<void, kspp::generic_avro>& queue(){
      return _incomming_msg;
    };

    inline const event_queue<void, kspp::generic_avro>& queue() const {
      return _incomming_msg;
    };

  private:
    void connect();
    int parse_response(std::shared_ptr<PGresult>);

    //int64_t parse_ts(DBPROCESS *stream);
    //int parse_avro(DBPROCESS* stream, COL* columns, size_t ncols);
    //int parse_response(DBPROCESS* stream);
    void _thread();
    bool _exit;
    bool _start_running;
    bool _good;
    bool _eof;
    bool _closed;
    std::chrono::seconds poll_intervall_;

    std::thread _bg;
    std::shared_ptr<kspp_postgres::connection> _connection;

    const std::string _table;
    const int32_t _partition;
    const std::string _consumer_group;

    const kspp::connect::connection_params cp_;

    const std::string _id_column;
    const std::string _ts_column;
    size_t _max_items_in_fetch;

    // this holds the read cursor (should be abstracted)
    int id_column_index_;
    int ts_column_index_;
    std::string last_id_;
    std::string last_ts_;


    std::shared_ptr<kspp::avro_schema_registry> schema_registry_;
    std::shared_ptr<avro::ValidSchema> schema_;
    int32_t schema_id_;
    event_queue<void, kspp::generic_avro> _incomming_msg;

    uint64_t _msg_cnt;
  };
}

