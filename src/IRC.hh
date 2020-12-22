#pragma once

#include <glog/logging.h>
#include <sys/types.h>

#include <cassert>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace kbot {

enum IRCUserCapability : uint64_t {
  kQuit = (1ULL << 0),
  kPart = (1ULL << 1),
  kJoin = (1ULL << 2),
  kNickModify = (1ULL << 3),
  kMax = UINT64_MAX,
};

enum class IRCService {
  kAtheme,
  kMax,
};

inline constexpr int IRCServiceMax = static_cast<int>(IRCService::kMax);

inline constexpr const char *const IRCServiceStringTable[IRCServiceMax] = {
    "Atheme IRC Services",
};

// Low-level API to interact with the IRC server

class IRC {
  const enum IRCService service_type = IRCService::kAtheme;

 public:
  int fd = -1;
  explicit IRC(int sockfd);
  IRC(const IRC &) = delete;
  IRC &operator=(IRC &) = delete;
  IRC(IRC &&i) { fd = std::exchange(i.fd, -1); }
  IRC &operator=(IRC &&i) {
    if (this != &i) {
      fd = std::exchange(i.fd, -1);
    }
    return *this;
  }
  // Static Methods
  static constexpr const char *StateToString(const enum IRCService s);
  // Command API
  ssize_t Login(std::string_view nickname, std::string_view password = "") const;
  ssize_t Nick(std::string_view nickname) const;
  ssize_t Join(std::string_view channel) const;
  ssize_t Part(std::string_view channel) const;
  ssize_t PrivMsg(std::string_view recipient, std::string_view msg) const;
  ssize_t Quit(std::string_view msg = "") const;
  // Low-level API
  ssize_t SendMsg(std::string_view msg) const;
  std::string RecvMsg() const;
  // Friends/Misc
  friend std::ostream &operator<<(std::ostream &o, const IRC &i);

 protected:
  ~IRC();
};

// User record for IRC messages not from the server

struct IRCUser {
  std::string_view nickname;
  std::string_view hostname;
  std::string_view username;
};

// Destructured raw IRC message

enum class IRCMessageType : uint8_t {
  _DEFAULT,
  PING,
  LOGIN,
  NICK,
  JOIN,
  PART,
  PRIVMSG,
  QUIT,
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

  explicit IRCMessage(std::string_view l, IRCMessageType t = IRCMessageType::_DEFAULT) try
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
        val = std::string_view(&tags[prev], &tags[i == tags.npos ? tags.size() : i]);
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
    while (prev != line.npos) {
      i = line.find_first_of(' ', prev);
      auto end = i == line.npos ? line.size() : i;
      param_vec.push_back(std::string_view(&line[prev], &line[end]));
      if (i == line.npos) break;
      prev = line.find_first_not_of(' ', i + 1);
    }
    if (param_vec.size() == 0 || param_vec[0] == "") throw "Bad parameter present";
    line.shrink_to_fit();
    param_vec.shrink_to_fit();
    tag_kv.shrink_to_fit();
  } catch (const char *e) {
    DLOG(ERROR) << "Failure: " << e << " (" << l << ')';
    throw std::runtime_error("IRCMessage parsing error");
  }

  IRCMessage(const IRCMessage &) = delete;
  IRCMessage &operator=(IRCMessage &) = delete;
  IRCMessage(IRCMessage &&) = default;
  IRCMessage &operator=(IRCMessage &&) = delete;
  ~IRCMessage() = default;

  std::string_view GetTags() const { return tags; }

  const std::vector<std::pair<std::string_view, std::string_view>> &GetTagKV() const {
    return tag_kv;
  }

  std::string_view GetSource() const { return source; }

  std::string_view GetCommand() const { return command; }

  const std::vector<std::string_view> &GetParameters() const { return param_vec; }

  // Friends/Misc
  friend std::ostream &operator<<(std::ostream &o, const IRCMessage &m) {
    if (m.tags != "") o << "Tags=" << m.tags << ' ';
    o << "Source=" << m.source << " Command=" << m.command << " Param=";
    for (auto &sv : m.param_vec) o << sv << ' ';
    return o << '\n';
  }
};

