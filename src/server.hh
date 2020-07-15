#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace kbot {

enum class server_state {
  kDisconnected,
  kConnected,
  kFailed,
};

class server {
  const int fd = -1;
  enum server_state state = server_state::kDisconnected;
  const std::string address;
  const uint16_t port;
  std::string nickname;
public:
  server(const int sockfd, const std::string addr, const uint16_t portnum, const char *nick);
  server(const server&) = delete;
  server& operator=(const server&) = delete;
  server(server&&) = delete;
  server& operator=(server&&) = delete;
  ~server();

  enum server_state get_state();
  void set_state(enum server_state state);
  const std::string& get_address();
  uint16_t get_port();
  const std::string& get_nickname();
  void set_nickname(std::string_view nickname);
};

std::shared_ptr<server> connection_new(std::string_view, const uint16_t, const char *);
void connection_delete(server *);

} // namespace kbot
