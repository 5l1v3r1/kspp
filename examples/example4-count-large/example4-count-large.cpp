#include <iostream>
#include <string>
#include <chrono>
#include <regex>
#include <kspp/codecs/text_codec.h>
#include <kspp/topology_builder.h>
#include <kspp/processors/transform.h>
#include <kspp/processors/count.h>
#include <kspp/algorithm.h>

#define NR_OF_PARTITIONS 8

int main(int argc, char **argv) {
  auto text_builder = kspp::topology_builder<kspp::text_codec>("example4-count", "localhost");

  {
    auto topology = text_builder.create_topic_topology();
    auto sources = topology->create_kafka_sources<void, std::string>("test_text", NR_OF_PARTITIONS);

    //TBD this could be a topic_transform (now it's a partition_transform)
    std::regex rgx("\\s+");
    auto word_streams = kspp::flat_map<void, std::string, std::string, void>::create(sources, [&rgx](const auto e, auto flat_map) {
      std::sregex_token_iterator iter(e->value->begin(), e->value->end(), rgx, -1);
      std::sregex_token_iterator end;
      for (; iter != end; ++iter)
        flat_map->push_back(std::make_shared<kspp::krecord<std::string, void>>(*iter));
    });

    auto word_sink = topology->create_kafka_topic_sink<std::string, void>(word_streams, "test_words");

    for (auto i : word_streams) {
      i->start(-2);
    }

    while (!eof(word_streams))
      process_one(word_streams);
  }

  { 
    auto topology = text_builder.create_topic_topology();
    auto word_sources = topology->create_kafka_sources<std::string, void>("test_words", NR_OF_PARTITIONS);
    auto word_counts = topology->create_count_by_key<std::string, size_t>(word_sources, 10000);
    
    topology->init_metrics();

    for (auto i : word_counts) {
      std::cerr << i->name() << std::endl;
      i->start(-2);
      i->flush();
    }

    for (auto i : word_counts)
      for (auto j : *i)
        std::cerr << j->key << " : " << *j->value << std::endl;

    topology->output_metrics(std::cerr);
  }
}
