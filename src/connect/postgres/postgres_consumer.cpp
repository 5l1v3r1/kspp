#include <kspp/connect/postgres/postgres_consumer.h>
#include <kspp/kspp.h>
#include <chrono>
#include <memory>
#include <glog/logging.h>
#include <boost/bind.hpp>

using namespace std::chrono_literals;

namespace kspp {
  postgres_consumer::postgres_consumer(int32_t partition, std::string table, std::string consumer_group, std::string connect_string, std::string id_column, std::string ts_column, std::chrono::seconds poll_intervall, size_t max_items_in_fetch)
      : _exit(false)
      , _good(true)
      , _closed(false)
      , _eof(false)
      , _connected(false)
      , _fg_work(new boost::asio::io_service::work(_fg_ios))
      , _bg_work(new boost::asio::io_service::work(_bg_ios))
      , _fg(boost::bind(&boost::asio::io_service::run, &_fg_ios))
      , _bg(boost::bind(&boost::asio::io_service::run, &_bg_ios))
      , _connection(std::make_shared<postgres_asio::connection>(_fg_ios, _bg_ios))
      , _main_thread([this] { _thread(); })
      , _table(table)
      , _partition(partition)
      , _consumer_group(consumer_group)
      , _connect_string(connect_string)
      , _id_column(id_column)
      , _ts_column(ts_column)
      , ts_column_index_(-1)
      , last_ts_(0)
      , poll_intervall_(poll_intervall)
      , _max_items_in_fetch(max_items_in_fetch)
      , _can_be_committed(0)
      , _last_committed(-1)
      , _max_pending_commits(0)
      , _msg_cnt(0)
      , _msg_bytes(0) {
  }

  postgres_consumer::~postgres_consumer(){
    _exit = true;

    if (!_closed)
      close();

    _main_thread.join();

    _fg_work.reset();
    _bg_work.reset();
    //bg_ios.stop();
    //fg_ios.stop();
    _bg.join();
    _fg.join();

    _connection->close();
    _connection = nullptr;
  }

  void postgres_consumer::close(){
    _exit = true;

    if (_closed)
      return;
    _closed = true;

    if (_connection) {
      _connection->close();
      LOG(INFO) << "postgres_consumer table:" << _table << ":" << _partition << ", closed - consumed " << _msg_cnt << " messages (" << _msg_bytes << " bytes)";
    }

    _connected = false;
  }

  std::shared_ptr<PGresult> postgres_consumer::consume(){
    if (_queue.empty())
      return nullptr;
    auto p = _queue.pop_and_get();
    return p;
  }

  void postgres_consumer::start(int64_t offset){
    if (offset == kspp::OFFSET_STORED) {
      //TODO not implemented yet
      /*
       * if (_config->get_cluster_metadata()->consumer_group_exists(_consumer_group, 5s)) {
        DLOG(INFO) << "kafka_consumer::start topic:" << _topic << ":" << _partition  << " consumer group: " << _consumer_group << " starting from OFFSET_STORED";
      } else {
        //non existing consumer group means start from beginning
        LOG(INFO) << "kafka_consumer::start topic:" << _topic << ":" << _partition  << " consumer group: " << _consumer_group << " missing, OFFSET_STORED failed -> starting from OFFSET_BEGINNING";
        offset = kspp::OFFSET_BEGINNING;
      }
       */
    } else if (offset == kspp::OFFSET_BEGINNING) {
      DLOG(INFO) << "postgres_consumer::start table:" << _table << ":" << _partition  << " consumer group: " << _consumer_group << " starting from OFFSET_BEGINNING";
    } else if (offset == kspp::OFFSET_END) {
      DLOG(INFO) << "postgres_consumer::start table:" << _table << ":" << _partition  << " consumer group: " << _consumer_group << " starting from OFFSET_END";
    } else {
      DLOG(INFO) << "postgres_consumer::start table:" << _table << ":" << _partition  << " consumer group: " << _consumer_group << " starting from fixed offset: " << offset;
    }

    connect_async();
    update_eof();
  }

  void postgres_consumer::stop(){

  }

  int32_t postgres_consumer::commit(int64_t offset, bool flush){

  }

  int postgres_consumer::update_eof() {

  }

  void postgres_consumer::_thread(){
    while (!_exit) {
      if (_closed)
        break;

      // connected
      if (!_connected) {
        std::this_thread::sleep_for(1s);
        continue;
      }

      _eof = false;
      auto last_msg_count = _msg_cnt;
      _eof = true;
      LOG(INFO) << "poll done - got: " << _msg_cnt - last_msg_count << " messages, total: " << _msg_cnt << ", last ts: " << last_ts_;
      //LOG(INFO) << "waiting for next poll - sleeping " << poll_intervall_.count() << " seconds";

      // if we sleep long we cannot be killed
      int count= poll_intervall_.count();
      for (int i=0; i!=count; ++i){
        std::this_thread::sleep_for(1s);
        if (_exit)
          break;
      }

    }
    DLOG(INFO) << "exiting thread";
  }

