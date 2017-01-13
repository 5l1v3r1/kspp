#include <iostream>
#include <string>
#include <chrono>
#include <kspp/codecs/binary_codec.h>
#include <kspp/topology_builder.h>

#define PARTITION 0

int main(int argc, char **argv) {
  size_t join_count = 0;

  auto builder = kspp::topology_builder<kspp::binary_codec>("localhost", "C:\\tmp");
  auto stream = builder.create_kafka_source<boost::uuids::uuid, int64_t>("kspp_test0_eventstream", PARTITION);
  auto table = builder.create_ktable<boost::uuids::uuid, int64_t>("example2-join", "proc-0", "kspp_test0_table", PARTITION);

  auto join = builder.create_left_join<boost::uuids::uuid, int64_t, int64_t, int64_t>(stream, table, [&join_count](const boost::uuids::uuid& key, const int64_t& left, const int64_t& right, int64_t& row) {
    row = right;
    join_count++;
  });
  
  auto sink = builder.create_kafka_sink<boost::uuids::uuid, int64_t>("kspp_test0_eventstream_out", 0);
  join->start(-2);

  // first sync table
  std::cout << "before sync" << std::endl;
  table->flush();
  table->commit();
  std::cout << "after sync" << std::endl;

  auto t0 = std::chrono::high_resolution_clock::now();

  // now join stream with loaded table
  join_count = 0;
  join->flush();
  join->commit();

  auto t1 = std::chrono::high_resolution_clock::now();
  auto fs = t1 - t0;
  auto d = std::chrono::duration_cast<std::chrono::milliseconds>(fs);
  std::cout << "joins: " << join_count << " t: " << d.count() << "ms\n";
  std::cout << "lookups per sec : " << 1000.0 * join_count / (double)d.count() << std::endl;
  return 0;
}
