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

ssize_t IRC::PONG(std::string& pingmsg)
{
  auto r = send_msg(pingmsg.replace(1, 2, "O"));
  if (r < 0)
    std::clog << "Failed to send PONG message: " << strerror(errno) << '\n';
  return r;
}

ssize_t IRC::LOGIN(const std::string& nickname, const std::string& password)
{
  ssize_t fail = 0;
  auto r = send_msg("\rUSER " + nickname + " 0 * :" + nickname + "\r\n");
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
    r = PRIVMSG("NickServ", "identify " + password);
    if (r < 0)
      std::clog << "Failed to send IDENTIFY LOGIN message: " << strerror(errno) << '\n';
    fail = r;
  }
  return fail;
}

ssize_t IRC::NICK(const std::string& nickname)
{
  auto r = send_msg("\rNICK " + nickname + "\r\n");
  if (r < 0)
    std::clog << "Failed to send NICK message: " << strerror(errno) << '\n';
  return r;
}

ssize_t IRC::JOIN(const std::string& channel)
{
  auto r = send_msg("\rJOIN " + channel + "\r\n");
  if (r < 0)
    std::clog << "Failed to send JOIN message: " << strerror(errno) << '\n';
  return r;
}

ssize_t IRC::PART(const std::string& channel)
{
  auto r = send_msg("\rPART " + channel + "\r\n");
  if (r < 0)
    std::clog << "Failed to send PART message: " << strerror(errno) << '\n';
  return r;
}

ssize_t IRC::PRIVMSG(const std::string& recipient, const std::string& msg)
{
  auto r = send_msg("\rPRIVMSG " + recipient + " :" + msg + "\r\n");
  if (r < 0)
    std::clog << "Failed to send PRIVMSG message: " << strerror(errno) << '\n';
  return r;
}

ssize_t IRC::QUIT(const std::string& msg)
{
  auto r = send_msg("\rQUIT :" + msg + "\r\n");
  if (r < 0)
    std::clog << "Failed to send QUIT message: " << strerror(errno) << '\n';
  return r;
}

ssize_t IRC::send_msg(std::string_view msg)
{
  auto r = send(fd, msg.data(), msg.size(), MSG_NOSIGNAL);
  if (r < 0) {
    std::clog << "Failed to send data: " << strerror(errno) << '\n';
  }
  return r;
}

std::string IRC::recv_msg()
{
  std::string buf;
  size_t r = 0;
  int tries = 5;
  do {
    buf.resize(r + 4096);
    auto p = recv(fd, buf.data() + r, 4096, MSG_NOSIGNAL|MSG_DONTWAIT);
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

std::ostream& operator<<(std::ostream& o, IRC& i)
{
  return o << "Service: " << i.state_to_string(i.service_type) << " (SSL: " << "false" << ")\n";
}

} // namespace kbot
