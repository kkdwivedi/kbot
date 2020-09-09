#include <iostream>
#include <cassert>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>

#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include <irc.hh>

namespace kbot {

IRC::IRC(const int sockfd) : fd(sockfd)
{
  std::clog << "Constructing IRC Backend: " << *this;
}

IRC::~IRC()
{
  std::clog << "Destructing IRC Backend: " << *this;
  close(fd);
}

constexpr const char* IRC::state_to_string(enum IRCService s)
{
  return IRCServiceStringTable[static_cast<int>(s)];
}

ssize_t IRC::LOGIN(std::string_view nickname, std::string_view password) const
{
  ssize_t fail = 0;
  std::string buf;
  buf += "\rUSER ";
  buf += nickname;
  buf += " 0 * :";
  buf += "nickname";
  buf += "\r\n";
  auto r = send_msg(buf);
  if (r < 0) {
    std::clog << "Failed to send USER LOGIN message: " << strerror(errno) << '\n';
    fail = r;
  }
  r = NICK(nickname);
  if (r < 0) {
    std::clog << "Failed to send NICK LOGIN message: " << strerror(errno) << '\n';
    fail = r;
  }
  if (password != "") {
    r = PRIVMSG("NickServ", std::string("identify ") += password);
    if (r < 0)
      std::clog << "Failed to send IDENTIFY LOGIN message: " << strerror(errno) << '\n';
    fail = r;
  }
  return fail;
}

ssize_t IRC::NICK(std::string_view nickname) const
{
  std::string buf;
  buf += "\rNICK ";
  buf += nickname;
  buf += "\r\n";
  auto r = send_msg(buf);
  if (r < 0)
    std::clog << "Failed to send NICK message: " << strerror(errno) << '\n';
  return r;
}

ssize_t IRC::JOIN(std::string_view channel) const
{
  std::string buf;
  buf += "\rJOIN ";
  buf += channel;
  buf += "\r\n";
  auto r = send_msg(buf);
  if (r < 0)
    std::clog << "Failed to send JOIN message: " << strerror(errno) << '\n';
  return r;
}

ssize_t IRC::PART(std::string_view channel) const
{
  std::string buf;
  buf += "\rPART ";
  buf += channel;
  buf += "\r\n";
  auto r = send_msg(buf);
  if (r < 0)
    std::clog << "Failed to send PART message: " << strerror(errno) << '\n';
  return r;
}

ssize_t IRC::PRIVMSG(std::string_view recipient, std::string_view msg) const
{
  std::string buf;
  buf += "\rPRIVMSG ";
  buf += recipient;
  buf += " :";
  buf += msg;
  buf += "\r\n";
  auto r = send_msg(buf);
  if (r < 0)
    std::clog << "Failed to send PRIVMSG message: " << strerror(errno) << '\n';
  return r;
}

ssize_t IRC::QUIT(std::string_view msg) const
{
  std::string buf;
  buf += "\rQUIT :";
  buf += msg;
  buf += "\r\n";
  auto r = send_msg(buf);
  if (r < 0)
    std::clog << "Failed to send QUIT message: " << strerror(errno) << '\n';
  return r;
}

ssize_t IRC::send_msg(std::string_view msg) const
{
  auto r = send(fd, msg.data(), msg.size(), MSG_NOSIGNAL);
  if (r < 0) {
    std::clog << "Failed to send data: " << strerror(errno) << '\n';
  }
  return r;
}

std::string IRC::recv_msg() const
{
  std::string buf;
  size_t r = 0;
  int tries = 5;
  do {
    buf.resize(r + 4096);
    ssize_t p = recv(fd, buf.data() + r, 4096, MSG_NOSIGNAL|MSG_DONTWAIT);
    if (p <= 0) {
      if (errno != EAGAIN)
	std::clog << "Failed to receive data: " << strerror(errno) << '\n';
      if (r) break;
      else return "";
    }
    r += static_cast<size_t>(p);
  } while (buf[r-1] != '\n' && tries--);
  // Discard remaining buffer if all tries failed
  if (buf[r-1] != '\n') {
    while (--r && buf[r-1] != '\n');
  }
  buf.resize(r);
  return buf;
}

std::ostream& operator<<(std::ostream& o, const IRC& i)
{
  return o << "Service: " << i.state_to_string(i.service_type) << " (SSL: " << "false" << ")\n";
}

} // namespace kbot
