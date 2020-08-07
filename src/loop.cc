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

void cb_nickname(const Server& s, const IRCMessage& m)
{
  auto u = m.get_user();
  std::clog << "Noticed nickname change\n";
  if (u.nickname == s.get_nickname()) {
    s.update_nickname(m.get_parameters()[0].substr(1));
  }
}

// Map of callbacks for each command
std::unordered_map<std::string_view, callback_t> callback_map = {
  { "PING", cb_pong },
  { "PRIVMSG", cb_privmsg },
  { "NICK", cb_nickname },
};

} // namespace

// Mutex held during insertion/deletion
std::recursive_mutex callback_map_mtx;

bool add_callback(std::string_view command, callback_t cb_ptr)
{
  assert(cb_ptr != nullptr);
  std::lock_guard<std::recursive_mutex> lock(callback_map_mtx);
  auto [_, b] = callback_map.insert({command, cb_ptr});
  return b;
}

callback_t get_callback(std::string_view command)
{
  std::lock_guard<std::recursive_mutex> lock(callback_map_mtx);
  auto it = callback_map.find(command);
  return it == callback_map.end() ? nullptr : it->second;
}

bool del_callback(std::string_view command)
{
  std::lock_guard<std::recursive_mutex> lock(callback_map_mtx);
  return !!callback_map.erase(command);
}

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
  std::lock_guard<std::recursive_mutex> lock(callback_map_mtx);
  const IRCMessage m(line);
  std::clog << " === " << m;
  // Handle termination as early as possible
  if (message::is_message_quit(m))
    return false;
  auto cb = get_callback(m.get_command());
  if (cb != nullptr) {
    std::clog << "Command found ... Dispatching callback.\n";
    cb(*ptr, m);
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
