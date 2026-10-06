#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <openssl/ssl.h>

#include "ntrip_client_cpp/ntrip_base.hpp"

namespace ntrip_client_cpp {

class NTRIPClient : public NTRIPBase 
{
public:
  static constexpr int DEFAULT_RTCM_TIMEOUT_SECONDS = 4;

  // Empty ntrip_version means "None" in Python terms.
  // Empty username AND empty password means "no authentication".
  NTRIPClient(const std::string& host,
              int port,
              const std::string& mountpoint,
              const std::string& ntrip_version,
              const std::string& username,
              const std::string& password,
              const Logger& logger = Logger());

  ~NTRIPClient() override;

  bool connect() override;
  void disconnect() override;
  void send_nmea(const std::string& sentence) override;
  std::vector<std::vector<uint8_t>> recv_rtcm() override;

  // Public configuration — matches Python attrs.
  bool ssl = false;
  std::string cert;     // empty == "None"
  std::string key;      // empty == "None"
  std::string ca_cert;  // empty == "None"

  // Public timeout — matches Python attr.
  int rtcm_timeout_seconds = DEFAULT_RTCM_TIMEOUT_SECONDS;

private:
  std::string form_request() const;
  bool socket_is_open();
  bool setup_ssl_context();
  ssize_t send_some(const void* data, size_t len);
  ssize_t recv_some(void* buf, size_t len);
  void close_socket();

  // Config
  std::string host_;
  int port_;
  std::string mountpoint_;
  std::string ntrip_version_;
  std::string basic_credentials_;  // empty => no Authorization header

  // Sockets / TLS
  int server_fd_ = -1;
  int raw_fd_ = -1;
  SSL_CTX* ssl_ctx_ = nullptr;
  SSL* ssl_ = nullptr;

  // Reconnect bookkeeping (verbatim from Python).
  int nmea_send_failed_count_ = 0;
  static constexpr int kNmeaSendFailedMax = 5;
  int read_zero_bytes_count_ = 0;
  static constexpr int kReadZeroBytesMax = 5;
  bool first_rtcm_received_ = false;
  double recv_rtcm_last_packet_timestamp_ = 0.0;
};

}  // namespace ntrip_client_cpp