#pragma once

#include <string>
#include <string_view>

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



} // namespace kbot
