#include <kspp/connect/postgres/postgres_consumer.h>
#include <kspp/kspp.h>
#include <chrono>
#include <memory>
#include <glog/logging.h>
#include <boost/bind.hpp>
#include <kspp/connect/postgres/postgres_avro_utils.h>

using namespace std::chrono_literals;

namespace kspp {
  postgres_consumer::postgres_consumer(int32_t partition,
                                         std::string table,
                                         std::string consumer_group,
                                         const kspp::connect::connection_params& cp,
                                         std::string id_column,
                                         std::string ts_column,
                                         std::shared_ptr<kspp::avro_schema_registry> schema_registry,
                                         std::chrono::seconds poll_intervall,
                                         size_t max_items_in_fetch)
      : _good(true)
      , _closed(false)
      , _eof(false)
      , _start_running(false)
      , _exit(false)
      , _bg([this] { _thread(); })
      , _connection(std::make_shared<kspp_postgres::connection>())
      , _table(table)
      , _partition(partition)
      , _consumer_group(consumer_group)
      , cp_(cp)
      , _id_column(id_column)
      , _ts_column(ts_column)
      , id_column_index_(-1)
      , ts_column_index_(-1)
      , schema_registry_(schema_registry)
      , _max_items_in_fetch(max_items_in_fetch)
      , schema_id_(-1)
      , poll_intervall_(poll_intervall)
      , _msg_cnt(0) {
  }

  postgres_consumer::~postgres_consumer() {
    _exit = true;
    if (!_closed)
      close();
    _bg.join();
    _connection->close();
    _connection = nullptr;
  }

  void postgres_consumer::close() {
    _exit = true;
    _start_running = false;

    if (_closed)
      return;
    _closed = true;

    if (_connection) {
      _connection->close();
      LOG(INFO) << "postgres_consumer table:" << _table << ":" << _partition << ", closed - consumed " << _msg_cnt << " messages";
    }
  }

  bool postgres_consumer::initialize() {
    if (_connection->connect(cp_)){
      LOG(ERROR) << "could not connect to " << cp_.host;
      return false;
    }

    if (_connection->set_client_encoding("UTF8")){
      LOG(ERROR) << "could not set client encoding UTF8 ";
      return false;
    }

    //should we check more thing in database
    //maybe select a row and register the schema???

    _start_running = true;
  }

  void postgres_consumer::start(int64_t offset) {
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
      DLOG(INFO) << "postgres_consumer::start table:" << _table << ":" << _partition << " consumer group: "
                 << _consumer_group << " starting from OFFSET_BEGINNING";
    } else if (offset == kspp::OFFSET_END) {
      DLOG(INFO) << "postgres_consumer::start table:" << _table << ":" << _partition << " consumer group: "
                 << _consumer_group << " starting from OFFSET_END";
    } else {
      DLOG(INFO) << "postgres_consumer::start table:" << _table << ":" << _partition << " consumer group: "
                 << _consumer_group << " starting from fixed offset: " << offset;
    }

