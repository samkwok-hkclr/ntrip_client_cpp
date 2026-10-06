#include "ntrip_client_cpp/ntrip_client.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <stdexcept>

#include <openssl/err.h>
#include <openssl/evp.h>

namespace ntrip_client_cpp 
{

namespace 
{

constexpr size_t kChunkSize = 1024;

const std::vector<std::string> kSourcetableResponses = {"SOURCETABLE 200 OK"};
const std::vector<std::string> kSuccessResponses = {
    "ICY 200 OK", "HTTP/1.0 200 OK", "HTTP/1.1 200 OK"};
const std::vector<std::string> kUnauthorizedResponses = {"401"};

double now_seconds() 
{
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return std::chrono::duration<double>(now).count();
}

std::string base64_encode(const std::string& in) 
{
  // 4 chars per 3 input bytes, rounded up.
  std::string out(4 * ((in.size() + 2) / 3), '\0');
  const int n = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(out.data()),
                                reinterpret_cast<const unsigned char*>(in.data()),
                                static_cast<int>(in.size()));
  out.resize(static_cast<size_t>(n));
  return out;
}

bool contains_any(const std::string& haystack, const std::vector<std::string>& needles) 
{
  for (const auto& needle : needles) 
  {
    if (haystack.find(needle) != std::string::npos) 
      return true;
  }
  return false;
}

}  // namespace

NTRIPClient::NTRIPClient(const std::string& host,
                         int port,
                         const std::string& mountpoint,
                         const std::string& ntrip_version,
                         const std::string& username,
                         const std::string& password,
                         const Logger& logger)
  : NTRIPBase(logger),
    host_(host),
    port_(port),
    mountpoint_(mountpoint),
    ntrip_version_(ntrip_version) 
{
  if (!username.empty() && !password.empty()) 
  {
    basic_credentials_ = base64_encode(username + ":" + password);
  }
}

NTRIPClient::~NTRIPClient() 
{
  disconnect();
}

bool NTRIPClient::setup_ssl_context() 
{
  ssl_ctx_ = SSL_CTX_new(TLS_client_method());
  if (!ssl_ctx_) 
  {
    logger_.log_err("Unable to create SSL context");
    return false;
  }

  // Python loads the system default CA store via create_default_context().
  SSL_CTX_set_default_verify_paths(ssl_ctx_);

  if (!ca_cert.empty()) 
  {
    if (SSL_CTX_load_verify_locations(ssl_ctx_, ca_cert.c_str(), nullptr) != 1) 
    {
      logger_.log_err("Unable to load CA cert: " + ca_cert);
      SSL_CTX_free(ssl_ctx_);
      ssl_ctx_ = nullptr;
      return false;
    }
  }

  if (!cert.empty()) 
  {
    if (SSL_CTX_use_certificate_file(ssl_ctx_, cert.c_str(), SSL_FILETYPE_PEM) != 1) 
    {
      logger_.log_err("Unable to load client cert: " + cert);
      SSL_CTX_free(ssl_ctx_);
      ssl_ctx_ = nullptr;
      return false;
    }
    if (!key.empty()) 
    {
      if (SSL_CTX_use_PrivateKey_file(ssl_ctx_, key.c_str(), SSL_FILETYPE_PEM) != 1) 
      {
        logger_.log_err("Unable to load client key: " + key);
        SSL_CTX_free(ssl_ctx_);
        ssl_ctx_ = nullptr;
        return false;
      }
    }
  }

  return true;
}

