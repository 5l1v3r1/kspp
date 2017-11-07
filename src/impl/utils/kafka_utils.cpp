#include <kspp/impl/kafka_utils.h>
#include <thread>
#include <glog/logging.h>
#include <librdkafka/rdkafka.h>
#include <kspp/impl/rd_kafka_utils.h>

using namespace std::chrono_literals;

namespace kspp {
  namespace kafka {
    int32_t get_number_partitions(std::shared_ptr<cluster_config> cluster_config, std::string topic) {
      std::string errstr;
      std::unique_ptr<RdKafka::Conf> conf(RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL));
      /*
      * Set configuration properties
      */
      try {
        set_broker_config(conf.get(), cluster_config);
        set_config(conf.get(), "api.version.request", "true");
      }
      catch (std::invalid_argument &e) {
        LOG(FATAL) << "get_number_partitions: " << topic << " bad config " << e.what();
      }
      auto producer = std::unique_ptr<RdKafka::Producer>(RdKafka::Producer::create(conf.get(), errstr));
      if (!producer) {
        LOG(FATAL) << ", failed to create producer:" << errstr;
      }
      // really try to make sure the partition exist before we continue
      RdKafka::Metadata *md = NULL;
      auto _rd_topic = std::unique_ptr<RdKafka::Topic>(RdKafka::Topic::create(producer.get(), topic, nullptr, errstr));
      if (!_rd_topic) {
        LOG(FATAL) << "failed to create RdKafka::Topic:" << errstr;
      }
      int32_t nr_of_partitions = 0;
      while (nr_of_partitions == 0) {
        auto ec = producer->metadata(false, _rd_topic.get(), &md, 5000);
        if (ec == 0) {
          const RdKafka::Metadata::TopicMetadataVector *v = md->topics();
          for (auto &&i : *v) {
            if (i->topic() == topic)
              nr_of_partitions = (int32_t) i->partitions()->size();
          }
        }
        if (nr_of_partitions == 0) {
          LOG(ERROR) << "waiting for topic " << topic << " to be available";
          std::this_thread::sleep_for(1s);
        }
      }
      delete md;
      return nr_of_partitions;
    }

    /*int wait_for_partition(std::shared_ptr<cluster_config> cluster_config, std::string topic, int32_t partition) {
      std::string errstr;
      std::unique_ptr<RdKafka::Conf> conf(RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL));

      LOG_IF(FATAL, partition < 0);

      *//*
      * Set configuration properties
      *//*
      try {
        set_broker_config(conf.get(), cluster_config);
        set_config(conf.get(), "api.version.request", "true");

      }
      catch (std::invalid_argument &e) {
        LOG(FATAL) << "get_number_partitions: " << topic << " bad config " << e.what();
      }

      auto producer = std::unique_ptr<RdKafka::Producer>(RdKafka::Producer::create(conf.get(), errstr));
      if (!producer) {
        LOG(ERROR) << "failed to create producer:" << errstr;
        return -1;
      }

      // make sure the partition exist before we continue
      RdKafka::Metadata *md = NULL;
      auto _rd_topic = std::unique_ptr<RdKafka::Topic>(RdKafka::Topic::create(producer.get(), topic, nullptr, errstr));
      if (!_rd_topic) {
        LOG(FATAL) << "failed to create RdKafka::Topic:" << errstr;
      }
      bool is_available = false;
      while (!is_available) {
        auto ec = producer->metadata(false, _rd_topic.get(), &md, 5000);
        if (ec == 0) {
          const RdKafka::Metadata::TopicMetadataVector *v = md->topics();
          for (auto &&i : *v) {
            auto partitions = i->partitions();
            for (auto &&j : *partitions) {
              if ((j->err() == 0) && (j->id() == partition) && (j->leader() >= 0)) {
                is_available = true;
                break;
              }
            }
          }
        }
        if (!is_available) {
          LOG(ERROR) << "waiting for partitions leader to be available, " << topic << ":" << partition;
          std::this_thread::sleep_for(1s);
        }
      }
      delete md;
      return 0;
    }
*/


    // STUFF THATS MISSING IN LIBRDKAFKA C++ API...
    // WE DO THIS THE C WAY INSTEAD
    // NEEDS TO BE REWRITTEN USING USING C++ API

