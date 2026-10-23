#pragma once

#include <memory>
#include <string>
#include <atomic>
#include <mutex>
#include <thread>

#include <rclcpp/rclcpp.hpp>
#include <rclcpp_lifecycle/lifecycle_node.hpp>
#include <rclcpp_lifecycle/lifecycle_publisher.hpp>
#include <lifecycle_msgs/msg/state.hpp>
#include <lifecycle_msgs/msg/transition.hpp>
#include <std_msgs/msg/header.hpp>
#include <nmea_msgs/msg/sentence.hpp>
#include <sensor_msgs/msg/nav_sat_fix.hpp>
#include <rtcm_msgs/msg/message.hpp>

#include "ntrip_client_cpp/logger.hpp"
#include "ntrip_client_cpp/ntrip_base.hpp"

namespace ntrip_client_cpp 
{

using CallbackReturn = rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;

class NTRIPRosBase : public rclcpp_lifecycle::LifecycleNode 
{
public:
  explicit NTRIPRosBase(const std::string& node_name);
  ~NTRIPRosBase() override;

  // Lifecycle transitions
  CallbackReturn on_configure(const rclcpp_lifecycle::State&) override;
  CallbackReturn on_activate(const rclcpp_lifecycle::State&) override;
  CallbackReturn on_deactivate(const rclcpp_lifecycle::State&) override;
  CallbackReturn on_cleanup(const rclcpp_lifecycle::State&) override;
  CallbackReturn on_shutdown(const rclcpp_lifecycle::State&) override;
  CallbackReturn on_error(const rclcpp_lifecycle::State&) override;

protected:
  static Logger make_logger(const rclcpp::Logger& logger);

  std::unique_ptr<NTRIPBase> client_;

  // Base params
  int nmea_max_length_ = 0;
  int nmea_min_length_ = 0;
  int reconnect_attempt_max_ = 0;
  int reconnect_attempt_wait_seconds_ = 0;
  std::string rtcm_frame_id_;
  bool debug_ = false;

private:
  enum class ConnectState : uint8_t
  {
    Disconnected = 0,
    Connecting,
    Connected,
    Failed,
  };

  static const char * conn_state_to_string(const ConnectState s)
  {
    switch (s)
    {
      case ConnectState::Disconnected: return "Disconnected";
      case ConnectState::Connecting:   return "Connecting";
      case ConnectState::Connected:    return "Connected";
      case ConnectState::Failed:       return "Failed";
    }
    return "Unknown";
  }
  
  void set_connect_state(const ConnectState state);
  void connection_loop();
  void stop_connection_thread();

  void subscribe_nmea(const nmea_msgs::msg::Sentence::SharedPtr msg);
  void subscribe_fix(const sensor_msgs::msg::NavSatFix::SharedPtr msg);
  void publish_rtcm();

  std::atomic<ConnectState> connect_state_{ConnectState::Disconnected};
  std::atomic<bool>         connect_thread_active_{false};
  std::thread               connect_thread_;
  mutable std::mutex        client_mtx_;

  rclcpp_lifecycle::LifecyclePublisher<rtcm_msgs::msg::Message>::SharedPtr rtcm_pub_;
  rclcpp::Subscription<nmea_msgs::msg::Sentence>::SharedPtr nmea_sub_;
  rclcpp::Subscription<sensor_msgs::msg::NavSatFix>::SharedPtr fix_sub_;
  rclcpp::TimerBase::SharedPtr rtcm_timer_;
};

}  // namespace ntrip_client_cpp