#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <sys/types.h>

namespace kbot {

enum class IRCService {
  kAtheme,
  kMax,
};

inline constexpr int IRCServiceMax = static_cast<int>(IRCService::kMax);

inline constexpr const char* const IRCServiceStringTable[IRCServiceMax] = {
  "Atheme IRC Services",
};

class IRC {
  const enum IRCService service_type = IRCService::kAtheme;
public:
  const int fd = -1;
  explicit IRC(const int sockfd);
  IRC(const IRC&) = delete;
  IRC& operator=(IRC&) = delete;
  IRC(IRC&&) = delete;
  IRC& operator=(IRC&&) = delete;
  virtual ~IRC();
  // Static Methods
  static constexpr const char* state_to_string(const enum IRCService s);
  // Command API
  ssize_t PONG(std::string& pingmsg);
  ssize_t LOGIN(const std::string& nickname, const std::string& password = "");
  ssize_t NICK(const std::string& nickname);
  ssize_t JOIN(const std::string& channel);
  ssize_t PART(const std::string& channel);
  ssize_t PRIVMSG(const std::string& recipient, const std::string& msg);
  ssize_t QUIT(const std::string& msg = "");
  // Low-level API
  ssize_t send_msg(std::string_view msg);
  std::string recv_msg();
  // Friends/Misc
  friend std::ostream& operator<<(std::ostream& o, IRC& i);
};

struct IRCUser {
  const std::string nickname;
  const std::string hostname;
  const std::string username;
  const std::string realname;
};

class IRCMessage {
  std::string line;
  std::string_view tags;
  std::vector<std::pair<std::string_view, std::string_view>> tag_kv;
  std::string_view source;
  std::string_view command;
  std::vector<std::string_view> param_vec;
public:
  explicit IRCMessage(std::string_view l);
  IRCMessage(const IRCMessage&) = delete;
  IRCMessage& operator=(IRCMessage&) = delete;
  IRCMessage(IRCMessage&&) = delete;
  IRCMessage& operator=(IRCMessage&&) = delete;
  ~IRCMessage() = default;
  // Query API
  IRCUser get_user() const;
  std::string_view get_tags() const;
  std::vector<std::pair<std::string_view, std::string_view>> get_tag_kv();
  std::string_view get_command() const;
  std::string_view get_parameters() const;
  // Friends/Misc
  friend std::ostream& operator<<(std::ostream& o, IRCMessage& m);
};

} // namespace kbot
