#include "ntrip_client_cpp/ntrip_base.hpp"

#include <chrono>
#include <stdexcept>
#include <thread>

namespace ntrip_client_cpp 
{

NTRIPBase::NTRIPBase(const Logger& logger)
  : nmea_parser(logger), rtcm_parser(logger), logger_(logger) {}

void NTRIPBase::reconnect() 
{
  if (!connected_.load()) 
  {
    logger_.log_debug("Reconnect called while not connected, ignoring");
    return;
  }

  while (!shutdown_.load()) 
  {
    ++reconnect_attempt_count_;
    disconnect();
    const bool connect_success = connect();

    // Faithful port of the Python control flow, including its two quirks:
    //   (a) the error message stringifies reconnect_attempt_count AFTER it is
    //       reset to zero, so it always reports "0 times".
    //   (b) if attempt_count reaches max on the same iteration that connect()
    //       succeeds, the max-attempts branch wins and we throw.
    if (!connect_success && reconnect_attempt_count_ < reconnect_attempt_max) 
    {
      logger_.log_err("Reconnect failed. Retrying in " +
                      std::to_string(reconnect_attempt_wait_seconds) + " seconds");
      std::this_thread::sleep_for(std::chrono::seconds(reconnect_attempt_wait_seconds));
    } 
    else if (reconnect_attempt_count_ >= reconnect_attempt_max) 
    {
      const int attempts = reconnect_attempt_count_;
      reconnect_attempt_count_ = 0;
      
      throw std::runtime_error(
          "Reconnect was attempted " + std::to_string(reconnect_attempt_count_) +
          " times, but never succeeded (attempts before reset: " +
          std::to_string(attempts) + ")");
    } 
    else if (connect_success) 
    {
      reconnect_attempt_count_ = 0;
      break;
    }
  }
}

void NTRIPBase::shutdown() 
{
  shutdown_.store(true);
  disconnect();
}

}  // namespace ntrip_client_cpp