  void postgres_consumer::connect_async() {
    DLOG(INFO) << "connecting : connect_string: " <<  _connect_string;
    _connection->connect(_connect_string, [this](int ec) {
      if (!ec) {
        DLOG(INFO) << "connected";
        bool r1 = _connection->set_client_encoding("UTF8");
        _good = true;
        _connected = true;
        _eof = true;
        select_async();
      } else {
        LOG(ERROR) << "connect failed";
        _good = false;
        _connected = false;
        _eof = true;
      }
    });
  }


  void postgres_consumer::handle_fetch_cb(int ec, std::shared_ptr<PGresult> result) {
    if (ec)
      return;
    int tuples_in_batch = PQntuples(result.get());
    _msg_cnt += tuples_in_batch;

    // should this be here?? it costs something
    size_t nFields = PQnfields(result.get());
    for (int i = 0; i < tuples_in_batch; i++)
      for (int j = 0; j < nFields; j++)
        _msg_bytes += PQgetlength(result.get(), i, j);

    if (tuples_in_batch == 0) {
      DLOG(INFO) << "query done, got total: " << _msg_cnt; // tbd remove this
      _connection->exec("CLOSE MYCURSOR; COMMIT", [](int ec, std::shared_ptr<PGresult> res) {});
      _eof = true;
      return;
    } else {
      DLOG(INFO) << "query batch done, got total: " << _msg_cnt; // tbd remove this
      _queue.push_back(result);
      _connection->exec("FETCH " + std::to_string(_max_items_in_fetch) + " IN MYCURSOR",
                        [this](int ec, std::shared_ptr<PGresult> res) {
                          this->handle_fetch_cb(ec, std::move(res));
                        });
    }
  }

  void postgres_consumer::select_async()
  {
    // connected
    if (!_connected)
      return;

    // already runnning
    if (!_eof)
      return;

    DLOG(INFO) << "exec(BEGIN)";
    _eof = false;
    _connection->exec("BEGIN", [this](int ec, std::shared_ptr<PGresult> res) {
      if (ec) {
        LOG(FATAL) << "BEGIN failed ec:" << ec << " last_error: " << _connection->last_error();
        return;
      }
      std::string fields = "*";

      std::string order_by = "";
      if (_ts_column.size())
        order_by = _ts_column + " ASC, " + _id_column + " ASC";
      else
        order_by = _id_column + " ASC";

      std::string where_clause;

      if (_ts_column.size())
        where_clause = _ts_column;
      else
        where_clause = _id_column;

      std::string statement = "DECLARE MYCURSOR CURSOR FOR SELECT " + fields + " FROM "+ _table + " ORDER BY " + order_by;
      DLOG(INFO) << "exec(" + statement + ")";
      _connection->exec(statement,
                        [this](int ec, std::shared_ptr<PGresult> res) {
                          if (ec) {
                            LOG(FATAL) << "DECLARE MYCURSOR... failed ec:" << ec << " last_error: " << _connection->last_error();
                            return;
                          }
                          _connection->exec("FETCH " + std::to_string(_max_items_in_fetch) +" IN MYCURSOR",
                                            [this](int ec, std::shared_ptr<PGresult> res) {
                                              try {
                                                /*
                                                boost::shared_ptr<avro::ValidSchema> schema(
                                                    valid_schema_for_table_row(_table  + ".value", res));
                                                boost::shared_ptr<avro::ValidSchema> key_schema(
                                                    valid_schema_for_table_key(_table + ".key", {"id"}, res));

                                                std::cerr << "key schema" << std::endl;
                                                key_schema->toJson(std::cerr);
                                                std::cerr << std::endl;

                                                std::cerr << "value schema" << std::endl;
                                                std::cerr << std::endl;
                                                schema->toJson(std::cerr);
                                                std::cerr << std::endl;
  */
                                                handle_fetch_cb(ec, std::move(res));
                                              }
                                              catch (std::exception &e) {
                                                LOG(FATAL) << "exception: " << e.what();
                                              };
                                              /*
                                              int nFields = PQnfields(res.get());
                                              for (int i = 0; i < nFields; i++)
                                                  printf("%-15s", PQfname(res.get(), i));
                                              */
                                            });
                        });
    });
  }



}

