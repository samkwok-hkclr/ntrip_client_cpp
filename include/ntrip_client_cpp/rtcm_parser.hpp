#pragma once

#include <cstdint>
#include <vector>

#include "ntrip_client_cpp/logger.hpp"

namespace ntrip_client_cpp 
{

constexpr size_t RTCM_MAX_BUFFER_SIZE = 1024 * 100;

class RTCMParser 
{
public:
  explicit RTCMParser(const Logger& logger = Logger());

  // Feed a chunk of raw bytes in; get back zero or more fully-validated RTCM packets.
  // Each packet is the full frame (preamble + header + body + 3-byte CRC).
  std::vector<std::vector<uint8_t>> parse(const std::vector<uint8_t>& buffer);

private:
  uint32_t checksum(const std::vector<uint8_t>& packet) const;

  Logger logger_;

  // Cross-call state (matches Python).
  bool caching_data_ = false;
  std::vector<uint8_t> buffer_;
};

}  // namespace ntrip_client_cpp