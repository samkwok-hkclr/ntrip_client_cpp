#pragma once

#include <functional>
#include <string>

namespace ntrip_client_cpp 
{

struct Logger 
{
  std::function<void(const std::string&)> error;
  std::function<void(const std::string&)> warn;
  std::function<void(const std::string&)> info;
  std::function<void(const std::string&)> debug;

  Logger() = default;

  Logger(std::function<void(const std::string&)> e,
         std::function<void(const std::string&)> w,
         std::function<void(const std::string&)> i,
         std::function<void(const std::string&)> d)
    : error(std::move(e)),
      warn(std::move(w)),
      info(std::move(i)),
      debug(std::move(d)) {}

  void log_err(const std::string& msg) const { if (error) error(msg); }
  void log_warn(const std::string& msg) const { if (warn) warn(msg); }
  void log_info(const std::string& msg) const { if (info) info(msg); }
  void log_debug(const std::string& msg) const { if (debug) debug(msg); }
};

}  // namespace ntrip_client_cpp