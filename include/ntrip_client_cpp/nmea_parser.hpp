#pragma once

#include <string>

#include "ntrip_client_cpp/logger.hpp"

namespace ntrip_client_cpp 
{

constexpr int NMEA_DEFAULT_MAX_LENGTH = 82;
constexpr int NMEA_DEFAULT_MIN_LENGTH = 3;
constexpr char NMEA_CHECKSUM_SEPARATOR = '*';

class NMEAParser 
{
public:
  explicit NMEAParser(const Logger& logger = Logger());

  static int checksum(const std::string& sentence_no_checksum);
  static std::string lat_dd_to_dmm(double decimal_degrees);
  static std::string lon_dd_to_dmm(double decimal_degrees);

  bool is_valid_sentence(const std::string& sentence);

  // Public config (matches Python attributes).
  int nmea_max_length = NMEA_DEFAULT_MAX_LENGTH;
  int nmea_min_length = NMEA_DEFAULT_MIN_LENGTH;

private:
  Logger logger_;
};

}  // namespace ntrip_client_cpp