    // copy of c++ code
    /*
     * static void set_broker_config(rd_kafka_conf_t *rd_conf, std::shared_ptr<kspp::cluster_config> cluster_config) {
      rd_kafka_conf_set(rd_conf, "bootstrap.servers", cluster_config->get_brokers().c_str(), nullptr, 0);

      if (cluster_config->get_brokers().substr(0, 3) == "ssl") {
        // SSL no auth - always
        rd_kafka_conf_set(rd_conf, "security.protocol", "ssl", nullptr, 0);
        rd_kafka_conf_set(rd_conf, "ssl.ca.location", cluster_config->get_ca_cert_path().c_str(), nullptr, 0);

        //client cert
        rd_kafka_conf_set(rd_conf, "ssl.certificate.location", cluster_config->get_client_cert_path().c_str(), nullptr,
                          0);
        rd_kafka_conf_set(rd_conf, "ssl.key.location", cluster_config->get_private_key_path().c_str(), nullptr, 0);
        // optional password
        if (cluster_config->get_private_key_passphrase().size())
          rd_kafka_conf_set(rd_conf, "ssl.key.password", cluster_config->get_private_key_passphrase().c_str(), nullptr,
                            0);
      }
    }
     */

    int wait_for_group(std::shared_ptr<cluster_config> cluster_config, std::string group_id) {
      while(true)
      {
        if (cluster_config->get_metadata_provider()->consumer_group_exists(group_id, 1s))
          return true;
        LOG(ERROR) << "waiting for " << group_id << ", retrying in 1s";
        std::this_thread::sleep_for(1s);
      }
    }

    // NEEDS TO BE REWRITTEN USING USING C++ API
//
//     bool group_exists(std::shared_ptr<cluster_config> cluster_config, std::string group_id) {
//      char errstr[128];
//      rd_kafka_t *rk = nullptr;
//      rd_kafka_conf_t *rd_conf = rd_kafka_conf_new();
//      set_broker_config(rd_conf, cluster_config);
//
//      /* Create Kafka C handle */
//      if (!(rk = rd_kafka_new(RD_KAFKA_PRODUCER, rd_conf, errstr, sizeof(errstr)))) {
//        LOG(FATAL) << "rd_kafka_new failed";
//        rd_kafka_conf_destroy(rd_conf);
//      }
//
//      rd_kafka_resp_err_t err = RD_KAFKA_RESP_ERR_NO_ERROR;
//      const struct rd_kafka_group_list *grplist = nullptr;
//      int retries = 5; // 5 sec
//
//      /* FIXME: Wait for broker to come up. This should really be abstracted by librdkafka. */
//      do {
//
//        if (err) {
//          DLOG(ERROR) << "retrying group list in 1s, ec: " << rd_kafka_err2str(err);
//          std::this_thread::sleep_for(1s);
//        } else if (grplist) {
//          // the previous call must have succeded bu returned an empty list -
//          // bug in rdkafka when using ssl - we cannot separate this from a non existent group - we must retry...
//          LOG_IF(FATAL, grplist->group_cnt != 0) << "group list should be empty";
//          rd_kafka_group_list_destroy(grplist);
//          LOG(INFO) << "got empty group list - retrying in 1s";
//          std::this_thread::sleep_for(1s);
//        }
//        err = rd_kafka_list_groups(rk, group_id.c_str(), &grplist, 5000);
//        DLOG_IF(INFO, err!=0) << "rd_kafka_list_groups: " << group_id.c_str() << ", res: " << err;
//        DLOG_IF(INFO, err==0) << "rd_kafka_list_groups: " << group_id.c_str() << ", res: OK" << " grplist->group_cnt: "
//                              << grplist->group_cnt;
//      } while ((err == RD_KAFKA_RESP_ERR__TRANSPORT ||
//                err == RD_KAFKA_RESP_ERR_GROUP_LOAD_IN_PROGRESS) || (err == 0 && grplist && grplist->group_cnt == 0) &&
//                                                                    retries-- > 0);
//
//      if (err) {
//        LOG(ERROR) << "failed to retrieve groups, ec: " << rd_kafka_err2str(err);
//        rd_kafka_destroy(rk);
//        throw std::runtime_error(rd_kafka_err2str(err));
//      }
//
//      bool found = (grplist->group_cnt > 0);
//      rd_kafka_group_list_destroy(grplist);
//      rd_kafka_destroy(rk);
//      return found;
//    }
  }//namespace kafka
} // kspp


