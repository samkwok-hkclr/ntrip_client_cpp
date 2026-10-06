#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include "ntrip_client_cpp/logger.hpp"
#include "ntrip_client_cpp/nmea_parser.hpp"
#include "ntrip_client_cpp/rtcm_parser.hpp"

namespace ntrip_client_cpp 
{

class NTRIPBase 
{
public:
  // Public constants (values verbatim from Python; note the "ATEMPT" typo kept
  // intentionally so the ROS node param name stays identical).
  static constexpr int DEFAULT_RECONNECT_ATTEMPT_MAX = 10;
  static constexpr int DEFAULT_RECONNECT_ATEMPT_WAIT_SECONDS = 5;

  explicit NTRIPBase(const Logger& logger = Logger());
  virtual ~NTRIPBase() = default;

  virtual bool connect() = 0;
  virtual void disconnect() = 0;
  virtual void send_nmea(const std::string& sentence) = 0;
  virtual std::vector<std::vector<uint8_t>> recv_rtcm() = 0;

  // Blocking reconnect loop. Matches the Python behavior (including the sleep).
  // Throws std::runtime_error after reconnect_attempt_max failed attempts.
  void reconnect();

  // Set the shutdown flag and disconnect. Called from the ROS node's stop().
  void shutdown();

  // Public parsers (kept public to match Python's direct attribute access).
  NMEAParser nmea_parser;
  RTCMParser rtcm_parser;

  // Public reconnect tuning knobs.
  int reconnect_attempt_max = DEFAULT_RECONNECT_ATTEMPT_MAX;
  int reconnect_attempt_wait_seconds = DEFAULT_RECONNECT_ATEMPT_WAIT_SECONDS;

protected:
  Logger logger_;

  // State flags (named after the Python attributes).
  std::atomic<bool> connected_{false};
  std::atomic<bool> shutdown_{false};

  // Python initialises this in the subclass (NTRIPClient.__init__). We
  // initialise it here instead — strictly safer and behavior-preserving.
  int reconnect_attempt_count_ = 0;
};

}  // namespace ntrip_client_cpp