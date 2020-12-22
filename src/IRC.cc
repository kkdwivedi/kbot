#include <fmt/format.h>
#include <glog/logging.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <IRC.hh>
#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>

namespace kbot {

IRC::IRC(int sockfd) : fd(sockfd) { DLOG(INFO) << "Constructing IRC Backend: " << *this; }

IRC::~IRC() {
  if (fd >= 0) {
    DLOG(INFO) << "Destructing IRC Backend: " << *this;
    close(fd);
  }
}

constexpr const char *IRC::StateToString(enum IRCService s) {
  return IRCServiceStringTable[static_cast<int>(s)];
}

ssize_t IRC::Login(std::string_view nickname, std::string_view password) const {
  ssize_t fail = 0;
  std::string buf = fmt::format("\rUSER {} 0 * :{}\r\n", nickname, nickname);
  auto r = SendMsg(buf);
  if (r < 0) {
    PLOG(ERROR) << "Failed to send USER LOGIN message";
    fail = r;
  }
  r = Nick(nickname);
  if (r < 0) {
    PLOG(ERROR) << "Failed to send NICK LOGIN message";
    fail = r;
  }
  if (password != "") {
    r = PrivMsg("NickServ", fmt::format("identify {}", password));
    if (r < 0) PLOG(ERROR) << "Failed to send IDENTIFY LOGIN message";
    fail = r;
  }
  return fail;
}

ssize_t IRC::Nick(std::string_view nickname) const {
  std::string buf = fmt::format("\rNICK {}\r\n", nickname);
  auto r = SendMsg(buf);
  if (r < 0) PLOG(ERROR) << "Failed to send NICK message";
  return r;
}

ssize_t IRC::Join(std::string_view channel) const {
  std::string buf = fmt::format("\rJOIN {}\r\n", channel);
  auto r = SendMsg(buf);
  if (r < 0) PLOG(ERROR) << "Failed to send JOIN message";
  return r;
}

ssize_t IRC::Part(std::string_view channel) const {
  std::string buf = fmt::format("\rPART {}\r\n", channel);
  auto r = SendMsg(buf);
  if (r < 0) PLOG(ERROR) << "Failed to send PART message";
  return r;
}

ssize_t IRC::PrivMsg(std::string_view recipient, std::string_view msg) const {
  std::string buf = fmt::format("\rPRIVMSG {} :{}\r\n", recipient, msg);
  auto r = SendMsg(buf);
  if (r < 0) PLOG(ERROR) << "Failed to send PRIVMSG message";
  return r;
}

ssize_t IRC::Quit(std::string_view msg) const {
  if (fd >= 0) {
    std::string buf = fmt::format("\rQUIT {}\r\n", msg);
    auto r = SendMsg(buf);
    if (r < 0) {
      PLOG(ERROR) << "Failed to send QUIT message";
    } else {
      struct pollfd pfd {
        .fd = this->fd, .events = POLLOUT
      };
      poll(&pfd, 1, 5000);
    }
    return r;
  } else {
    return 0;
  }
}

ssize_t IRC::SendMsg(std::string_view msg) const {
  auto r = send(fd, msg.data(), msg.size(), MSG_NOSIGNAL);
  if (r < 0) {
    PLOG(ERROR) << "Failed to send data";
  }
  return r;
}

std::string IRC::RecvMsg() const {
  std::string buf;
  size_t r = 0;
  int tries = 5;
  do {
    buf.resize(r + 4096);
    ssize_t p = recv(fd, buf.data() + r, 4096, MSG_NOSIGNAL | MSG_DONTWAIT);
    if (p < 0) {
      if (errno != EAGAIN) {
        PLOG(ERROR) << "Failed to receive data";
        return "";
      }
      if (r)
        break;
      else
        return "";
    } else if (p == 0) {
      return "";
    }
    r += static_cast<size_t>(p);
  } while (buf[r - 1] != '\n' && tries--);
  // Discard remaining buffer if all tries failed
  if (buf[r - 1] != '\n') {
    while (--r && buf[r - 1] != '\n')
      ;
  }
  buf.resize(r);
  return buf;
}

std::ostream &operator<<(std::ostream &o, const IRC &i) {
  return o << "Service: " << i.StateToString(i.service_type) << " (SSL: "
           << "false"
           << ")";
}

// Message Types

namespace {

constexpr uint64_t GetCommandMaskAsUint(std::string_view command) {
  const char *p = command.data();
  uint64_t mask = 0;
  if (command.size() <= 8) {
    for (size_t i = 0; i < command.size(); i++) {
      mask |= static_cast<uint64_t>(static_cast<unsigned char>(*p++) & 0xff) << (i * 8);
    }
  }
  return mask;
}

}  // namespace

IRCMessageType GetSetIRCMessageType(IRCMessage &m) {
  switch (GetCommandMaskAsUint(m.GetCommand())) {
    case GetCommandMaskAsUint("PING"):
      return m.message_type = IRCMessageType::PING;
    case GetCommandMaskAsUint("LOGIN"):
      return m.message_type = IRCMessageType::LOGIN;
    case GetCommandMaskAsUint("NICK"):
      return m.message_type = IRCMessageType::NICK;
    case GetCommandMaskAsUint("JOIN"):
      return m.message_type = IRCMessageType::JOIN;
    case GetCommandMaskAsUint("PART"):
      return m.message_type = IRCMessageType::PART;
    case GetCommandMaskAsUint("PRIVMSG"):
      return m.message_type = IRCMessageType::PRIVMSG;
    case GetCommandMaskAsUint("KILL"):
    case GetCommandMaskAsUint("QUIT"):
      return m.message_type = IRCMessageType::QUIT;
    default:
      return m.message_type = IRCMessageType::_DEFAULT;
  }
}

IRCMessageVariant GetIRCMessageVariantFrom(IRCMessage &&m) {
  IRCMessageVariant mv;
  switch (GetSetIRCMessageType(m)) {
    case IRCMessageType::PING:
      mv.emplace<IRCMessagePing>(std::move(m));
      return mv;
      // case IRCMessageType::LOGIN:
    case IRCMessageType::NICK:
      assert(Message::IsUserMessage(m.GetSource()));
      mv.emplace<IRCMessageNick>(std::move(m));
      return mv;
    case IRCMessageType::JOIN:
      mv.emplace<IRCMessageJoin>(std::move(m));
      return mv;
    case IRCMessageType::PART:
      mv.emplace<IRCMessagePart>(std::move(m));
      return mv;
    case IRCMessageType::PRIVMSG:
      assert(!Message::IsServerMessage(m.GetSource()));
      if (!Message::IsQuitMessage(m)) {
        mv.emplace<IRCMessagePrivMsg>(std::move(m));
        return mv;
      }
      [[fallthrough]];
    case IRCMessageType::QUIT:
      mv.emplace<IRCMessageQuit>();
      return mv;
    default:
      mv.emplace<IRCMessage>(std::move(m));
      return mv;
  }
}

}  // namespace kbot
