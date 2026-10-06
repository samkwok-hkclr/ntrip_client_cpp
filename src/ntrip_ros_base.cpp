#include "ntrip_client_cpp/ntrip_ros_base.hpp"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <sstream>

#include "ntrip_client_cpp/nmea_parser.hpp"

namespace ntrip_client_cpp 
{

namespace 
{

bool env_debug_flag() 
{
  const char* raw = std::getenv("NTRIP_CLIENT_DEBUG");
  if (!raw) 
    return false;
  std::string s(raw);
  for (auto& c : s) 
    c = static_cast<char>(std::tolower(c));
  return s == "true" || s == "1";
}

Logger build_logger(rclcpp::Logger logger) 
{
  return Logger(
    [logger](const std::string& s) { RCLCPP_ERROR(logger, "%s", s.c_str()); },
    [logger](const std::string& s) { RCLCPP_WARN(logger, "%s", s.c_str()); },
    [logger](const std::string& s) { RCLCPP_INFO(logger, "%s", s.c_str()); },
    [logger](const std::string& s) { RCLCPP_DEBUG(logger, "%s", s.c_str()); });
}

}  // namespace

NTRIPRosBase::NTRIPRosBase(const std::string& node_name)
  : rclcpp_lifecycle::LifecycleNode(node_name) 
{
  const bool debug_param = declare_parameter<bool>("debug", false);
  debug_ = debug_param || env_debug_flag();

  rtcm_frame_id_ = declare_parameter<std::string>("rtcm_frame_id", "odom");
  nmea_max_length_ = declare_parameter<int>("nmea_max_length", NMEA_DEFAULT_MAX_LENGTH);
  nmea_min_length_ = declare_parameter<int>("nmea_min_length", NMEA_DEFAULT_MIN_LENGTH);
  const std::string rtcm_message_package = declare_parameter<std::string>("rtcm_message_package", "mavros_msgs");
  reconnect_attempt_max_ = declare_parameter<int>("reconnect_attempt_max", NTRIPBase::DEFAULT_RECONNECT_ATTEMPT_MAX);
  reconnect_attempt_wait_seconds_ = declare_parameter<int>("reconnect_attempt_wait_seconds", NTRIPBase::DEFAULT_RECONNECT_ATEMPT_WAIT_SECONDS);

  if (debug_) 
  {
    get_logger().set_level(rclcpp::Logger::Level::Debug);
  }

  if (rtcm_message_package != "rtcm_msgs") 
  {
    RCLCPP_WARN(get_logger(),
                "RTCM package '%s' not supported by C++ port; using rtcm_msgs.",
                rtcm_message_package.c_str());
  }
}

Logger NTRIPRosBase::make_logger(const rclcpp::Logger& logger) 
{
  return build_logger(logger);
}

// ---------------------------------------------------------------------------
// Lifecycle callbacks
// ---------------------------------------------------------------------------

CallbackReturn NTRIPRosBase::on_configure(const rclcpp_lifecycle::State&) 
{
  RCLCPP_INFO(get_logger(), "on_configure()");

  if (!client_) 
  {
    RCLCPP_ERROR(get_logger(), "No client set — did the subclass ctor run?");
    return CallbackReturn::FAILURE;
  }

  // Propagate base params into the client (same as Python).
  client_->nmea_parser.nmea_max_length = nmea_max_length_;
  client_->nmea_parser.nmea_min_length = nmea_min_length_;
  client_->reconnect_attempt_max = reconnect_attempt_max_;
  client_->reconnect_attempt_wait_seconds = reconnect_attempt_wait_seconds_;

  // Publisher is created here, activated in on_activate().
  rtcm_pub_ = create_publisher<rtcm_msgs::msg::Message>("rtcm", 10);

  // Subscriptions live across deactivate/activate; callbacks guard on state.
  nmea_sub_ = create_subscription<nmea_msgs::msg::Sentence>(
    "nmea", 
    10,
    [this](const nmea_msgs::msg::Sentence::SharedPtr msg) {
      subscribe_nmea(msg);
    });

  fix_sub_ = create_subscription<sensor_msgs::msg::NavSatFix>(
    "fix", 
    10,
    [this](const sensor_msgs::msg::NavSatFix::SharedPtr msg) {
      subscribe_fix(msg);
    });

  return CallbackReturn::SUCCESS;
}

CallbackReturn NTRIPRosBase::on_activate(const rclcpp_lifecycle::State&) 
{
  RCLCPP_INFO(get_logger(), "on_activate()");

  if (!client_) 
  {
    return CallbackReturn::FAILURE;
  }

  if (!client_->connect()) 
  {
    RCLCPP_ERROR(get_logger(), "Unable to connect");
    return CallbackReturn::FAILURE;
  }

  if (rtcm_pub_) 
  {
    rtcm_pub_->on_activate();
  }

  rtcm_timer_ = create_wall_timer(std::chrono::milliseconds(100), [this]() { publish_rtcm(); });

  return CallbackReturn::SUCCESS;
}

CallbackReturn NTRIPRosBase::on_deactivate(const rclcpp_lifecycle::State&) 
{
  RCLCPP_INFO(get_logger(), "on_deactivate()");

  if (rtcm_timer_) 
  {
    rtcm_timer_->cancel();
    rtcm_timer_.reset();
  }

  if (rtcm_pub_) 
  {
    rtcm_pub_->on_deactivate();
  }

  if (client_) 
  {
    client_->shutdown();
  }

  return CallbackReturn::SUCCESS;
}

CallbackReturn NTRIPRosBase::on_cleanup(const rclcpp_lifecycle::State&) 
{
  RCLCPP_INFO(get_logger(), "on_cleanup()");

  if (rtcm_timer_) 
  {
    rtcm_timer_->cancel();
    rtcm_timer_.reset();
  }

  nmea_sub_.reset();
  fix_sub_.reset();
  rtcm_pub_.reset();

  return CallbackReturn::SUCCESS;
}

CallbackReturn NTRIPRosBase::on_shutdown(const rclcpp_lifecycle::State& state) 
{
  RCLCPP_INFO(get_logger(), "on_shutdown() from %s", state.label().c_str());
  
  on_deactivate(state);
  on_cleanup(state);

  return CallbackReturn::SUCCESS;
}

CallbackReturn NTRIPRosBase::on_error(const rclcpp_lifecycle::State& state) 
{
  RCLCPP_ERROR(get_logger(), "on_error() from %s", state.label().c_str());

  if (rtcm_timer_) 
  {
    rtcm_timer_->cancel();
    rtcm_timer_.reset();
  }
  if (client_) 
  {
    client_->shutdown();
  }

  return CallbackReturn::SUCCESS;
}

// ---------------------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------------------

void NTRIPRosBase::subscribe_nmea(const nmea_msgs::msg::Sentence::SharedPtr msg) 
{
  if (!client_) 
    return;
  
  if (get_current_state().id() != lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE) 
    return;

  client_->send_nmea(msg->sentence);
}

void NTRIPRosBase::subscribe_fix(const sensor_msgs::msg::NavSatFix::SharedPtr msg) 
{
  if (!client_) 
    return;

  if (get_current_state().id() != lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE) 
    return;

  // Timestamp -> hhmmss.cc using LOCAL time (Python uses datetime.fromtimestamp,
  // which is local, not UTC — preserved here for parity).
  const double timestamp_secs =
    static_cast<double>(msg->header.stamp.sec) +
    static_cast<double>(msg->header.stamp.nanosec) * 1e-9;

  double intpart = 0.0;
  const double frac = std::modf(timestamp_secs, &intpart);
  const std::time_t t = static_cast<std::time_t>(intpart);
  struct std::tm tm_buf {};
  localtime_r(&t, &tm_buf);

  const int hour = tm_buf.tm_hour;
  const int minute = tm_buf.tm_min;
  const int second = tm_buf.tm_sec;
  const int microsecond = static_cast<int>(frac * 1e6);
  const int millisecond = static_cast<int>(microsecond * 1e-4);  // 0..99

  std::ostringstream nmea_utc_ss;
  nmea_utc_ss.fill('0');
  nmea_utc_ss.width(2); nmea_utc_ss << hour;
  nmea_utc_ss.width(2); nmea_utc_ss << minute;
  nmea_utc_ss.width(2); nmea_utc_ss << second;
  nmea_utc_ss << ".";
  nmea_utc_ss.width(2); nmea_utc_ss << millisecond;
  const std::string nmea_utc = nmea_utc_ss.str();

  // Directions.
  const std::string nmea_lat_direction = (msg->latitude < 0.0) ? "S" : "N";
  const std::string nmea_lon_direction = (msg->longitude < 0.0) ? "W" : "E";

  const std::string nmea_lat = NMEAParser::lat_dd_to_dmm(msg->latitude);
  const std::string nmea_lon = NMEAParser::lon_dd_to_dmm(msg->longitude);

  // Status -> GPGGA quality digit.
  int nmea_status = 0;
  switch (msg->status.status) 
  {
    case sensor_msgs::msg::NavSatStatus::STATUS_FIX:      
      nmea_status = 1; 
      break;
    case sensor_msgs::msg::NavSatStatus::STATUS_SBAS_FIX: 
      nmea_status = 2; 
      break;
    case sensor_msgs::msg::NavSatStatus::STATUS_GBAS_FIX: 
      nmea_status = 5; 
      break;
    default:                                              
      nmea_status = 0; 
      break;
  }

  const std::string sentence_no_checksum =
    "$GPGGA," + nmea_utc + "," + nmea_lat + "," + nmea_lat_direction + "," +
    nmea_lon + "," + nmea_lon_direction + "," +
    std::to_string(nmea_status) +
    ",05,1.0,100.0,M,-32.0,M,,0000";

  const int checksum = NMEAParser::checksum(sentence_no_checksum);

  std::ostringstream checksum_hex;
  checksum_hex << std::hex << checksum;

  const std::string nmea_sentence = sentence_no_checksum + "*" + checksum_hex.str() + "\r\n";

  client_->send_nmea(nmea_sentence);
}

void NTRIPRosBase::publish_rtcm() 
{
  if (!client_) 
    return;

  if (!rtcm_pub_ || !rtcm_pub_->is_activated()) 
    return;

  for (auto& raw : client_->recv_rtcm()) 
  {
    rtcm_msgs::msg::Message msg;
    msg.header.stamp = now();
    msg.header.frame_id = rtcm_frame_id_;
    msg.message = raw;
    rtcm_pub_->publish(msg);
  }
}

}  // namespace ntrip_client_cpp