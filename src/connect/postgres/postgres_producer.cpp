#include <kspp/connect/postgres/postgres_producer.h>
#include <chrono>
#include <memory>
#include <glog/logging.h>
#include <boost/bind.hpp>
#include <kspp/kspp.h>
#include <kspp/connect/postgres/postgres_avro_utils.h>

using namespace std::chrono_literals;

namespace kspp {
  postgres_producer::postgres_producer(std::string table,
                                       const kspp::connect::connection_params& cp,
                                       std::string id_column,
                                       std::string client_encoding,
                                       size_t max_items_in_fetch )
      :
      _bg([this] { _thread(); })
      , _connection(std::make_shared<kspp_postgres::connection>())
      , _table(table)
      , cp_(cp)
      , _id_column(id_column)
      , _client_encoding(client_encoding)
      , _max_items_in_fetch(max_items_in_fetch)
      , _msg_cnt(0)
      , _msg_bytes(0)
      , _good(true)
      , _closed(false)
      , _connected(false)
      , _table_checked(false)
      , _table_exists(false) {
    initialize();
  }

  postgres_producer::~postgres_producer(){
    _exit = true;
    if (!_closed)
      close();
    _bg.join();
    _connection->close();
    _connection = nullptr;
  }

  void postgres_producer::close(){
    if (_closed)
      return;
    _closed = true;

    if (_connection) {
      _connection->close();
      LOG(INFO) << "postgres_producer table:" << _table << ", closed - producer " << _msg_cnt << " messages (" << _msg_bytes << " bytes)";
    }

    _connected = false;
  }

  void postgres_producer::stop(){

  }

  bool postgres_producer::initialize() {
    if (_connection->connect(cp_)){
      LOG(ERROR) << "could not connect to " << cp_.host;
      return false;
    }

    if (_connection->set_client_encoding(_client_encoding)){
      LOG(ERROR) << "could not set client encoding " << _client_encoding;
      return false;
    }

    check_table_exists();

    _start_running = true;
  }


//  void postgres_producer::connect_async() {
//    DLOG(INFO) << "connecting : connect_string: " <<  _connect_string;
//    _connection->connect(_connect_string, [this](int ec) {
//      if (!ec) {
//        if (_client_encoding.size()) {
//          if (!_connection->set_client_encoding(_client_encoding))
//            LOG(ERROR) << "failed to set client encoding : " << _connection->last_error();
//        }
//
//        LOG(INFO) << "postgres connected, client_encoding: " << _connection->get_client_encoding();
//
//        _good = true;
//        _connected = true;
//        check_table_exists_async();
//      } else {
//        LOG(ERROR) << "connect failed";
//        _good = false;
//        _connected = false;
//      }
//    });
//  }

  /*
  SELECT EXISTS (
   SELECT 1
   FROM   pg_tables
   WHERE  schemaname = 'schema_name'
   AND    tablename = 'table_name'
   );
   */

  bool postgres_producer::check_table_exists() {
    std::string statement = "SELECT 1 FROM pg_tables WHERE tablename = '" + _table + "'";
    DLOG(INFO) << "exec(" + statement + ")";
    auto res = _connection->exec(statement);

    if (res.first) {
      LOG(FATAL) << statement  << " failed ec:" << res.first << " last_error: " << _connection->last_error();
      _table_checked = true;
      return false;
    }

    int tuples_in_batch = PQntuples(res.second.get());

    if(tuples_in_batch>0) {
      LOG(INFO) << _table << " exists";
      _table_exists = true;
    } else {
      LOG(INFO) << _table << " not existing - will be created later";
    }

    _table_checked = true;
    return true;
  }

  void postgres_producer::_thread() {

    while (!_exit) {
      if (_closed)
        break;

      // connected
      if (!_start_running) {
        std::this_thread::sleep_for(1s);
        continue;
      }

      // have we lost connection ?
      if (!_connection->connected()) {
        if (!_connection->connect(cp_))
        {
          std::this_thread::sleep_for(10s);
          continue;
        }

        if (!_connection->set_client_encoding(_client_encoding)){
          std::this_thread::sleep_for(10s);
          continue;
        }
      }

      if (_incomming_msg.empty()){
        std::this_thread::sleep_for(100ms);
        continue;
      }

      if (!_table_exists) {
        auto msg = _incomming_msg.front();

        //TODO verify that the data actually has the _id_column(s)

        std::string statement = avro2sql_create_table_statement(_table, _id_column, *msg->record()->value()->valid_schema());
        LOG(INFO) << "exec(" + statement + ")";
        auto res = _connection->exec(statement);

// TODO!!!!!
//        if (ec) {
//            LOG(FATAL) << statement << " failed ec:" << ec << " last_error: " << _connection->last_error();
//            _table_checked = true;
//        } else {
//          _table_exists = true;
//        };
        _table_exists = true;
      }


      auto msg = _incomming_msg.front();
      std::string statement = avro2sql_build_insert_1(_table, *msg->record()->value()->valid_schema());
      std::string upsert_part = avro2sql_build_upsert_2(_table, _id_column, *msg->record()->value()->valid_schema());

      size_t msg_in_batch = 0;
      size_t bytes_in_batch = 0;
      std::set<std::string> unique_keys_in_batch;
      event_queue<void, kspp::GenericAvro> in_batch;
      while (!_incomming_msg.empty() && msg_in_batch < _max_items_in_fetch) {
        auto msg = _incomming_msg.front();

        // we cannot have the id columns of this update more than once
        // postgres::exec failed ERROR:  ON CONFLICT DO UPDATE command cannot affect row a second time
        auto key_string = avro2sql_key_values(*msg->record()->value()->valid_schema(), _id_column,
                                              *msg->record()->value()->generic_datum());
        auto res = unique_keys_in_batch.insert(key_string);
        if (res.second == false) {
          DLOG(INFO)
              << "breaking up upsert due to 'ON CONFLICT DO UPDATE command cannot affect row a second time...' batch size: "
              << msg_in_batch;
          break;
        }

        if (msg_in_batch > 0)
          statement += ", \n";
        statement += avro2sql_values(*msg->record()->value()->valid_schema(),
                                     *msg->record()->value()->generic_datum());;
        ++msg_in_batch;
        in_batch.push_back(msg);
        _incomming_msg.pop_front();
      }
      statement += "\n";
      statement += upsert_part;
      bytes_in_batch += statement.size();
      //std::cerr << statement << std::endl;

      auto res = _connection->exec(statement);

      /*
       * if (ec) {
        LOG(FATAL) << statement << " failed ec:" << ec << " last_error: "
                   << _connection->last_error();
        return;
      }
      */

      while (!in_batch.empty()) {
        _pending_for_delete.push_back(in_batch.pop_and_get());
      }

      _msg_cnt += msg_in_batch;
      _msg_bytes += bytes_in_batch;

      }
  DLOG(INFO) << "exiting thread";
}


void postgres_producer::poll() {
  while (!_pending_for_delete.empty()) {
    _pending_for_delete.pop_front();
  }
}

}

