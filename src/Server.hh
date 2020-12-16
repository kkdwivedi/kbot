#pragma once

#include <absl/container/flat_hash_map.h>
#include <dlfcn.h>
#include <glog/logging.h>

#include <Database.hh>
#include <IRC.hh>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>

namespace kbot {

enum class ServerState {
  kSetup,
  kConnected,
  kLoggedIn,
  kFailed,
  kMax,
};

constexpr const char *const ServerStateStringTable[(int)ServerState::kMax] = {
    "Uninitialized",
    "Connected",
    "Logged In",
    "Failed",
};

struct Channel;
class Manager;

// CommandPlugin
// These are automatically ref-counted, so each server user just needs to keep the Plugin object
// alive for itself while it's loaded, mapped to its internal user_command_map.

class CommandPlugin {
  void *handle = nullptr;

  void CloseHandle() {
    if (handle) {
      dlclose(handle);
    }
  }

 public:
  CommandPlugin() = default;
  CommandPlugin(const CommandPlugin &) = delete;
  CommandPlugin &operator=(const CommandPlugin &) = delete;
  CommandPlugin(CommandPlugin &&u) { std::swap(handle, u.handle); }
  CommandPlugin &operator=(CommandPlugin &&u) {
    dlclose(handle);
    handle = std::exchange(u.handle, nullptr);
    return *this;
  }
  ~CommandPlugin() { CloseHandle(); }

  using registration_callback_t = void (*)(void *);
  bool OpenHandle(std::string_view name);
  registration_callback_t GetRegistrationFunc(std::string_view plugin_name);
  registration_callback_t GetDeletionFunc(std::string_view plugin_name);
  registration_callback_t GetHelpFunc(std::string_view plugin_name);
};

class Server : public IRC {
  std::mutex server_mtx;
  std::atomic<ServerState> state = ServerState::kSetup;
  std::string address;
  uint16_t port;
  std::shared_mutex chan_mtx;
  absl::flat_hash_map<std::string, Channel> chan_map;
  std::mutex nick_mtx;
  std::string nickname;
  db::Database local_db;

  using callback_t = void (*)(Manager &, const IRCMessagePrivMsg &);

 public:
  std::shared_mutex user_command_mtx;
  absl::flat_hash_map<std::string, callback_t> user_command_map;
  std::shared_mutex plugins_map_mtx;
  absl::flat_hash_map<std::string, CommandPlugin> plugins_map;

  explicit Server(int sockfd, std::string address, uint16_t port, const char *nickname)
      : IRC(sockfd), address(std::move(address)), port(port), nickname(nickname) {}
  Server(const Server &) = delete;
  Server &operator=(const Server &) = delete;
  Server(Server &&);
  Server &operator=(Server &&);
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
  void UpdateNickname(std::string_view old_nick, std::string_view new_nick) {
    std::unique_lock lock(nick_mtx);
    if (old_nick == nickname) {
      nickname = new_nick;
    } else {
      LOG(ERROR) << "Old nickname doesn't match current nickname, no update made";
    }
  }
  void SetNickname(std::string_view nickname_) {
    auto r = IRC::Nick(nickname_.data());
    if (r < 0) {
      LOG(ERROR) << "Failed to initiate change to nickname: " << nickname_;
      return;
    }
  }
  // Channel API
  void JoinChannel(std::string_view channel);
  void UpdateJoinChannel(std::string_view channel);
  void UpdatePartChannel(std::string_view channel);
  bool SendChannel(std::string_view channel, std::string_view msg);
  bool SetTopic(std::string_view channel, std::string_view topic);
  std::string GetTopic(std::string_view channel);
  bool PartChannel(std::string_view channel);
  // Plugin API
  void AddPluginCommands(std::span<const std::pair<std::string, callback_t>> commands);
  void RemovePluginCommands(std::span<const std::string_view> commands);
};

struct Channel {
  enum State {
    JoinRequested,
    Joined,
    PartRequested,
    // Parted
  } state = JoinRequested;
};

std::optional<Server> ConnectionNew(std::string, uint16_t, const char *);

}  // namespace kbot
