#pragma once

#include <string>
#include <vector>

#include "ntrip_client_cpp/ntrip_base.hpp"

namespace ntrip_client_cpp 
{

class NTRIPSerialDevice : public NTRIPBase 
{
public:
  NTRIPSerialDevice(const std::string& port,
                    int baudrate,
                    const Logger& logger = Logger());

  ~NTRIPSerialDevice() override;

  bool connect() override;
  void disconnect() override;
  void send_nmea(const std::string& sentence) override;
  std::vector<std::vector<uint8_t>> recv_rtcm() override;

private:
  // Translate a numeric baud rate into the termios speed constant.
  // Returns false (and logs) if the rate is not a standard Linux value.
  bool baudrate_to_speed(int baudrate, unsigned int& speed_out);

  std::string port_;
  int baudrate_;
  int fd_ = -1;
};

}  // namespace ntrip_client_cpp