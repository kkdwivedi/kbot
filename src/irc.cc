#include <glog/logging.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cassert>
#include <cstring>
#include <iostream>
#include <irc.hh>
#include <memory>
#include <string>
#include <string_view>

namespace kbot {

IRC::IRC(const int sockfd) : fd(sockfd) {
  DLOG(INFO) << "Constructing IRC Backend: " << *this;
  std::vector<int> v;
}

IRC::~IRC() {
  DLOG(INFO) << "Destructing IRC Backend: " << *this;
  close(fd);
}

constexpr const char* IRC::StateToString(enum IRCService s) {
  return IRCServiceStringTable[static_cast<int>(s)];
}

ssize_t IRC::Login(std::string_view nickname, std::string_view password) const {
  ssize_t fail = 0;
  std::string buf;
  buf += "\rUSER ";
  buf += nickname;
  buf += " 0 * :";
  buf += "nickname";
  buf += "\r\n";
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
    r = PrivMsg("NickServ", std::string("identify ") += password);
    if (r < 0) PLOG(ERROR) << "Failed to send IDENTIFY LOGIN message";
    fail = r;
  }
  return fail;
}

ssize_t IRC::Nick(std::string_view nickname) const {
  std::string buf;
  buf += "\rNICK ";
  buf += nickname;
  buf += "\r\n";
  auto r = SendMsg(buf);
  if (r < 0) PLOG(ERROR) << "Failed to send NICK message";
  return r;
}

ssize_t IRC::Join(std::string_view channel) const {
  std::string buf;
  buf += "\rJOIN ";
  buf += channel;
  buf += "\r\n";
  auto r = SendMsg(buf);
  if (r < 0) PLOG(ERROR) << "Failed to send JOIN message";
  return r;
}

ssize_t IRC::Part(std::string_view channel) const {
  std::string buf;
  buf += "\rPART ";
  buf += channel;
  buf += "\r\n";
  auto r = SendMsg(buf);
  if (r < 0) PLOG(ERROR) << "Failed to send PART message";
  return r;
}

ssize_t IRC::PrivMsg(std::string_view recipient, std::string_view msg) const {
  std::string buf;
  buf += "\rPRIVMSG ";
  buf += recipient;
  buf += " :";
  buf += msg;
  buf += "\r\n";
  auto r = SendMsg(buf);
  if (r < 0) PLOG(ERROR) << "Failed to send PRIVMSG message";
  return r;
}

ssize_t IRC::Quit(std::string_view msg) const {
  std::string buf;
  buf += "\rQUIT :";
  buf += msg;
  buf += "\r\n";
  auto r = SendMsg(buf);
  if (r < 0) PLOG(ERROR) << "Failed to send QUIT message";
  return r;
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
    if (p <= 0) {
      if (errno != EAGAIN)
        PLOG(ERROR) << "Failed to receive data: " << strerror(errno);
      if (r)
        break;
      else
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

std::ostream& operator<<(std::ostream& o, const IRC& i) {
  return o << "Service: " << i.StateToString(i.service_type) << " (SSL: "
           << "false"
           << ")";
}

}  // namespace kbot
