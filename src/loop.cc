#include <iostream>
#include <cassert>
#include <cstring>
#include <mutex>
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

void cb_privmsg_hello(const Server& s, const IRCMessage& m)
{
  if (m.get_parameters()[0] == "##kbot")
    s.PRIVMSG("##kbot", std::string(m.get_user().nickname) += ": Hey buddy!");
}

void cb_privmsg(const Server& s, const IRCMessage& m)
{
  std::lock_guard<std::recursive_mutex> lock(privmsg_callback_map_mtx);
  auto cb = get_privmsg_callback(m.get_parameters()[1]);
  if (cb != nullptr)
    cb(s, m);
}

void cb_nickname(const Server& s, const IRCMessage& m)
{
  auto u = m.get_user();
  std::clog << "Noticed nickname change\n";
  if (u.nickname == s.get_nickname()) {
    s.update_nickname(m.get_parameters()[0].substr(1));
  }
}

// Map for bot commands
std::unordered_map<std::string_view, callback_t> privmsg_callback_map = {
  { ":,quit", nullptr },
  { ":,hi", cb_privmsg_hello },
};

} // namespace

// Mutex held during insertion/deletion
std::recursive_mutex privmsg_callback_map_mtx;

bool add_privmsg_callback(std::string_view command, callback_t cb_ptr)
{
  assert(cb_ptr != nullptr);
  std::lock_guard<std::recursive_mutex> lock(privmsg_callback_map_mtx);
  auto [_, b] = privmsg_callback_map.insert({command, cb_ptr});
  return b;
}

callback_t get_privmsg_callback(std::string_view command)
{
  std::lock_guard<std::recursive_mutex> lock(privmsg_callback_map_mtx);
  auto it = privmsg_callback_map.find(command);
  return it == privmsg_callback_map.end() ? nullptr : it->second;
}

static constexpr uint64_t get_lookup_mask(std::string_view command)
{
  const char *p = command.data();
  uint64_t mask = 0;
  if (command.size() <= 8) {
    for (size_t i = 0; i < command.size(); i++) {
      mask |= static_cast<uint64_t>(static_cast<unsigned char>(*p++) & 0xff) << (i * 8);
    }
  }
  return mask;
}

callback_t get_callback(std::string_view command)
{
  printf("Mask generated for %s -> %lx\n", std::string(command).data(), get_lookup_mask(command));
  switch (get_lookup_mask(command)) {
  case get_lookup_mask("PING"):
    return cb_pong;
  case get_lookup_mask("NICK"):
    return cb_nickname;
  case get_lookup_mask("PRIVMSG"):
    return cb_privmsg;
  case 0:
  default:
    return nullptr;
  }
}

bool del_privmsg_callback(std::string_view command)
{
  std::lock_guard<std::recursive_mutex> lock(privmsg_callback_map_mtx);
  return !!privmsg_callback_map.erase(command);
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
  const IRCMessage m(line);
  std::clog << " === " << m;
  // Handle termination as early as possible
  if (message::is_quit_message(m))
    return false;
  std::lock_guard<std::recursive_mutex> lock(privmsg_callback_map_mtx);
  auto cb = get_callback(m.get_command());
  if (cb != nullptr) {
    std::clog << "Command found ... Dispatching callback.\n";
    cb(*ptr, m);
  }
  return true;
} catch (std::runtime_error& e) {
  std::clog << "Caught IRCMessage exception: (" << e.what() << ")\n";
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
