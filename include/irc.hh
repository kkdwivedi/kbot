#pragma once

#include <glog/logging.h>
#include <sys/types.h>

#include <cassert>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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

// Low-level API to interact with the IRC server

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
  static constexpr const char* StateToString(const enum IRCService s);
  // Command API
  ssize_t Login(std::string_view nickname,
                std::string_view password = "") const;
  ssize_t Nick(std::string_view nickname) const;
  ssize_t Join(std::string_view channel) const;
  ssize_t Part(std::string_view channel) const;
  ssize_t PrivMsg(std::string_view recipient, std::string_view msg) const;
  ssize_t Quit(std::string_view msg = "") const;
  // Low-level API
  ssize_t SendMsg(std::string_view msg) const;
  std::string RecvMsg() const;
  // Friends/Misc
  friend std::ostream& operator<<(std::ostream& o, const IRC& i);
};

// User record for IRC messages not from the server

struct IRCUser {
  std::string_view nickname;
  std::string_view hostname;
  std::string_view username;
  std::string_view channel;
};

// Destructured raw IRC message

enum class IRCMessageType : uint8_t {
  _DEFAULT,
  LOGIN,
  NICK,
  JOIN,
  PART,
  PRIVMSG,
};

class IRCMessage {
 protected:
  std::string line;
  std::string_view tags;
  std::vector<std::pair<std::string_view, std::string_view>> tag_kv;
  std::string_view source;
  std::string_view command;
  std::vector<std::string_view> param_vec;

 public:
  IRCMessageType message_type = IRCMessageType::_DEFAULT;

  explicit IRCMessage(std::string_view l,
                      IRCMessageType t = IRCMessageType::_DEFAULT) try
      : line(l), message_type(t) {
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
        val = std::string_view(&tags[prev],
                               &tags[i == tags.npos ? tags.size() : i]);
        tag_kv.push_back({key, val});
        if (i == tags.npos)
          break;
        else
          prev = i + 1;
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
    if (source != "" && source.find('!') == source.npos &&
        message_type == IRCMessageType::PRIVMSG) {
      throw std::runtime_error("Bad source: Server message");
    }
    if (line[i] != '\0') {
      prev = i;
      i = line.find(' ', prev);
      if (i == line.npos) {
        throw "No parameter present";
      }
      command = std::string_view(&line[prev], &line[i++]);
      if (command == "") throw "No command present";
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
  } catch (const char* e) {
    DLOG(ERROR) << "Failure: " << e << " (" << l << ')';
    throw std::runtime_error("IRCMessage parsing error");
  }

  IRCMessage(const IRCMessage&) = delete;
  IRCMessage& operator=(IRCMessage&) = delete;
  IRCMessage(IRCMessage&& m) {
    message_type = m.message_type;
    m.message_type = IRCMessageType::_DEFAULT;
    line = std::move(m.line);
    tags = std::move(m.tags);
    tag_kv = std::move(m.tag_kv);
    source = std::move(m.source);
    command = std::move(m.command);
    param_vec = std::move(m.param_vec);
  }
  IRCMessage& operator=(IRCMessage&&) = delete;
  virtual ~IRCMessage() = default;

  std::string_view GetTags() const { return tags; }

  const std::vector<std::pair<std::string_view, std::string_view>>& get_tag_kv()
      const {
    return tag_kv;
  }

  std::string_view GetSource() const { return source; }

  std::string_view GetCommand() const { return command; }

  const std::vector<std::string_view>& GetParameters() const {
    return param_vec;
  }

  // Friends/Misc
  friend std::ostream& operator<<(std::ostream& o, const IRCMessage& m) {
    if (m.tags != "") o << "Tags=" << m.tags << ' ';
    o << "Source=" << m.source << " Command=" << m.command << " Param=";
    for (auto& sv : m.param_vec) o << sv << ' ';
    return o << '\n';
  }
};

// Message Types

class IRCMessagePrivmsg : public IRCMessage {
 public:
  IRCMessagePrivmsg(std::string_view l)
      : IRCMessage(std::move(l), IRCMessageType::PRIVMSG) {}
  IRCMessagePrivmsg(IRCMessage&& m) : IRCMessage(std::move(m)) {}
  IRCUser GetUser() const {
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
    u.channel = std::string_view(param_vec.at(0));
    return u;
  }
  std::string_view GetChannel() const { return param_vec.at(0); }
};

// Predicate functions

namespace Message {

inline bool IsUserCapable(const IRCUser& u, const uint64_t cap_mask) {
  // TODO: store admins to a persistent DB
  static uint64_t kkd_cap_mask = UINT64_MAX;
  if (u.nickname == "kkd" && u.username == "~memxor" &&
      u.hostname == "unaffiliated/kartikeya") {
    if ((kkd_cap_mask & cap_mask) != 0) return true;
  }
  return false;
}

inline bool IsUserMessage(std::string_view source) {
  if (source.find('!') == source.npos) return false;
  return true;
}

inline bool IsServerMessage(std::string_view source) {
  return !IsUserMessage(std::move(source));
}

inline bool IsQuitMessage(const IRCMessage& m) {
  if (IsServerMessage(m.GetSource())) return false;
  auto& params = m.GetParameters();
  if (params.size() > 1 && !(params.at(1) == ":,quit")) return false;
  if (m.message_type != IRCMessageType::PRIVMSG) return false;
  if (!IsUserCapable(static_cast<const IRCMessagePrivmsg&>(m).GetUser(), kQuit))
    return false;
  return true;
}

inline bool IsPrivMsgMessage(const IRCMessage& m) {
  if (IsServerMessage(m.GetSource())) return false;
  if (m.GetCommand() != "PRIVMSG") return false;
  return true;
}

}  // namespace Message

}  // namespace kbot