bool NTRIPClient::connect() 
{
  // Create the socket (blocking; we'll set timeouts via SO_RCVTIMEO/SO_SNDTIMEO).
  server_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd_ < 0) 
  {
    logger_.log_err("Unable to create socket");
    return false;
  }

  // Python: socket.settimeout(5)
  struct timeval tv {};
  tv.tv_sec = 5;
  tv.tv_usec = 0;
  ::setsockopt(server_fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  ::setsockopt(server_fd_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

  // Resolve host (Python does this inside connect()).
  struct addrinfo hints {};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  struct addrinfo* result = nullptr;
  const std::string port_str = std::to_string(port_);
  if (::getaddrinfo(host_.c_str(), port_str.c_str(), &hints, &result) != 0 || !result) 
  {
    logger_.log_err("Unable to connect socket to server at http://" + host_ + ":" + port_str);
    logger_.log_err("Exception: host resolution failed");
    close_socket();
    return false;
  }

  // Non-blocking connect with a 5s poll so we can honour the timeout.
  const int flags = ::fcntl(server_fd_, F_GETFL, 0);
  ::fcntl(server_fd_, F_SETFL, flags | O_NONBLOCK);

  int rc = ::connect(server_fd_, result->ai_addr, result->ai_addrlen);
  ::freeaddrinfo(result);

  if (rc < 0 && errno != EINPROGRESS) 
  {
    logger_.log_err("Unable to connect socket to server at http://" + host_ + ":" + port_str);
    logger_.log_err(std::string("Exception: ") + std::strerror(errno));
    close_socket();
    return false;
  }

  if (rc < 0) 
  {
    struct pollfd pfd {};
    pfd.fd = server_fd_;
    pfd.events = POLLOUT;
    const int prc = ::poll(&pfd, 1, 5000);
    if (prc <= 0) 
    {
      logger_.log_err("Unable to connect socket to server at http://" + host_ + ":" + port_str);
      logger_.log_err("Exception: connect timeout");
      close_socket();
      return false;
    }
    int so_error = 0;
    socklen_t so_len = sizeof(so_error);
    ::getsockopt(server_fd_, SOL_SOCKET, SO_ERROR, &so_error, &so_len);
    if (so_error != 0) 
    {
      logger_.log_err("Unable to connect socket to server at http://" + host_ + ":" + port_str);
      logger_.log_err(std::string("Exception: ") + std::strerror(so_error));
      close_socket();
      return false;
    }
  }

  // Restore blocking mode.
  ::fcntl(server_fd_, F_SETFL, flags);

  // Wrap in TLS if requested.
  if (ssl)
  {
    if (!setup_ssl_context()) 
    {
      close_socket();
      return false;
    }
    ssl_ = SSL_new(ssl_ctx_);
    if (!ssl_) 
    {
      logger_.log_err("Unable to allocate SSL object");
      close_socket();
      return false;
    }
    raw_fd_ = server_fd_;
    SSL_set_fd(ssl_, server_fd_);
    SSL_set_tlsext_host_name(ssl_, host_.c_str());  // SNI
    if (SSL_connect(ssl_) != 1) 
    {
      const unsigned long err = ERR_get_error();
      char err_buf[256] = {0};
      ERR_error_string_n(err, err_buf, sizeof(err_buf));
      logger_.log_err("SSL handshake failed with server at http://" + host_ + ":" + port_str);
      logger_.log_err(std::string("Exception: ") + err_buf);
      close_socket();
      return false;
    }
  }

  // Send HTTP GET request.
  const std::string request = form_request();
  if (send_some(request.data(), request.size()) < 0) 
  {
    logger_.log_err("Unable to send request to server at http://" + host_ + ":" + port_str);
    close_socket();
    return false;
  }

  // Read HTTP response (single recv, like Python).
  char response_buf[kChunkSize];
  const ssize_t n = recv_some(response_buf, sizeof(response_buf));
  if (n < 0) 
  {
    logger_.log_err("Unable to read response from server at http://" + host_ + ":" + port_str);
    close_socket();
    return false;
  }
  const std::string response(response_buf, static_cast<size_t>(n));

  // Success detection (substring containment, matching Python's `in`).
  if (contains_any(response, kSuccessResponses)) 
  {
    connected_.store(true);
  }

  // Known-error detection (Python's if/elif chain).
  bool known_error = false;
  if (contains_any(response, kSourcetableResponses)) 
  {
    logger_.log_warn("Received sourcetable response from the server. This probably means the mountpoint specified is not valid");
    known_error = true;
  }
  else if (contains_any(response, kUnauthorizedResponses)) 
  {
    logger_.log_warn("Received unauthorized response from the server. Check your username, password, and mountpoint to make sure they are correct.");
    known_error = true;
  } 
  else if (!connected_.load() && ntrip_version_.empty()) 
  {
    logger_.log_warn("Received unknown error from the server. Note that the NTRIP version was not specified in the launch file. This is not necesarilly the cause of this error, but it may be worth checking your NTRIP casters documentation to see if the NTRIP version needs to be specified.");
    known_error = true;
  }

  if (known_error || !connected_.load()) 
  {
    logger_.log_err("Invalid response received from http://" + host_ + ":" + port_str + "/" + mountpoint_);
    logger_.log_err("Response: " + response);
    connected_.store(false);
    close_socket();
    return false;
  }

  logger_.log_info("Connected to http://" + host_ + ":" + port_str + "/" + mountpoint_);
  return true;
}

void NTRIPClient::disconnect() 
{
  connected_.store(false);
  close_socket();
}

void NTRIPClient::send_nmea(const std::string& sentence_in) 
{
  if (!connected_.load()) 
  {
    logger_.log_warn("NMEA sent before client was connected, discarding NMEA");
    return;
  }

  std::string sentence = sentence_in;

  // Python quirk preserved: this matches the literal 4-char sequence
  // backslash-r-backslash-n, not an actual CRLF. Kept verbatim.
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

  if (send_some(sentence.data(), sentence.size()) < 0) 
  {
    logger_.log_warn("Unable to send NMEA sentence to server.");
    ++nmea_send_failed_count_;
    if (nmea_send_failed_count_ >= kNmeaSendFailedMax) 
    {
      logger_.log_warn("NMEA sentence failed to send to server " +
                       std::to_string(nmea_send_failed_count_) + " times, restarting");
      reconnect();
      nmea_send_failed_count_ = 0;
      send_nmea(sentence);  // Recursive retry (faithful to Python).
    }
  }
}

std::vector<std::vector<uint8_t>> NTRIPClient::recv_rtcm() 
{
  if (!connected_.load()) 
  {
    logger_.log_warn("RTCM requested before client was connected, returning empty list");
    return {};
  }

  const double now = now_seconds();
  if (now - recv_rtcm_last_packet_timestamp_ >= rtcm_timeout_seconds && first_rtcm_received_) 
  {
    logger_.log_err("RTCM data not received for " + std::to_string(rtcm_timeout_seconds) + " seconds, reconnecting");
    reconnect();
    first_rtcm_received_ = false;
  }

  // Non-blocking readiness check on the underlying fd.
  struct pollfd pfd {};
  pfd.fd = server_fd_;
  pfd.events = POLLIN;
  if (::poll(&pfd, 1, 0) <= 0) 
  {
    return {};
  }

  std::vector<uint8_t> data;
  while (true) 
  {
    uint8_t chunk[kChunkSize];
    const ssize_t n = recv_some(chunk, sizeof(chunk));
    if (n < 0) 
    {
      logger_.log_err("Error while reading " + std::to_string(kChunkSize) + " bytes from socket");
      if (!socket_is_open()) 
      {
        logger_.log_err("Socket appears to be closed. Reconnecting");
        reconnect();
        return {};
      }
      break;
    }
    if (n == 0) 
    {
      // Peer closed. Python: `chunk = b''` then `len(chunk) < CHUNK` -> break.
      break;
    }
    data.insert(data.end(), chunk, chunk + n);
    if (static_cast<size_t>(n) < kChunkSize) 
      break;
  }

  logger_.log_debug("Read " + std::to_string(data.size()) + " bytes");

  if (data.empty()) 
  {
    ++read_zero_bytes_count_;
    if (read_zero_bytes_count_ >= kReadZeroBytesMax) 
    {
      logger_.log_warn("Reconnecting because we received 0 bytes from the socket even though it said there was data available " +
                       std::to_string(read_zero_bytes_count_) + " times");
      reconnect();
      read_zero_bytes_count_ = 0;
      return {};
    }
    return {};
  }

  recv_rtcm_last_packet_timestamp_ = now_seconds();
  first_rtcm_received_ = true;
  return rtcm_parser.parse(data);
}

std::string NTRIPClient::form_request() const 
{
  std::string request_str;
  if (!ntrip_version_.empty()) 
  {
    request_str = "GET /" + mountpoint_ + " HTTP/1.0\r\nNtrip-Version: " +
                  ntrip_version_ + "\r\nUser-Agent: NTRIP ntrip_client_ros\r\n";
  } 
  else 
  {
    request_str = "GET /" + mountpoint_ + " HTTP/1.0\r\nUser-Agent: NTRIP ntrip_client_ros\r\n";
  }
  if (!basic_credentials_.empty()) 
  {
    request_str += "Authorization: Basic " + basic_credentials_ + "\r\n";
  }
  request_str += "\r\n";
  return request_str;
}

bool NTRIPClient::socket_is_open() 
{
  char buf[1];
  const ssize_t n = ::recv(server_fd_, buf, 1, MSG_DONTWAIT | MSG_PEEK);
  if (n == 0) 
  {
    return false;  // peer closed
  }
  if (n < 0) 
  {
    if (errno == EAGAIN || errno == EWOULDBLOCK) 
    {
      return true;  // would block, socket still open
    }
    if (errno == ECONNRESET) 
    {
      logger_.log_warn("Connection reset by peer");
      return false;
    }
    logger_.log_warn("Socket appears to be closed");
    logger_.log_warn(std::string("Exception: ") + std::strerror(errno));
    return false;
  }
  return true;
}

ssize_t NTRIPClient::send_some(const void* data, size_t len) 
{
  if (ssl_) 
  {
    const int n = SSL_write(ssl_, data, static_cast<int>(len));
    return n > 0 ? n : -1;
  }
  return ::send(server_fd_, data, len, 0);
}

ssize_t NTRIPClient::recv_some(void* buf, size_t len) 
{
  if (ssl_) 
  {
    const int n = SSL_read(ssl_, buf, static_cast<int>(len));
    if (n > 0) 
      return n;
    const int err = SSL_get_error(ssl_, n);
    if (err == SSL_ERROR_ZERO_RETURN) 
      return 0;  // clean close_notify
    return -1;  // timeout (WANT_READ/WANT_WRITE) or hard error
  }
  return ::recv(server_fd_, buf, len, 0);
}

void NTRIPClient::close_socket() 
{
  if (ssl_) 
  {
    SSL_shutdown(ssl_);
    SSL_free(ssl_);
    ssl_ = nullptr;
  }
  if (ssl_ctx_) 
  {
    SSL_CTX_free(ssl_ctx_);
    ssl_ctx_ = nullptr;
  }
  if (server_fd_ >= 0) 
  {
    ::shutdown(server_fd_, SHUT_RDWR);
    ::close(server_fd_);
    server_fd_ = -1;
  }
  raw_fd_ = -1;
}

}  // namespace ntrip_client_cpp