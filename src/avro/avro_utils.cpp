#include <kspp/avro/avro_utils.h>
#include <sstream>

namespace kspp{
  std::string normalize(const avro::ValidSchema &vs) {
    std::stringstream ss;
    vs.toJson(ss);
    std::string s = ss.str();
    // TBD we should strip type : string to string
    // strip whitespace
    s.erase(remove_if(s.begin(), s.end(), ::isspace), s.end());  // c version does not use locale...
    return s;
  }

  std::string to_string(avro::Type t){
    switch (t)
    {
      case avro::AVRO_STRING: return "AVRO_STRING";
      case avro::AVRO_BYTES: return "AVRO_BYTES";
      case avro::AVRO_INT: return "AVRO_INT";
      case avro::AVRO_LONG: return "AVRO_LONG";
      case avro::AVRO_FLOAT: return "AVRO_FLOAT";
      case avro::AVRO_DOUBLE: return "AVRO_DOUBLE";
      case avro::AVRO_BOOL: return "AVRO_BOOL";
      case avro::AVRO_NULL: return "AVRO_NULL";
      case avro::AVRO_RECORD: return "AVRO_RECORD";
      case avro::AVRO_ENUM: return "AVRO_ENUM";
      case avro::AVRO_ARRAY: return "AVRO_ARRAY";
      case avro::AVRO_UNION: return "AVRO_UNION";
      case avro::AVRO_FIXED: return "AVRO_FIXED";
      case avro::AVRO_NUM_TYPES: return "AVRO_NUM_TYPES";
      default:
        return "AVRO_UNKNOWN";
    };
  }
}