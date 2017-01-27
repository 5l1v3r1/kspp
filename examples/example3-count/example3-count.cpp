#include <iostream>
#include <string>
#include <chrono>
#include <regex>
#include <kspp/codecs/text_codec.h>
#include <kspp/topology_builder.h>
#include <kspp/processors/transform.h>
#include <kspp/processors/count.h>
#include <kspp/algorithm.h>

#define PARTITION 0

int main(int argc, char **argv) {
  auto codec = std::make_shared<kspp::text_codec>();
  auto builder = kspp::topology_builder("example3-count", "localhost");
  {
    auto topology = builder.create_topology(PARTITION);
    auto sink = topology->create_processor<kspp::kafka_partition_sink<void, std::string, kspp::text_codec>>("kspp_TextInput", codec);
    kspp::produce<void, std::string>(*sink, "hello kafka streams");
  }

  {
    auto topology = builder.create_topology(PARTITION);
    auto source = topology->create_processor<kspp::kafka_source<void, std::string, kspp::text_codec>>("kspp_TextInput", codec);

    std::regex rgx("\\s+");
    auto word_stream = topology->create_processor<kspp::flat_map<void, std::string, std::string, void>>(source, [&rgx](const auto e, auto flat_map) {
      std::sregex_token_iterator iter(e->value->begin(), e->value->end(), rgx, -1);
      std::sregex_token_iterator end;
      for (; iter != end; ++iter)
        flat_map->push_back(std::make_shared<kspp::krecord<std::string, void>>(*iter));
    });

    auto word_counts = topology->create_processor<kspp::count_by_key<std::string, int, kspp::text_codec>>(word_stream, 2000, codec);
    auto sink = topology->create_processor<kspp::stream_sink<std::string, int>>(word_counts, &std::cerr);

    topology->init_metrics();
    word_counts->start(-2);
    word_counts->flush();

    {
      auto metrics = builder.create_topic_topology();
      auto metrics_sink = metrics->create<kspp::kafka_topic_sink<std::string, std::string, kspp::text_codec>>("kspp_metrics", codec);

      //topology->output_metrics(metrics_sink);

    }

  }
}
