#include <iostream>
#include <string>
#include <chrono>
#include <regex>
#include <kspp/binary_encoder.h>
#include <kspp/text_codec.h>
#include <kspp/topology_builder.h>
#include <kspp/processors/transform.h>
#include <kspp/processors/count.h>

#define PARTITION 0


struct word_count_data
{
  std::string text;
  int64_t     count;
};

inline size_t binary_encode(const word_count_data& obj, std::ostream& dst) {
  size_t sz = 0;
  sz += csi::binary_encode(obj.text, dst);
  sz += csi::binary_encode(obj.count, dst);
  return dst.good() ? sz : 0;
}

inline size_t binary_decode(std::istream& src, word_count_data& obj) {
  size_t sz = 0;
  sz += csi::binary_decode(src, obj.text);
  sz += csi::binary_decode(src, obj.count);
  return src.good() ? sz : 0;
}

int main(int argc, char **argv) {
  auto codec = std::make_shared<csi::binary_codec>();
  auto builder = csi::topology_builder<csi::binary_codec>("localhost", "C:\\tmp", codec);

  {
    auto sink = builder.create_kafka_sink<void, std::string>("kspp_TextInput", PARTITION);
    csi::produce<void, std::string>(*sink, "hello kafka streams");
  }

  {
    auto source = builder.create_kafka_source<void, std::string>("kspp_TextInput", PARTITION);
    source->start(-2);
    while (!source->eof()) {
      auto msg = source->consume();
      if (msg) {
        std::cerr << *msg->value << std::endl;
      }
    }
  }

  {
    auto source = builder.create_kafka_source<void, std::string>("kspp_TextInput", PARTITION);

    std::regex rgx("\\s+");
    //auto word_stream = std::make_shared<csi::transform_stream<void, std::string, std::string, void>>(source, [&rgx](std::shared_ptr<csi::krecord<void, std::string>> e, csi::ksink<std::string, void>* sink) {
    auto word_stream = std::make_shared<csi::transform_stream<void, std::string, std::string, void>>(source, [&rgx](const auto e, auto sink) {
      std::sregex_token_iterator iter(e->value->begin(), e->value->end(), rgx, -1);
      std::sregex_token_iterator end;
      for (; iter != end; ++iter)
        sink->produce(std::make_shared<csi::krecord<std::string, void>>(*iter));
    });
    
    auto word_counts = std::make_shared<csi::count_keys<std::string, csi::binary_codec>>(word_stream, "C:\\tmp", codec);

    word_counts->start(-2);
    while (!word_counts->eof()) {
      auto msg = word_counts->consume();
    }

    for (auto i : *word_counts)
      std::cerr << i->key << " : " << *i->value << std::endl;


  }


  {
    auto text_codec = std::make_shared<csi::text_codec>();
    auto text_builder = csi::topology_builder<csi::text_codec>("localhost", "C:\\tmp", text_codec);
    auto source = text_builder.create_kafka_source<void, std::string>("kspp_bible", PARTITION);

    std::regex rgx("\\s+");
    //auto word_stream = std::make_shared<csi::transform_stream<void, std::string, std::string, void>>(source, [&rgx](std::shared_ptr<csi::krecord<void, std::string>> e, csi::ksink<std::string, void>* sink) {
    auto word_stream = std::make_shared<csi::transform_stream<void, std::string, std::string, void>>(source, [&rgx](const auto e, auto sink) {
      std::sregex_token_iterator iter(e->value->begin(), e->value->end(), rgx, -1); 
      std::sregex_token_iterator end;
      for (; iter != end; ++iter)
        sink->produce(std::make_shared<csi::krecord<std::string, void>>(*iter));
    });

    auto word_counts = std::make_shared<csi::count_keys<std::string, csi::text_codec>>(word_stream, "C:\\tmp", text_codec);

    word_counts->start(-2); // this does not reset the counts in the backing store.....
    while (!word_counts->eof()) {
      auto msg = word_counts->consume();
      //if (msg) {
      //  std::cerr << msg->key << ":" << *msg->value << std::endl;
      //}
    }

    for (auto i : *word_counts)
      std::cerr << i->key << " : " << *i->value << std::endl;


  }


}