    initialize();
  }

  int postgres_consumer::parse_response(std::shared_ptr<PGresult> result){
    if (!result)
      return -1;

    // first time?
    if (!schema_) {
      this->schema_ = std::make_shared<avro::ValidSchema>(*schema_for_table_row(_table, result.get()));

      if (schema_registry_) {
        // we should probably prepend the name with a prefix (like _my_db_table_name)
        schema_id_ = schema_registry_->put_schema(_table, schema_);
      }

      // print schema first time...
      std::stringstream ss;
      this->schema_->toJson(ss);
      LOG(INFO) << "schema: " << ss.str();

      // TODO this could be an array of columns
      id_column_index_ = PQfnumber(result.get(), _id_column.c_str());
      ts_column_index_ = PQfnumber(result.get(), _ts_column.c_str());




    }

    int nRows = PQntuples(result.get());

    for (int i = 0; i < nRows; i++)
    {
      auto gd = std::make_shared<kspp::generic_avro>(schema_, schema_id_);
      assert(gd->type() == avro::AVRO_RECORD);
      avro::GenericRecord& record(gd->generic_datum()->value<avro::GenericRecord>());
      size_t nFields = record.fieldCount();
      for (int j = 0; j < nFields; j++)
      {
        if (record.fieldAt(j).type() != avro::AVRO_UNION)
        {
          LOG(FATAL) << "unexpected schema - bailing out, type:" << record.fieldAt(j).type();
          break;
        }
        avro::GenericUnion& au(record.fieldAt(j).value<avro::GenericUnion>());

        const std::string& column_name = record.schema()->nameAt(j);

        //which pg column has this value?
        int column_index = PQfnumber(result.get(), column_name.c_str());
        if (column_index < 0)
        {
          LOG(FATAL) << "unknown column - bailing out: " << column_name;
          break;
        }

        if (PQgetisnull(result.get(), i, column_index) == 1)
        {
          au.selectBranch(0); // NULL branch - we hope..
          assert(au.datum().type() == avro::AVRO_NULL);
        }
        else
        {
          au.selectBranch(1);
          avro::GenericDatum& avro_item(au.datum());
          const char* val = PQgetvalue(result.get(), i, j);

          switch (avro_item.type())
          {
            case avro::AVRO_STRING:
              avro_item.value<std::string>() = val;
              break;
            case avro::AVRO_BYTES:
              avro_item.value<std::string>() = val;
              break;
            case avro::AVRO_INT:
              avro_item.value<int32_t>() = atoi(val);
              break;
            case avro::AVRO_LONG:
              avro_item.value<int64_t>() = std::stoull(val);
              break;
            case avro::AVRO_FLOAT:
              avro_item.value<float>() = (float)atof(val);
              break;
            case avro::AVRO_DOUBLE:
              avro_item.value<double>() = atof(val);
              break;
            case avro::AVRO_BOOL:
              avro_item.value<bool>() = (strcmp(val, "True") == 0);
              break;
            case avro::AVRO_RECORD:
            case avro::AVRO_ENUM:
            case avro::AVRO_ARRAY:
            case avro::AVRO_MAP:
            case avro::AVRO_UNION:
            case avro::AVRO_FIXED:
            case avro::AVRO_NULL:
            default:
              LOG(FATAL) << "unexpected / non supported type e:" << avro_item.type();
          }
        }
      }

      //we need to store last timestamp and last key for next select clause
      if (id_column_index_>=0)
        last_id_ = PQgetvalue(result.get(), i, id_column_index_);
      if (ts_column_index_>=0)
        last_ts_ = PQgetvalue(result.get(), i, ts_column_index_);

      // or should we use ts column instead of now();
      auto r = std::make_shared<krecord<void, kspp::generic_avro>>(gd, kspp::milliseconds_since_epoch());
      auto e = std::make_shared<kevent<void, kspp::generic_avro>>(r);
      assert(e.get()!=nullptr);

      //should we wait a bit if we fill incomming queue to much??
      while(_incomming_msg.size()>10000 && !_exit) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        DLOG(INFO) << "c_incomming_msg.size() " << _incomming_msg.size();
      }

      _incomming_msg.push_back(e);
      ++_msg_cnt;
    }
    return 0;
  }

  void postgres_consumer::_thread() {
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

        //UTF8?
        if (!_connection->set_client_encoding("UTF8")){
          std::this_thread::sleep_for(10s);
          continue;
        }
      }

      _eof = false;

      std::string fields = "*";
      std::string order_by = "";
      if (_ts_column.size()) {
        if (_id_column.size())
          order_by = " ORDER BY " + _ts_column + " ASC, " + _id_column + " ASC";
        else
          order_by = " ORDER BY " + _ts_column + " ASC";
      } else {
        order_by = " ORDER BY " + _id_column + " ASC";
      }

      std::string where_clause;

      // do we have a timestamp field
      // we have to have either a interger id that is increasing or a timestamp that is increasing
      // before we read anything the where clause will not be valid
      if (last_id_.size()) {
        if (_ts_column.size())
          where_clause =
              " WHERE (" + _ts_column + " = '" + last_ts_ + "' AND " + _id_column + " > '" + last_id_ + "') OR (" +
              _ts_column + " > '" + last_ts_ + "')";
        else
          where_clause = " WHERE " + _id_column + " > '" + last_id_ + "'";
      } else {
        if (last_ts_.size())
          where_clause = " WHERE " + _ts_column + " >= '" + last_ts_ + "'";
      }

      //std::string statement = "SELECT TOP " + std::to_string(_max_items_in_fetch) + " " + fields + " FROM " + _table + where_clause + " ORDER BY " + order_by;
      std::string statement = "SELECT " + fields + " FROM " + _table + where_clause + order_by + " LIMIT " + std::to_string(_max_items_in_fetch);

      DLOG(INFO) << "exec(" + statement + ")";

      auto ts0 = kspp::milliseconds_since_epoch();
      auto last_msg_count = _msg_cnt;
      auto res = _connection->exec(statement);
      if (res.first) {
        LOG(ERROR) << "exec failed - disconnecting and retrying e: " << _connection->last_error();
        _connection->disconnect();
        std::this_thread::sleep_for(10s);
        continue;
      }

      int parse_result = parse_response(res.second);
      if (parse_result) {
        LOG(ERROR) << "parse failed - disconnecting and retrying";
        _connection->disconnect();
        std::this_thread::sleep_for(10s);
        continue;
      }
      auto ts1 = kspp::milliseconds_since_epoch();

      if ((_msg_cnt - last_msg_count) != _max_items_in_fetch)
        _eof = true;

      LOG(INFO) << "poll done, table: " << _table << " retrieved: " << _msg_cnt - last_msg_count << " messages, total: " << _msg_cnt << ", last ts: " << last_ts_ << " duration " << ts1 -ts0 << " ms";


      if (_eof) {
        // what is sleeping cannot bne killed...
        int count = poll_intervall_.count();
        for (int i = 0; i != count; ++i) {
          std::this_thread::sleep_for(1s);
          if (_exit)
            break;
        }
      }

    }
    DLOG(INFO) << "exiting thread";
  }
}

