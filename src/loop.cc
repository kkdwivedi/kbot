#include <iostream>
#include <cassert>
#include <cstring>
#include <string_view>
#include <vector>
#include <thread>
#include <unordered_map>

#include <poll.h>

#include <irc.hh>
#include <loop.hh>
#include <server.hh>

namespace kbot {

namespace {

void cb_pong(const Server& s, const IRCMessage& m)
{
  std::clog << "Received PING, replying with PONG to " << m.get_parameters()[0] << '\n';
  s.send_msg("PONG: " + std::string(m.get_parameters()[0].substr(1, std::string::npos)));
}

void cb_privmsg(const Server& s, const IRCMessage& m)
{
  if (m.get_parameters()[0] == "##kbot")
    if (m.get_parameters()[1].substr(0, 4) == ":,hi")
      s.PRIVMSG("##kbot", std::string(m.get_user().nickname) += ": Hey buddy!");
}

} // namespace

std::unordered_map<std::string_view, void(*)(const Server&, const IRCMessage&)> callback_map = {
  { "PING", cb_pong },
  { "PRIVMSG", cb_privmsg },
};

std::vector<std::string_view> tokenize_msg_multi(std::string& msg)
{
  std::vector<std::string_view> ret;
  char *p = msg.data();
  const char *delim = "\r\n";
  while ((p = std::strtok(p, delim))) {
    ret.emplace_back(p);
    p = nullptr;
  }
  return ret;
}

bool process_msg_line(Server *ptr, std::string_view line) try
{
  const IRCMessage m(line);
  std::clog << " === " << m;
  if (m.get_command() == "PRIVMSG") {
    if (m.get_parameters()[1].substr(0, 6) == ":,quit")
      return false;
  }
  auto it = callback_map.find(m.get_command());
  if (it != callback_map.end()) {
    std::clog << "Command found ... Dispatching callback.\n";
    assert(it->second != nullptr);
    it->second(*ptr, m);
  }
  return true;
} catch (std::runtime_error&) {
  std::clog << "Caught IRCMessage exception\n";
  return false;
}

void worker_run(std::shared_ptr<Server> ptr)
{
  std::clog << "Main loop for Server: " << *ptr;
  struct pollfd pfd = { .fd = ptr->fd, .events = POLLIN };
  for (bool r = true; r;) {
    poll(&pfd, 1, -1);
    std::string msg(ptr->recv_msg());
    if (msg == "") continue;
    std::vector<std::string_view> tok = tokenize_msg_multi(msg);
    for (auto& line : tok) {
      r = process_msg_line(ptr.get(), line);
    }
  }
  kbot::tr.push_back(nullptr);
}

void supervisor_run()
{
  std::vector<std::jthread> jthr_vec;
  for (;;) {
    sem_wait(&tr.req_sem);
    auto sptr = tr.pop_front();
    if (sptr == nullptr) {
      std::clog << "Received notification to quit\n";
      break;
    }
    jthr_vec.emplace_back(worker_run, sptr);
  }
}

} // namespace kbot