namespace Message {

IRCUser ParseSourceUser(std::string_view source);

}

// Message Types

class IRCMessagePing : public IRCMessage {
 public:
  IRCMessagePing(IRCMessage &&m) : IRCMessage(std::move(m)) {}
  std::string_view GetPongParameter() const { return param_vec.at(0); }
};

class IRCMessageNick : public IRCMessage {
 public:
  IRCMessageNick(IRCMessage &&m) : IRCMessage(std::move(m)) {}
  std::string_view GetNewNickname() const { return param_vec.at(0).substr(1); }
  IRCUser GetUser() const { return Message::ParseSourceUser(source); }
};

class IRCMessageJoin : public IRCMessage {
 public:
  IRCMessageJoin(IRCMessage &&m) : IRCMessage(std::move(m)) {}
  std::string_view GetChannel() const { return param_vec.at(0); }
};

class IRCMessagePart : public IRCMessage {
 public:
  IRCMessagePart(IRCMessage &&m) : IRCMessage(std::move(m)) {}
  std::string_view GetChannel() const { return param_vec.at(0); }
};

class IRCMessagePrivMsg : public IRCMessage {
 public:
  IRCMessagePrivMsg(IRCMessage &&m) : IRCMessage(std::move(m)) {
    // The buffer name is always present in the parameter, everything else is optional
    assert(param_vec.size() >= 1);
  }
  IRCUser GetUser() const { return Message::ParseSourceUser(source); }
  std::string_view GetChannel() const { return param_vec.at(0); }
  std::string_view GetUserCommand() const { return param_vec.at(1); }
  std::vector<std::string_view> GetUserCommandParameters() const {
    // First is channel, then the user command itself, so skip those two
    if (param_vec.size() < 2) {
      throw std::out_of_range("Not adequate parameters for user command");
    }
    return std::vector<std::string_view>(param_vec.begin() + 2, param_vec.end());
  }
};

struct IRCMessageQuit {};

// Predicate functions

namespace Message {

inline bool IsUserCapable(const IRCUser &u, const uint64_t cap_mask) {
  // TODO: store admins to a persistent DB
  static uint64_t kkd_cap_mask = UINT64_MAX;
  if (u.nickname == "kkd" && u.username == "~memxor" && u.hostname == "unaffiliated/kartikeya") {
    if ((kkd_cap_mask & cap_mask) != 0) return true;
  }
  return false;
}

inline bool IsUserMessage(std::string_view source) {
  if (source.find('!') == source.npos) return false;
  return true;
}

inline bool IsServerMessage(std::string_view source) { return !IsUserMessage(std::move(source)); }

inline bool IsQuitMessage(const IRCMessage &m) {
  if (IsServerMessage(m.GetSource())) return false;
  auto &params = m.GetParameters();
  if (params.size() > 1 && !(params.at(1) == ":,quit")) return false;
  if (m.message_type != IRCMessageType::PRIVMSG) return false;
  if (!IsUserCapable(ParseSourceUser(m.GetSource()), kQuit)) return false;
  return true;
}

inline bool IsPingMessage(const IRCMessage &m) {
  if (m.GetCommand() == "PING") return true;
  return false;
}

inline bool IsPrivMsgMessage(const IRCMessage &m) {
  if (IsServerMessage(m.GetSource())) return false;
  if (m.GetCommand() != "PRIVMSG") return false;
  return true;
}

inline IRCUser ParseSourceUser(std::string_view source) {
  if (!Message::IsUserMessage(source)) {
    throw std::runtime_error("Source parameter is not a valid IRCUser specification");
  }
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

}  // namespace Message

using IRCMessageVariant =
    std::variant<std::monostate, IRCMessage, IRCMessagePing, IRCMessageNick, IRCMessageJoin,
                 IRCMessagePart, IRCMessagePrivMsg, IRCMessageQuit>;

IRCMessageType GetSetIRCMessageType(IRCMessage &m);
IRCMessageVariant GetIRCMessageVariantFrom(IRCMessage &&m);

}  // namespace kbot
