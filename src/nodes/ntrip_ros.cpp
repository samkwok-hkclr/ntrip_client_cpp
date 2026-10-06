#include <csignal>
#include <memory>
#include <stdexcept>
#include <string>

#include <rclcpp/rclcpp.hpp>

#include "ntrip_client_cpp/ntrip_client.hpp"
#include "ntrip_client_cpp/ntrip_ros_base.hpp"

namespace 
{

// Python's "None" sentinel for optional string params.
std::string none_to_empty(const std::string& s) 
{
  return s == "None" ? std::string() : s;
}

class NTRIPRos : public ntrip_client_cpp::NTRIPRosBase 
{
public:
  NTRIPRos() : NTRIPRosBase("ntrip_client") 
  {
    // ---- Declare subclass-specific params (mirrors Python) ----
    const std::string host = declare_parameter<std::string>("host", "127.0.0.1");
    const int port = declare_parameter<int>("port", 2101);
    const std::string mountpoint = declare_parameter<std::string>("mountpoint", "mount");

    std::string ntrip_version = declare_parameter<std::string>("ntrip_version", "None");
    ntrip_version = none_to_empty(ntrip_version);

    const bool authenticate = declare_parameter<bool>("authenticate", false);
    std::string username = declare_parameter<std::string>("username", "");
    std::string password = declare_parameter<std::string>("password", "");

    if (authenticate) 
    {
      if (username.empty()) 
      {
        RCLCPP_ERROR(get_logger(), "Requested to authenticate, but param \"username\" was not set");
        throw std::runtime_error("missing username");
      }
      if (password.empty()) 
      {
        RCLCPP_ERROR(get_logger(), "Requested to authenticate, but param \"password\" was not set");
        throw std::runtime_error("missing password");
      }
    } 
    else 
    {
      // No auth -> empty strings -> no Authorization header.
      username.clear();
      password.clear();
    }

    const bool ssl_flag = declare_parameter<bool>("ssl", false);
    std::string cert = none_to_empty(declare_parameter<std::string>("cert", "None"));
    std::string key = none_to_empty(declare_parameter<std::string>("key", "None"));
    std::string ca_cert = none_to_empty(declare_parameter<std::string>("ca_cert", "None"));

    const int rtcm_timeout_seconds = declare_parameter<int>(
      "rtcm_timeout_seconds",
      ntrip_client_cpp::NTRIPClient::DEFAULT_RTCM_TIMEOUT_SECONDS);

    // ---- Build the client ----
    auto client = std::make_unique<ntrip_client_cpp::NTRIPClient>(
      host, port, mountpoint, ntrip_version, username, password,
      make_logger(get_logger()));

    client->ssl = ssl_flag;
    client->cert = cert;
    client->key = key;
    client->ca_cert = ca_cert;
    client->rtcm_timeout_seconds = rtcm_timeout_seconds;

    client_ = std::move(client);
  }
};

}  // namespace

int main(int argc, char** argv) 
{
  std::signal(SIGPIPE, SIG_IGN);
  rclcpp::init(argc, argv);

  std::shared_ptr<NTRIPRos> node;
  try 
  {
    node = std::make_shared<NTRIPRos>();
  } 
  catch (const std::exception& e) 
  {
    RCLCPP_FATAL(rclcpp::get_logger("ntrip_client"), "Init failed: %s", e.what());
    rclcpp::shutdown();
    return 1;
  }

  rclcpp::spin(node->get_node_base_interface());
  rclcpp::shutdown();
  return 0;
}