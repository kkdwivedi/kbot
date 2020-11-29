#pragma once

#include <glog/logging.h>

#include <atomic>
#include <cstdint>
#include <irc.hh>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>

namespace kbot {

enum class ServerState {
  kDisconnected,
  kConnected,
  kLoggedIn,
  kFailed,
  kMax,
};

enum class ChannelState {
  kMember,
  kVoiced,
  kHalfOp,
  kOp,
  kOwner,
  kInvalid,
  kMax,
};

constexpr int ServerStateMax = static_cast<int>(ServerState::kMax);
constexpr int ChannelStateMax = static_cast<int>(ChannelState::kMax);

constexpr const char *const ServerStateStringTable[ServerStateMax] = {
    "Disconnected",
    "Logged In",
    "Connected",
    "Failed",
};

constexpr const char *const ChannelStateStringTable[ChannelStateMax] = {
    "Member", "Voiced", "HalfOperator", "Operator", "Owner", "Invalid",
};

struct Channel;

class Server : public IRC {
  std::atomic<ServerState> state = ServerState::kDisconnected;
  std::string address;
  uint16_t port;
  std::shared_mutex chan_mtx;
  std::unordered_map<std::string, std::unique_ptr<Channel>> chan_map;
  std::mutex nick_mtx;
  std::string nickname;

 public:
  explicit Server(int sockfd, std::string address, uint16_t port, const char *nickname)
      : IRC(sockfd), address(std::move(address)), port(port), nickname(nickname) {}
  Server(const Server &) = delete;
  Server &operator=(const Server &) = delete;
  Server(Server &&);
  Server &operator=(Server &&) = delete;
  ~Server() { IRC::Quit("Goodbye cruel world!"); }
  // Static Methods
  static constexpr const char *StateToString(const enum ServerState state) {
    return ServerStateStringTable[static_cast<int>(state)];
  }
  // Basic API
  void DumpInfo();
  ServerState GetState() { return state.load(std::memory_order_relaxed); }
  void SetState(const ServerState state);
  std::string GetAddress() { return address; }
  uint16_t GetPort() const { return port; }
  const std::string &GetNickname() {
    std::unique_lock lock(nick_mtx);
    return nickname;
  }
  void UpdateNickname(std::string_view nickname_) {
    std::unique_lock lock(nick_mtx);
    nickname = nickname_;
  }
  void SetNickname(std::string_view nickname_) {
    ssize_t r;
    {
      std::unique_lock lock(nick_mtx);
      r = IRC::Nick(nickname_.data());
    }
    if (r < 0) {
      LOG(ERROR) << "Failed to initiate change to nickname: " << nickname_;
      return;
    }
  }
  // Channel API
  void JoinChannel(std::string_view channel);
  void UpdateChannel(std::string_view channel);
  bool SendChannel(std::string_view channel, std::string_view msg);
  bool SetTopic(std::string_view channel, std::string_view topic);
  std::string GetTopic(std::string_view channel);
  void PartChannel(std::string_view channel);
};

struct Channel {
  Channel() = default;
  Channel(const Channel &) = delete;
  Channel &operator=(const Channel &) = delete;
  Channel(Channel &&) = delete;
  Channel &operator=(Channel &&) = delete;
  ~Channel() = default;
};

std::optional<Server> ConnectionNew(std::string, uint16_t, const char *);

}  // namespace kbot
