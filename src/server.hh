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
  kMax,
};

constexpr int ServerStateMax = static_cast<int>(ServerState::kMax);

constexpr const char* const ServerStateStringTable[ServerStateMax] = {
  "Disconnected",
  "Connected",
  "Failed",
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

  static constexpr const char* state_to_string(const enum ServerState state);

  void dump_info() const;
  enum ServerState get_state() const;
  void set_state(const enum ServerState state) const;
  const std::string& get_address() const;
  uint16_t get_port() const;
  const std::string& get_nickname() const;
  void set_nickname(std::string_view nickname) const;

  friend std::ostream& operator<<(std::ostream& o, const Server& s);
};

std::shared_ptr<Server> connection_new(std::string_view, const uint16_t, const char *);
void connection_delete(const Server *);

} // namespace kbot
