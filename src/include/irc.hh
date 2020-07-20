#pragma once

#include <iostream>
#include <cstring>
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
};

class IRCMessage {
  std::string line;
  std::string_view tags;
  std::vector<std::pair<std::string_view, std::string_view>> tag_kv;
  std::string_view source;
  std::string_view command;
  std::vector<std::string_view> param_vec;
public:
  explicit IRCMessage(std::string_view l) : line(l)
  {
    char delim = ' ';
    char *p = std::strtok(line.data(), &delim);
    if (!p) {
      std::clog << "Failed parsing TAGS!\n";
      return;
    }
    // TODO: escaping (https://ircv3.net/specs/extensions/message-tags.html)
    if (*p == '@') {
      // Tag prefix
      tags = ++p;
      // Process KV pairs
      while (*p) {
	// Parse key
	std::string_view key, val;
	char *q = strchr(p, '=');
	if (q) {
	  key = std::string_view(p, static_cast<size_t>(q - p));
	} else break;
	// Parse value
	p = ++q;
	if (!*p) break;
	if (*p == ';') {
	  p++;
	  goto push;
	} else {
	  q = strchrnul(p, ';');
	  val = std::string_view(p, static_cast<size_t>(q - p));
	}
	p = *q ? ++q : q;
      push:
	tag_kv.push_back({key, val});
      }
    } else tags = "";
    if (tags != "")
      p = std::strtok(nullptr, &delim);
    if (*p++ == ':') source = p;
    else source = "";
    p = std::strtok(nullptr, &delim);
    if (!p) {
      std::clog << "Failed parsing COMMAND!\n";
      return;
    }
    command = p;
    bool first_semi = false;
    while ((p = std::strtok(nullptr, &delim))) {
      if (*p == ':' && !first_semi) p++, first_semi = true;
      param_vec.push_back(p);
    }
    if (!p && param_vec.size() == 0) {
      std::clog << "Failed parsing PARAMETERS!\n";
      return;
    }
  }

  IRCMessage(const IRCMessage&) = delete;
  IRCMessage& operator=(IRCMessage&) = delete;
  IRCMessage(IRCMessage&&) = delete;
  IRCMessage& operator=(IRCMessage&&) = delete;
  ~IRCMessage() = default;

  // Query API
  IRCUser get_user() const;

  std::string_view get_tags() const
  {
    return tags;
  }

  const std::vector<std::pair<std::string_view, std::string_view>>& get_tag_kv() const
  {
    return tag_kv;
  }

  std::string_view get_source() const
  {
    return source;
  }

  std::string_view get_command() const
  {
    return command;
  }

  const std::vector<std::string_view>& get_parameters() const
  {
    return param_vec;
  }

  //Friends/Misc
  friend std::ostream& operator<<(std::ostream& o, IRCMessage& m)
  {
    if (m.tags != "")
      o << "Tags=" << m.tags << ' ';
    o << "Source=" << m.source << " Command=" << m.command << " Param=";
    for (auto& sv : m.param_vec)
      o << sv << ' ';
    return o << '\n';
  }
};

} // namespace kbot
