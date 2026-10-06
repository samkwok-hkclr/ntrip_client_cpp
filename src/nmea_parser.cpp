#include "ntrip_client_cpp/nmea_parser.hpp"

#include <cmath>
#include <cstdint>
#include <iomanip>
#include <sstream>

namespace ntrip_client_cpp {

NMEAParser::NMEAParser(const Logger& logger) : logger_(logger) {}

int NMEAParser::checksum(const std::string& sentence_no_checksum) 
{
  int calculated = 0;
  for (size_t i = 1; i < sentence_no_checksum.size(); ++i) 
  {
    calculated ^= static_cast<unsigned char>(sentence_no_checksum[i]);
  }
  return calculated;
}

static std::string dd_to_dmm(double decimal_degrees, int degrees_width) 
{
  double degrees_whole = 0.0;
  const double degrees_frac = std::modf(std::fabs(decimal_degrees), &degrees_whole);

  const double minutes_float = degrees_frac * 60.0;
  double minutes_whole = 0.0;
  const double minutes_frac = std::modf(minutes_float, &minutes_whole);

  const int degrees = static_cast<int>(degrees_whole);
  const int minutes = static_cast<int>(minutes_whole);
  const int subminutes = static_cast<int>(minutes_frac * 1e5);

  std::ostringstream oss;
  oss << std::setfill('0')
      << std::setw(degrees_width) << degrees
      << std::setw(2) << minutes
      << "."
      << std::setw(2) << subminutes;
  return oss.str();
}

std::string NMEAParser::lat_dd_to_dmm(double decimal_degrees) 
{
  return dd_to_dmm(decimal_degrees, 2);
}

std::string NMEAParser::lon_dd_to_dmm(double decimal_degrees) 
{
  return dd_to_dmm(decimal_degrees, 3);
}

bool NMEAParser::is_valid_sentence(const std::string& sentence) 
{
  const int len = static_cast<int>(sentence.size());

  if (len > nmea_max_length) 
  {
    logger_.log_warn("Received invalid NMEA sentence. Max length is " +
                     std::to_string(nmea_max_length) + ", but sentence was " +
                     std::to_string(len) + " bytes");
    logger_.log_warn("Sentence: " + sentence);
    return false;
  }
  if (len < nmea_min_length) 
  {
    logger_.log_warn("Received invalid NMEA sentence. We need at least " +
                     std::to_string(nmea_min_length) + " bytes to parse but got " +
                     std::to_string(len) + " bytes");
    logger_.log_warn("Sentence: " + sentence);
    return false;
  }
  if (sentence[0] != '$' && sentence[0] != '!') 
  {
    logger_.log_warn(std::string("Received invalid NMEA sentence. Sentence should begin with \"$\" or \"!\", but instead begins with ") +
                     sentence[0]);
    logger_.log_warn("Sentence: " + sentence);
    return false;
  }
  if (len < 2 || sentence.substr(len - 2) != "\r\n") 
  {
    logger_.log_warn("Received invalid NMEA sentence. Sentence should end with \\r\\n");
    logger_.log_warn("Sentence: " + sentence);
    return false;
  }
  const size_t sep = sentence.rfind(NMEA_CHECKSUM_SEPARATOR);
  if (sep == std::string::npos) 
  {
    logger_.log_warn(std::string("Received invalid NMEA sentence. Sentence should have a \"") +
                     NMEA_CHECKSUM_SEPARATOR +
                     "\" character to seperate the checksum, but we could not find it.");
    logger_.log_warn("Sentence: " + sentence);
    return false;
  }

  const std::string data = sentence.substr(0, sep);
  std::string checksum_str = sentence.substr(sep + 1);
  while (!checksum_str.empty() &&
         (checksum_str.back() == '\r' || checksum_str.back() == '\n' ||
          checksum_str.back() == ' '  || checksum_str.back() == '\t')) 
  {
    checksum_str.pop_back();
  }

  if (checksum_str.empty()) 
  {
    logger_.log_warn("Received invalid NMEA sentence. Empty checksum field");
    logger_.log_warn("Sentence: " + sentence);
    return false;
  }

  int expected_checksum = 0;
  try 
  {
    expected_checksum = std::stoi(checksum_str, nullptr, 16);
  } 
  catch (const std::exception&) 
  {
    logger_.log_warn("Received invalid NMEA sentence. Could not parse checksum: " +
                     checksum_str);
    logger_.log_warn("Sentence: " + sentence);
    return false;
  }

  const int calculated_checksum = checksum(data);
  if (expected_checksum != calculated_checksum) 
  {
    logger_.log_warn("Received invalid NMEA sentence. Checksum mismatch");
    std::ostringstream oss;
    oss << "Expected Checksum:   0x" << std::hex << std::uppercase << expected_checksum;
    logger_.log_warn(oss.str());
    oss.str("");
    oss.clear();
    oss << "Calculated Checksum: 0x" << std::hex << std::uppercase << calculated_checksum;
    logger_.log_warn(oss.str());
    return false;
  }

  return true;
}

}  // namespace ntrip_client_cpp