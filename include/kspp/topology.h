#include <kspp/kspp.h>
#include <kspp/processors/merge.h>
#include <limits>
#include <set>
#pragma once

namespace kspp {
  class topology {
  public:
    topology(std::shared_ptr<cluster_config> c_config, std::string topology_id);

    virtual ~topology();

    std::shared_ptr<cluster_config> get_cluster_config() {
      return _cluster_config;
    }

    std::chrono::milliseconds max_buffering_time() const;

    void init_metrics(std::vector<metrics20::avro::metrics20_key_tags_t> tags = std::vector<metrics20::avro::metrics20_key_tags_t>());

    void for_each_metrics(std::function<void(kspp::metric &)> f);

    bool eof();

    std::size_t process_1s();

    std::size_t process(int64_t ts); // =milliseconds_since_epoch()

    void close();

    void start(start_offset_t offset);

    void commit(bool force);

    void flush(bool wait_for_events = true, std::size_t event_limit = std::numeric_limits<std::size_t>::max());

    void validate_preconditions();

    // top level factory
    template<class pp, typename... Args>
    typename std::enable_if<std::is_base_of<kspp::partition_processor, pp>::value, std::vector<std::shared_ptr<pp>>>::type
    create_processors(std::vector<int> partition_list, Args... args) {
      std::vector <std::shared_ptr<pp>> result;
      for (auto i : partition_list) {
        auto p = std::make_shared<pp>(this->get_cluster_config(), i, args...);
        _partition_processors.push_back(p);
        result.push_back(p);
      }
      return result;
    }

    // should this be removed?? right now only merge
    template<class pp, typename... Args>
    typename std::enable_if<std::is_base_of<kspp::partition_processor, pp>::value, std::shared_ptr<pp>>::type
    create_processor(Args... args) {
      auto p = std::make_shared<pp>(this->get_cluster_config(), args...);
      _partition_processors.push_back(p);
      return p;
    }


    // should this be removed - since we're likely to want to merge two streams
    /*
     * template<class ps, typename... Args>
    std::shared_ptr<kspp::merge<typename ps::key_type, typename ps::value_type>>
    merge(std::vector<std::shared_ptr<ps>> sources, Args... args) {
      std::shared_ptr <kspp::merge<typename ps::key_type, typename ps::value_type>> result = std::make_shared<kspp::merge<typename ps::key_type, typename ps::value_type>>(
          this->get_cluster_config(), args...);
      result->add(sources);
      _partition_processors.push_back(result);
      return result;
    }
    */

    /*
     * template<class ps, typename... Args>
    std::shared_ptr<kspp::merge<typename ps::key_type, typename ps::value_type>>
    merge(Args... args) {
      std::shared_ptr <kspp::merge<typename ps::key_type, typename ps::value_type>> result = std::make_shared<kspp::merge<typename ps::key_type, typename ps::value_type>>(
          this->get_cluster_config(), args...);
      _partition_processors.push_back(result);
      return result;
    }
    */

    // when you have a vector of partitions - lets create a new processor layer
    template<class pp, class ps, typename... Args>
    typename std::enable_if<std::is_base_of<kspp::partition_processor, pp>::value, std::vector<std::shared_ptr<pp>>>::type
    create_processors(std::vector<std::shared_ptr<ps>> sources, Args... args) {
      std::vector <std::shared_ptr<pp>> result;
      for (auto i : sources) {
        auto p = std::make_shared<pp>(this->get_cluster_config(), i, args...);
        _partition_processors.push_back(p);
        result.push_back(p);
      }
      return result;
    }

    /**
      joins between two arrays
      we could probably have stricter contraint on the types of v1 and v2
    */
    template<class pp, class sourceT, class leftT, typename... Args>
    typename std::enable_if<std::is_base_of<kspp::partition_processor, pp>::value, std::vector<std::shared_ptr<pp>>>::type
    create_processors(
        std::vector<std::shared_ptr<sourceT>> v1,
        std::vector<std::shared_ptr<leftT>> v2,
        Args... args) {
      std::vector <std::shared_ptr<pp>> result;
      auto i = v1.begin();
      auto j = v2.begin();
      auto end = v1.end();
      for (; i != end; ++i, ++j) {
        auto p = std::make_shared<pp>(this->get_cluster_config(), *i, *j, args...);
        _partition_processors.push_back(std::static_pointer_cast<kspp::partition_processor>(p));
        result.push_back(p);
      }
      return result;
    }

    // TBD
    // only kafka metrics reporter uses this - fix this by using a stream and a separate sink or raw sink
    template<class pp, typename... Args>
    typename std::enable_if<std::is_base_of<kspp::processor, pp>::value, std::shared_ptr<pp>>::type
    create_sink(Args... args) {
      auto p = std::make_shared<pp>(this->get_cluster_config(), args...);
      _sinks.push_back(p);
      return p;
    }

    // create from vector of sources - return one (kafka sink)
    template<class pp, class source, typename... Args>
    typename std::enable_if<std::is_base_of<kspp::processor, pp>::value, std::shared_ptr<pp>>::type
    create_sink(std::shared_ptr<source> src, Args... args) {
      auto p = std::make_shared<pp>(this->get_cluster_config(), args...);
      _sinks.push_back(p);
      src->add_sink(p);
      return p;
    }

    // create from vector of sources - return one (kafka sink..)
    template<class pp, class source, typename... Args>
    typename std::enable_if<std::is_base_of<kspp::processor, pp>::value, std::shared_ptr<pp>>::type
    create_sink(std::vector<std::shared_ptr<source>> sources, Args... args) {
      auto p = std::make_shared<pp>(this->get_cluster_config(), args...);
      _sinks.push_back(p);
      for (auto i : sources)
        i->add_sink(p);
      return p;
    }

  protected:
    void init_processing_graph();
    bool _is_started;
    std::shared_ptr<cluster_config> _cluster_config;
    std::string _topology_id;
    boost::filesystem::path _root_path;
    std::vector<std::shared_ptr<partition_processor>> _partition_processors;
    std::vector<std::shared_ptr<processor>> _sinks;
    std::vector<std::shared_ptr<partition_processor>> _top_partition_processors;
    int64_t _next_gc_ts;
    int64_t _min_buffering_ms;
    size_t _max_pending_sink_messages;
    std::set<std::string> _precondition_topics;
    std::string _precondition_consumer_group;
  };
} // namespace
