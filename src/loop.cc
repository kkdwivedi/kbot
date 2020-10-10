#include <iostream>
#include <cassert>
#include <cstring>
#include <mutex>
#include <string_view>
#include <vector>
#include <thread>
#include <unordered_map>

#include <poll.h>
#include <glog/logging.h>

#include <irc.hh>
#include <loop.hh>
#include <server.hh>

namespace kbot {

namespace {

void cb_pong(const Server& s, const IRCMessage& m)
{
  LOG(INFO) << "Received PING, replying with PONG to" << m.get_parameters()[0];
  s.send_msg("PONG: " + std::string(m.get_parameters()[0].substr(1, std::string::npos)));
}

void cb_nickname(const Server& s, const IRCMessage& m)
{
  std::string_view new_nick = m.get_parameters()[0].substr(1);
  auto u = static_cast<const IRCMessagePrivmsg&>(m).get_user();
  LOG(INFO) << "Nickname change received: " << u.nickname << " -> " << new_nick;
  if (std::lock_guard<std::mutex> lock(s.nick_mtx); u.nickname == s.nickname) {
    s.nickname = new_nick;
  }
}

void cb_privmsg_hello(const Server& s, const IRCMessagePrivmsg& m)
{
  if (m.get_parameters()[0] == "##kbot")
    s.PRIVMSG("##kbot", std::string(m.get_user().nickname) += ": Hey buddy!");
}

void cb_privmsg(const Server& s, const IRCMessage& m)
{
  std::lock_guard<std::recursive_mutex> lock(privmsg_callback_map_mtx);
  auto cb = get_privmsg_callback(m.get_parameters()[1]);
  if (cb != nullptr)
    cb(s, static_cast<const IRCMessagePrivmsg&>(m));
}

// Map for bot commands
std::unordered_map<std::string_view, void(*)(const Server&, const IRCMessagePrivmsg&)> privmsg_callback_map = {
  { ":,quit", nullptr },
  { ":,hi", &cb_privmsg_hello },
};

} // namespace

// Mutex held during insertion/deletion
std::recursive_mutex privmsg_callback_map_mtx;

auto get_privmsg_callback(std::string_view command) -> void(*)(const Server&, const IRCMessagePrivmsg&)
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
  switch (get_lookup_mask(command)) {
  case get_lookup_mask("PING"):
    return &cb_pong;
  case get_lookup_mask("NICK"):
    return &cb_nickname;
  case get_lookup_mask("PRIVMSG"):
    return &cb_privmsg;
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
  auto m = std::make_unique<IRCMessage>(line);
  if (message::is_privmsg_message(*m)) {
    m->message_type = IRCMessageType::PRIVMSG;
    m = std::make_unique<IRCMessagePrivmsg>(std::move(*m));
  }
  LOG(INFO) << ">>> " << *m;
  // Handle termination as early as possible
  if (message::is_quit_message(*m))
    return false;
  std::lock_guard<std::recursive_mutex> lock(privmsg_callback_map_mtx);
  auto cb = get_callback(m->get_command());
  if (cb != nullptr) {
    LOG(INFO) << "Command found ... Dispatching callback.";
    cb(*ptr, *m);
  }
  return true;
} catch (std::runtime_error& e) {
  LOG(INFO) << "Caught IRCMessage exception: (" << e.what() << ")";
  return false;
}

void worker_run(std::shared_ptr<Server> ptr)
{
  LOG(INFO) << "Main loop for Server: ";
  ptr->dump_info();
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
      DLOG(INFO) << "Received notification to quit";
      break;
    }
    jthr_vec.emplace_back(worker_run, sptr);
  }
}

} // namespace kbot
