#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace kbot {

enum class ServerState {
  kDisconnected,
  kConnected,
  kFailed,
};

class Server {
  const int fd = -1;

  mutable std::mutex state_mtx;
  mutable enum ServerState state = ServerState::kDisconnected;

  const std::string address;
  const uint16_t port;

  mutable std::mutex nick_mtx;
  mutable std::string nickname;
public:
  explicit Server(const int sockfd, const std::string addr, const uint16_t portnum, const char *nick);
  Server(const Server&) = delete;
  Server& operator=(const Server&) = delete;
  Server(Server&&) = delete;
  Server& operator=(Server&&) = delete;
  ~Server();

  enum ServerState get_state() const;
  void set_state(enum ServerState state) const;
  const std::string& get_address() const;
  uint16_t get_port() const;
  const std::string& get_nickname() const;
  void set_nickname(std::string_view nickname) const;
};

std::shared_ptr<Server> connection_new(std::string_view, const uint16_t, const char *);
void connection_delete(Server *);

} // namespace kbot
