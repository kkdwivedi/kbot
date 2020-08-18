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

enum IRCUserCapability : uint64_t {
  kQuit = (1ULL << 0),
  kPart = (1ULL << 1),
  kJoin = (1ULL << 2),
  kMax = UINT64_MAX,
};

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
  ssize_t LOGIN(std::string_view nickname, std::string_view password = "") const;
  ssize_t NICK(std::string_view nickname) const;
  ssize_t JOIN(std::string_view channel) const;
  ssize_t PART(std::string_view channel) const;
  ssize_t PRIVMSG(std::string_view recipient, std::string_view msg) const;
  ssize_t QUIT(std::string_view msg = "") const;
  // Low-level API
  ssize_t send_msg(std::string_view msg) const;
  std::string recv_msg() const;
  // Friends/Misc
  friend std::ostream& operator<<(std::ostream& o, const IRC& i);
};

struct IRCUser {
  std::string_view nickname;
  std::string_view hostname;
  std::string_view username;
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
      }
      if (!prev) throw "Malformed tag";
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
      if (command == "")
	throw "No command present";
    }
    prev = i;
    while ((i = line.find(' ', prev)) != line.npos) {
      param_vec.push_back(std::string_view(&line[prev], &line[i]));
      prev = i + 1;
    }
    param_vec.push_back(std::string_view(&line[prev]));
    if (param_vec.size() == 0 || param_vec[0] == "")
      throw "Bad parameter present";
    line.shrink_to_fit();
    param_vec.shrink_to_fit();
    tag_kv.shrink_to_fit();
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
    if (source.find('!') != source.npos) {
      IRCUser u = {};
      size_t src_size = source.size();
      size_t cur = 0;
      size_t next = std::min(source.find('!', cur), src_size);
      assert(cur <= next);
      u.nickname = std::string_view(&source[cur], &source[next]);
      if (next == src_size) return u;
      cur = next + 1;
      next = std::min(source.find('@', next), src_size);
      assert(cur <= next);
      u.username = std::string_view(&source[cur], &source[next]);
      if (next == src_size) return u;
      cur = next + 1;
      next = src_size;
      assert(cur <= next);
      u.hostname = std::string_view(&source[cur], &source[next]);
      return u;
    }
    throw std::runtime_error("Source is not a user");
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

  std::string_view get_channel() const
  {
    if (source.find('.') != source.npos)
      throw std::runtime_error("Called on server message");
    return get_parameters()[0];
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

// Predicates

namespace message {

inline bool is_user_capable(const IRCUser& u, const uint64_t cap_mask, std::string_view channel)
{
  // TODO: store admins to a persistent DB
  static uint64_t kkd_cap_mask = UINT64_MAX;
  if (u.nickname == "kkd" && u.username == "~memxor" && u.hostname == "unaffiliated/kartikeya") {
    if ((kkd_cap_mask & cap_mask) != 0)
      return true;
  }
  return false;
}

inline bool is_server_message(std::string_view source)
{
  if (source.find('!') == source.npos)
    return false;
  return true;
}

inline bool is_message_quit(const IRCMessage& m)
{
  if (!is_server_message(m.get_source()))
    return false;
  if (!is_user_capable(m.get_user(), kQuit, m.get_channel()))
    return false;
  if (!(m.get_parameters()[1] == ":,quit"))
    return false;
  return true;
}

} // namespace message

} // namespace kbot
