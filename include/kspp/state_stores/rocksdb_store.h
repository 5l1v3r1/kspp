#include <memory>
#include <strstream>
#include <fstream>
#include <experimental/filesystem>
#include <glog/logging.h>
#include <kspp/kspp.h>
#include "state_store.h"

#ifdef WIN32
//you dont want to know why this is needed...
#undef max
#endif

#include <rocksdb/db.h>
#pragma once

namespace kspp {
  template<class K, class V, class CODEC>
  class rocksdb_store
          : public state_store<K, V> {
  public:
    enum { MAX_KEY_SIZE = 10000, MAX_VALUE_SIZE = 100000 };

    class iterator_impl : public kmaterialized_source_iterator_impl<K, V> {
    public:
      enum seek_pos_e { BEGIN, END };

      iterator_impl(rocksdb::DB *db, std::shared_ptr<CODEC> codec, seek_pos_e pos)
              : _it(db->NewIterator(rocksdb::ReadOptions())), _codec(codec) {
        if (pos == BEGIN) {
          _it->SeekToFirst();
        } else {
          _it->SeekToLast(); // is there a better way to init to non valid??
          if (_it->Valid()) // if not valid the Next() calls fails...
            _it->Next(); // now it's invalid
        }
      }

      bool valid() const override {
        return _it->Valid();
      }

      void next() override {
        if (!_it->Valid())
          return;
        _it->Next();
      }

      std::shared_ptr<const krecord<K, V>> item() const override {
        if (!_it->Valid())
          return nullptr;
        rocksdb::Slice key = _it->key();
        rocksdb::Slice value = _it->value();


        int64_t timestamp = 0;
        // sanity - value size at least timestamp
        if (value.size() < sizeof(int64_t))
          return nullptr;
        memcpy(&timestamp, value.data(), sizeof(int64_t));
        K tmp_key;

        if (_codec->decode(key.data(), key.size(), tmp_key) != key.size())
          return nullptr;


        size_t actual_sz = value.size() - sizeof(int64_t); // remove timestamp
        auto tmp_value = std::make_shared<V>();
        size_t consumed = _codec->decode(value.data() + sizeof(int64_t), actual_sz, *tmp_value);
        if (consumed != actual_sz) {
          LOG(ERROR) << "rocksdb_store decode payload failed, consumed:" << consumed << ", actual sz:" << actual_sz;
          return nullptr;
        }
        return std::make_shared<krecord<K, V>>(tmp_key, tmp_value, timestamp);
      }

      bool operator==(const kmaterialized_source_iterator_impl<K, V> &other) const override {
        //fastpath...
        if (valid() && !other.valid())
          return false;
        if (!valid() && !other.valid())
          return true;
        if (valid() && other.valid())
          return _it->key() == ((const iterator_impl &) other)._it->key();
        return false;
      }

      inline rocksdb::Slice _key_slice() const {
        return _it->key();
      }

    private:
      std::unique_ptr<rocksdb::Iterator> _it;
      std::shared_ptr<CODEC> _codec;

    };

    rocksdb_store(std::experimental::filesystem::path storage_path, std::shared_ptr<CODEC> codec = std::make_shared<CODEC>())
            : _offset_storage_path(storage_path)
            , _codec(codec)
            , _current_offset(kspp::OFFSET_BEGINNING)
            , _last_comitted_offset(kspp::OFFSET_BEGINNING)
            , _last_flushed_offset(kspp::OFFSET_BEGINNING) {
      LOG_IF(FATAL, storage_path.generic_string().size()==0);
      std::experimental::filesystem::create_directories(storage_path);
      _offset_storage_path /= "kspp_offset.bin";
      rocksdb::Options options;
      options.create_if_missing = true;
      options.IncreaseParallelism(); // should be #cores
      options.OptimizeLevelStyleCompaction();
      rocksdb::DB *tmp = nullptr;
      auto s = rocksdb::DB::Open(options, storage_path.generic_string(), &tmp);
      _db.reset(tmp);
      if (!s.ok()) {
        LOG(FATAL) << "rocksdb_store, failed to open rocks db, path:" << storage_path.generic_string();
        throw std::runtime_error(
                std::string("rocksdb_store, failed to open rocks db, path:") + storage_path.generic_string());
      }

      if (std::experimental::filesystem::exists(_offset_storage_path)) {
        std::ifstream is(_offset_storage_path.generic_string(), std::ios::binary);
        int64_t tmp;
        is.read((char *) &tmp, sizeof(int64_t));
        if (is.good()) {
          _current_offset = tmp;
          _last_comitted_offset = tmp;
          _last_flushed_offset = tmp;
        }
      }
    }

    ~rocksdb_store() {
      close();
    }

    static std::string type_name() {
      return "rocksdb_store";
    }

    void close() override {
      _db = nullptr;
    }

    void _insert(std::shared_ptr<const krecord<K, V>> record, int64_t offset) override {
      _current_offset = std::max<int64_t>(_current_offset, offset);
      char key_buf[MAX_KEY_SIZE];
      char val_buf[MAX_VALUE_SIZE];

      size_t ksize = 0;
      size_t vsize = 0;
      //_current_offset = std::max<int64_t>(_current_offset, record->offset());
      if (record->value()) {
        {
          std::strstream s(key_buf, MAX_KEY_SIZE);
          ksize = _codec->encode(record->key(), s);
        }

        // write timestamp
        int64_t tmp = record->event_time();
        memcpy(val_buf, &tmp, sizeof(int64_t));
        {
          std::strstream s(val_buf + sizeof(int64_t), MAX_VALUE_SIZE - sizeof(int64_t));
          vsize = _codec->encode(*record->value(), s) + +sizeof(int64_t);
        }
        rocksdb::Status status = _db->Put(rocksdb::WriteOptions(), rocksdb::Slice((char *) key_buf, ksize),
                                          rocksdb::Slice(val_buf, vsize));
      } else {
        std::strstream s(key_buf, MAX_KEY_SIZE);
        ksize = _codec->encode(record->key(), s);
        auto status = _db->Delete(rocksdb::WriteOptions(), rocksdb::Slice(key_buf, ksize));
      }
    }

    std::shared_ptr<const krecord<K, V>> get(const K &key) const override{
      char key_buf[MAX_KEY_SIZE];
      size_t ksize = 0;
      {
        std::ostrstream s(key_buf, MAX_KEY_SIZE);
        ksize = _codec->encode(key, s);
      }

      std::string payload;
      rocksdb::Status s = _db->Get(rocksdb::ReadOptions(), rocksdb::Slice(key_buf, ksize), &payload);
      if (!s.ok())
        return nullptr;

      int64_t timestamp = 0;
      // sanity - at least timestamp
      if (payload.size() < sizeof(int64_t))
        return nullptr;
      memcpy(&timestamp, payload.data(), sizeof(int64_t));

      // read value
      size_t actual_sz = payload.size() - sizeof(int64_t);
      auto tmp_value = std::make_shared<V>();
      size_t consumed = _codec->decode(payload.data() + sizeof(int64_t), actual_sz, *tmp_value);
      if (consumed != actual_sz) {
        LOG(ERROR) << "rockdb_store, decode payload failed, consumed:" << consumed << ", actual sz:" << actual_sz;
        return nullptr;
      }
      return std::make_shared<krecord<K, V>>(key, tmp_value, timestamp);
    }

    //should we allow writing -2 in store??
    void start(int64_t offset) override {
      _current_offset = offset;
      commit(true);
    }

    /**
    * commits the offset
    */
    void commit(bool flush) override {
      _last_comitted_offset = _current_offset;
      if (flush || ((_last_comitted_offset - _last_flushed_offset) > 10000)) {
        if (_last_flushed_offset != _last_comitted_offset) {
          std::ofstream os(_offset_storage_path.generic_string(), std::ios::binary);
          os.write((char *) &_last_comitted_offset, sizeof(int64_t));
          _last_flushed_offset = _last_comitted_offset;
          os.flush();
        }
      }
    }

    /**
    * returns last offset
    */
    int64_t offset() const override {
      return _current_offset;
    }

    size_t aprox_size() const override {
      std::string num;
      _db->GetProperty("rocksdb.estimate-num-keys", &num);
      return std::stoll(num);
    }

    size_t exact_size() const override {
      size_t sz = 0;
      for (auto i = begin(); i != end(); ++i) {
        ++sz;
      }

      return sz;
    }

    void clear() override {
      for (auto it = iterator_impl(_db.get(), _codec, iterator_impl::BEGIN), end_ = iterator_impl(_db.get(), _codec, iterator_impl::END);
           it != end_; it.next()) {
        auto s = _db->Delete(rocksdb::WriteOptions(), it._key_slice());
      }
      _current_offset = kspp::OFFSET_BEGINNING;
    }


    typename kspp::materialized_source<K, V>::iterator begin(void) const override {
      return typename kspp::materialized_source<K, V>::iterator(
              std::make_shared<iterator_impl>(_db.get(), _codec, iterator_impl::BEGIN));
    }

    typename kspp::materialized_source<K, V>::iterator end() const override {
      return typename kspp::materialized_source<K, V>::iterator(
              std::make_shared<iterator_impl>(_db.get(), _codec, iterator_impl::END));
    }

  private:
    std::experimental::filesystem::path _offset_storage_path;
    std::unique_ptr<rocksdb::DB> _db;        // maybe this should be a shared ptr since we're letting iterators out...
    std::shared_ptr<CODEC> _codec;
    int64_t _current_offset;
    int64_t _last_comitted_offset;
    int64_t _last_flushed_offset;
  };
}



