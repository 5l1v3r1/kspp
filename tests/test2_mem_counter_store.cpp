#include <cassert>
#include <kspp/state_stores/mem_counter_store.h>

using namespace std::chrono_literals;

int main(int argc, char **argv) {
  // insert 3 check size
  kspp::mem_counter_store<int32_t, int> store("");
  auto t0 = kspp::milliseconds_since_epoch();
  store.insert(std::make_shared<kspp::krecord<int32_t, int>>(0, 1, t0), -1);
  store.insert(std::make_shared<kspp::krecord<int32_t, int>>(1, 1, t0), -1);
  store.insert(std::make_shared<kspp::krecord<int32_t, int>>(2, 1, t0), -1);
  assert(store.exact_size() == 3);

  // update existing key with new value
  {
    store.insert(std::make_shared<kspp::krecord<int32_t, int>>(2, 1, t0 + 10), -1);
    assert(store.exact_size() == 3);
    auto record = store.get(2);
    assert(record != nullptr);
    assert(record->key() == 2);
    assert(record->value() != nullptr);
    assert(*record->value() == 2);
    assert(record->event_time() == t0 + 10);
  }

  // update existing key with new value but old timestamp
  // this should be ok since this is an aggregation
  {
    store.insert(std::make_shared<kspp::krecord<int32_t, int>>(2, 2, t0), -1);
    assert(store.exact_size() == 3);
    auto record = store.get(2);
    assert(record != nullptr);
    assert(record->key() == 2);
    assert(record->value() != nullptr);
    assert(*record->value() == 4);
    assert(record->event_time() == t0 + 10); // keep biggest timestamp - not latest
  }

  // update existing key with new negative value
  {
    store.insert(std::make_shared<kspp::krecord<int32_t, int>>(0, -2, t0), -1);
    assert(store.exact_size() == 3);
    auto record = store.get(0);
    assert(record != nullptr);
    assert(record->key() == 0);
    assert(record->value() != nullptr);
    assert(*record->value() == -1);
  }

  // delete existing key with to old timestamp
  // should be forbidden
  {
    store.insert(std::make_shared<kspp::krecord<int32_t, int>>(2, nullptr, t0), -1);
    assert(store.exact_size() == 3);
    auto record = store.get(2);
    assert(record != nullptr);
    assert(record->key() == 2);
    assert(record->value() != nullptr);
    assert(*record->value() == 4);
  }

  // delete existing key with new timestamp
  {
    store.insert(std::make_shared<kspp::krecord<int32_t, int>>(2, nullptr, t0 + 30), -1);
    assert(store.exact_size() == 2);
    auto record = store.get(2);
    assert(record == nullptr);
  }
  return 0;
}


