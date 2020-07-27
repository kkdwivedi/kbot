#pragma once

#include <iostream>
#include <cassert>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>
#include <stdexcept>

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
  ssize_t LOGIN(const std::string& nickname, const std::string& password = "") const;
  ssize_t NICK(const std::string& nickname) const;
  ssize_t JOIN(const std::string& channel) const;
  ssize_t PART(const std::string& channel) const;
  ssize_t PRIVMSG(const std::string& recipient, const std::string& msg) const;
  ssize_t QUIT(const std::string& msg = "") const;
  // Low-level API
  ssize_t send_msg(std::string_view msg) const;
  std::string recv_msg() const;
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
  explicit IRCMessage(std::string_view l) try : line(l)
  {
    size_t i = 0, prev = 0;
    if (line[i] == '@') {
      prev = i + 1;
      i = line.find(' ', prev);
      if (i == line.npos) {
	throw "No command present";
      }
      tags = std::string_view(&line[prev], &line[i++]);
      size_t i, prev = 0;
      while ((i = tags.find('=', prev)) != tags.npos) {
	std::string_view key(&tags[prev], &tags[i]), val;
	prev = i + 1;
	i = tags.find(';', prev);
	val = std::string_view(&tags[prev], &tags[i == tags.npos ? tags.size() : i]);
	tag_kv.push_back({key, val});
	if (i == tags.npos)
	  break;
	else prev = i + 1;
	if (!prev) throw "Malformed tag";
      }
    }
    if (line[i] == ':') {
      prev = i + 1;
      i = line.find(' ', prev);
      if (i == line.npos) {
	throw "No command present";
      }
      source = std::string_view(&line[prev], &line[i++]);
    }
    if (line[i] != '\0') {
      prev = i;
      i = line.find(' ', prev);
      if (i == line.npos) {
	throw "No parameter present";
      }
      command = std::string_view(&line[prev], &line[i++]);
    }
    prev = i;
    while ((i = line.find(' ', prev)) != line.npos) {
      param_vec.push_back(std::string_view(&line[prev], &line[i]));
      prev = i + 1;
    }
    param_vec.push_back(std::string_view(&line[prev]));
  } catch (const char *e) {
    std::clog << "Failure: " << e << " (" << l << ')' << '\n';
    throw std::runtime_error("IRCMessage parsing error");
  }

  IRCMessage(const IRCMessage&) = delete;
  IRCMessage& operator=(IRCMessage&) = delete;
  IRCMessage(IRCMessage&&) = delete;
  IRCMessage& operator=(IRCMessage&&) = delete;
  ~IRCMessage() = default;

  // Query API
  IRCUser get_user() const
  {
    return {};
  }

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
  friend std::ostream& operator<<(std::ostream& o, const IRCMessage& m)
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
