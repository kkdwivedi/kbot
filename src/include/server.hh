#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#include "irc.hh"

namespace kbot {

enum class ServerState {
  kDisconnected,
  kConnected,
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

constexpr const char* const ServerStateStringTable[ServerStateMax] = {
  "Disconnected",
  "Connected",
  "Failed",
};

constexpr const char* const ChannelStateStringTable[ChannelStateMax] = {
  "Member",
  "Voiced",
  "HalfOperator",
  "Operator",
  "Owner",
  "Invalid",
};

class Channel;

class Server : public IRC {
  mutable std::mutex state_mtx;
  mutable enum ServerState state = ServerState::kDisconnected;
  const std::string address;
  const uint16_t port;
  mutable std::mutex nick_mtx;
  mutable std::string nickname;
  mutable std::mutex chan_mtx;
  std::unordered_map<std::size_t, std::unique_ptr<Channel>> chan_id_map;
  std::unordered_map<std::string, std::size_t> chan_string_map;
  std::size_t chan_id = 0;
public:
  using ChannelID = std::size_t;
  explicit Server(const int sockfd, const std::string addr, const uint16_t portnum, const char *nick);
  Server(const Server&) = delete;
  Server& operator=(const Server&) = delete;
  Server(Server&&) = delete;
  Server& operator=(Server&&) = delete;
  ~Server();
  // Static Methods
  static constexpr const char* state_to_string(const enum ServerState state);
  // Basic API
  void dump_info() const;
  enum ServerState get_state() const;
  void set_state(const enum ServerState state) const;
  const std::string& get_address() const;
  uint16_t get_port() const;
  const std::string& get_nickname() const;
  void set_nickname(std::string_view nickname) const;
  // Channel API
  ChannelID join_channel(const std::string& channel);
  bool send_channel(ChannelID id, std::string_view msg);
  bool set_topic(ChannelID id, std::string_view topic);
  std::string get_topic(ChannelID id);
  void part_channel(ChannelID id);
  // Friends/Misc
  friend std::ostream& operator<<(std::ostream& o, const Server& s);
};

class Channel {
  Server& sref;
  const std::string name;
  std::size_t id;
public:
  explicit Channel(Server& s, std::string_view namesv, std::size_t _id) : sref(s), name(namesv), id(_id) {}
  Channel(const Channel&) = delete;
  Channel& operator=(const Channel&) = delete;
  Channel(Channel&&) = delete;
  Channel& operator=(Channel&&) = delete;
  ~Channel() = default;
  const std::string& get_name()
  {
    return name;
  }
  std::size_t get_id()
  {
    return id;
  }
  bool send_msg(std::string_view msg);
  std::string get_topic();
  bool set_topic(std::string_view topic);
};

std::shared_ptr<Server> connection_new(std::string, const uint16_t, const char *);
void connection_delete(const Server *);

} // namespace kbot
