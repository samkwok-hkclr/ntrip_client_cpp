#include "ntrip_client_cpp/ntrip_serial_device.hpp"

#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace ntrip_client_cpp {

NTRIPSerialDevice::NTRIPSerialDevice(const std::string& port,
                                     int baudrate,
                                     const Logger& logger)
  : NTRIPBase(logger), port_(port), baudrate_(baudrate) {}

NTRIPSerialDevice::~NTRIPSerialDevice() 
{
  disconnect();
}

bool NTRIPSerialDevice::baudrate_to_speed(int baudrate, unsigned int& speed_out) 
{
  switch (baudrate) 
  {
    case 1200:    speed_out = B1200;    return true;
    case 2400:    speed_out = B2400;    return true;
    case 4800:    speed_out = B4800;    return true;
    case 9600:    speed_out = B9600;    return true;
    case 19200:   speed_out = B19200;   return true;
    case 38400:   speed_out = B38400;   return true;
    case 57600:   speed_out = B57600;   return true;
    case 115200:  speed_out = B115200;  return true;
    case 230400:  speed_out = B230400;  return true;
#ifdef B460800
    case 460800:  speed_out = B460800;  return true;
#endif
#ifdef B921600
    case 921600:  speed_out = B921600;  return true;
#endif
    default:
      return false;
  }
}

bool NTRIPSerialDevice::connect() 
{
  // Python pyserial: serial.Serial(port, baudrate) -> 8N1, no flow control,
  // non-blocking-ish read/write with a small internal timeout.
  fd_ = ::open(port_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (fd_ < 0) 
  {
    logger_.log_err("Unable to open serial port " + port_ +
                    " at baudrate " + std::to_string(baudrate_));
    logger_.log_err(std::string("Exception: ") + std::strerror(errno));
    fd_ = -1;
    return false;
  }

  // Opening a tty with O_NONBLOCK keeps the fd non-blocking for reads/writes.
  // pyserial's Serial defaults to blocking reads with a timeout; we want the
  // recv_rtcm() path to poll via ioctl(FIONREAD) and never block, and the
  // write path to drain in a loop. So we leave O_NONBLOCK in place.

  // Configure raw 8N1 (mirrors pyserial defaults).
  struct termios tty {};
  if (::tcgetattr(fd_, &tty) != 0) 
  {
    logger_.log_err("Unable to read termios for " + port_);
    logger_.log_err(std::string("Exception: ") + std::strerror(errno));
    ::close(fd_);
    fd_ = -1;
    return false;
  }

  unsigned int speed = 0;
  if (!baudrate_to_speed(baudrate_, speed)) 
  {
    logger_.log_err("Unsupported baudrate " + std::to_string(baudrate_) + " for port " + port_);
    ::close(fd_);
    fd_ = -1;
    return false;
  }

  ::cfmakeraw(&tty);              // 8N1, no echo, no canonical mode
  ::cfsetispeed(&tty, speed);
  ::cfsetospeed(&tty, speed);
  tty.c_cflag |= (CLOCAL | CREAD);
  tty.c_cflag &= ~CRTSCTS;        // no HW flow control (pyserial default)
  tty.c_cc[VMIN] = 0;             // non-blocking read semantics
  tty.c_cc[VTIME] = 0;

  if (::tcsetattr(fd_, TCSANOW, &tty) != 0) 
  {
    logger_.log_err("Unable to apply termios settings to " + port_);
    logger_.log_err(std::string("Exception: ") + std::strerror(errno));
    ::close(fd_);
    fd_ = -1;
    return false;
  }

  // Flush anything stale in the buffers (matches pyserial's reset on open).
  ::tcflush(fd_, TCIOFLUSH);

  connected_.store(true);
  logger_.log_info("Connected to serial port " + port_ + " at baudrate " + std::to_string(baudrate_));
  return true;
}

void NTRIPSerialDevice::disconnect() 
{
  connected_.store(false);
  if (fd_ >= 0) 
  {
    ::close(fd_);
    fd_ = -1;
  }
}

void NTRIPSerialDevice::send_nmea(const std::string& sentence_in) 
{
  if (!connected_.load()) 
  {
    logger_.log_warn("NMEA sent before port was connected, discarding NMEA");
    return;
  }

  std::string sentence = sentence_in;

  // Same quirk as the network client: matches literal '\\r\\n' (4 chars).
  if (sentence.size() >= 4 && sentence.substr(sentence.size() - 4) == "\\r\\n") 
  {
    sentence = sentence.substr(0, sentence.size() - 4) + "\r\n";
  } 
  else if (sentence.size() < 2 || sentence.substr(sentence.size() - 2) != "\r\n") 
  {
    sentence += "\r\n";
  }

  if (!nmea_parser.is_valid_sentence(sentence)) 
  {
    logger_.log_warn("Invalid NMEA sentence, not sending to server");
    return;
  }

  // Write in a loop (fd is O_NONBLOCK).
  const char* p = sentence.data();
  size_t remaining = sentence.size();
  bool failed = false;
  while (remaining > 0) 
  {
    const ssize_t n = ::write(fd_, p, remaining);
    if (n < 0) 
    {
      if (errno == EAGAIN || errno == EWOULDBLOCK) 
      {
        // Give the kernel a moment to drain the TX buffer.
        ::usleep(2000);  // 2 ms
        continue;
      }
      failed = true;
      break;
    }
    p += n;
    remaining -= static_cast<size_t>(n);
  }

  if (failed) 
  {
    logger_.log_err("Unable to send NMEA sentence to device, reconnecting...");
    reconnect();
    send_nmea(sentence);  // Faithful to Python's recursive retry.
  }
}

std::vector<std::vector<uint8_t>> NTRIPSerialDevice::recv_rtcm() 
{
  if (!connected_.load()) 
  {
    logger_.log_warn("RTCM requested before device was connected, returning empty list");
    return {};
  }

  int bytes_available = 0;
  if (::ioctl(fd_, FIONREAD, &bytes_available) != 0 || bytes_available <= 0) 
  {
    return {};
  }

  // read_all(): pull everything currently in the RX buffer.
  std::vector<uint8_t> data;
  data.reserve(static_cast<size_t>(bytes_available));

  uint8_t buf[1024];
  while (true) 
  {
    const ssize_t n = ::read(fd_, buf, sizeof(buf));
    if (n > 0) 
    {
      data.insert(data.end(), buf, buf + n);
      continue;
    }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) 
    {
      break;  // nothing more to read
    }
    if (n < 0) 
    {
      logger_.log_err("Unable to read RTCM from device, reconnecting...");
      reconnect();
      return {};
    }
    // n == 0 shouldn't happen with VMIN=0 + FIONREAD > 0, but if it does
    // treat it as "no more data".
    break;
  }

  if (data.empty()) 
  {
    return {};
  }

  logger_.log_debug("Read " + std::to_string(data.size()) + " bytes");
  return rtcm_parser.parse(data);
}

}  // namespace ntrip_client_cpp