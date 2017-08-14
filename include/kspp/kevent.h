#include <cstdint>
#include <cassert>
#include <memory>
#include <kspp/krecord.h>
#include <kspp/impl/commit_chain.h>

#pragma once

namespace kspp {
  template<class K, class V>
  class kevent {
  public:
    kevent(std::shared_ptr<const krecord<K, V>> r, std::shared_ptr<commit_chain::autocommit_marker> autocommit_marker = nullptr)
            : _record(r)
              , _autocommit_marker(autocommit_marker)
              , _partition_hash(-1) {
    }

    kevent(std::shared_ptr<const krecord<K, V>> r, std::shared_ptr<commit_chain::autocommit_marker> autocommit_marker, uint32_t partition_hash)
            : _record(r)
              , _autocommit_marker(autocommit_marker)
              , _partition_hash(partition_hash) {
    }

    inline int64_t event_time() const {
      return _record ? _record->event_time() : -1;
    }

    inline int64_t offset() const {
      return _autocommit_marker ? _autocommit_marker->offset() : -1;
    }

    inline std::shared_ptr<const krecord<K, V>> record() {
      return _record;
    }

    inline std::shared_ptr<commit_chain::autocommit_marker> id() {
      return _autocommit_marker;
    }

    inline bool has_partition_hash() const {
      return _partition_hash >= 0;
    }

    inline uint32_t partition_hash() const {
      assert(_partition_hash >= 0);
      return static_cast<uint32_t>(_partition_hash);
    }

  private:
    std::shared_ptr<const krecord<K, V>> _record;
    std::shared_ptr<commit_chain::autocommit_marker> _autocommit_marker;
    const int64_t _partition_hash;
  };
}