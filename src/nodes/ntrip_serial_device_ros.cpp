#include <csignal>
#include <memory>
#include <stdexcept>
#include <string>

#include <rclcpp/rclcpp.hpp>

#include "ntrip_client_cpp/ntrip_ros_base.hpp"
#include "ntrip_client_cpp/ntrip_serial_device.hpp"

namespace 
{

class NTRIPSerialDeviceRos : public ntrip_client_cpp::NTRIPRosBase 
{
public:
  NTRIPSerialDeviceRos() : NTRIPRosBase("ntrip_serial_device") 
  {
    const std::string port = declare_parameter<std::string>("port", "/dev/ttyS0");
    const int baudrate = declare_parameter<int>("baudrate", 115200);

    client_ = std::make_unique<ntrip_client_cpp::NTRIPSerialDevice>(
      port, baudrate, make_logger(get_logger()));
  }
};

}  // namespace

int main(int argc, char** argv) 
{
  // Harmless for the serial path; kept for consistency with the TCP node.
  std::signal(SIGPIPE, SIG_IGN);

  rclcpp::init(argc, argv);

  std::shared_ptr<NTRIPSerialDeviceRos> node;
  try 
  {
    node = std::make_shared<NTRIPSerialDeviceRos>();
  } 
  catch (const std::exception& e) 
  {
    RCLCPP_FATAL(rclcpp::get_logger("ntrip_serial_device"), "Failed to construct node: %s", e.what());
    rclcpp::shutdown();
    return 1;
  }

  rclcpp::spin(node->get_node_base_interface());
  rclcpp::shutdown();
  return 0;
}