#include <string>
#include <chrono>
#pragma once

namespace kspp {
  namespace connect {

    enum rescrape_policy_t { RESCRAPE_OFF, LAST_QUERY_TS, CLIENT_TS };

    struct connection_params {

      std::string url;   // where relevant

      std::string host;  // where relevant
      int port;

      //authentication
      std::string user;
      std::string password;

      //resource id
      std::string database;

      std::string http_header;
    };

    struct table_params {
      std::chrono::seconds poll_intervall = std::chrono::seconds(60);
      size_t max_items_in_fetch=30000;
      rescrape_policy_t rescrape_policy = RESCRAPE_OFF;
      uint32_t rescrape_ticks = 1;
    };
    }
}
