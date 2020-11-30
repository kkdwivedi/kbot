#include <glog/logging.h>
#include <poll.h>
#include <sys/signalfd.h>

#include <cassert>
#include <cstring>
#include <exception>
#include <iostream>
#include <irc.hh>
#include <loop.hh>
#include <mutex>
#include <optional>
#include <server.hh>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace kbot {

// Manager

Manager Manager::CreateNew(Server &&server) {
  auto epm = io::EpollManager::CreateNew();
  if (epm.has_value()) {
    auto curSigId = sigId.fetch_add(1, std::memory_order_relaxed);
    if (curSigId > SIGRTMAX) {
      sigId.fetch_sub(1, std::memory_order_relaxed);
      throw std::runtime_error("No more Manager instances can be created, signal ID exhausted.");
    }

    sigset_t set;
    sigfillset(&set);
    sigdelset(&set, curSigId);
    // Eww, maybe there's a better way to do this crap?
    if (pthread_sigmask(SIG_BLOCK, &set, nullptr)) {
      throw std::runtime_error("Failed to setup signal mask.");
    }

    std::string name = std::to_string(curSigId - SIGRTMIN);
    name.append("-").append(server.GetAddress());
    if (name.size() > 15) {
      // Requires length of thread name is capped at 16 (including '\0')
      name.erase(15);
    }
    if (pthread_setname_np(pthread_self(), name.c_str())) {
      throw std::runtime_error("Failed to set thread name.");
    }

    return Manager{std::move(*epm), std::move(server), curSigId};
  } else {
    throw std::runtime_error("Failed to create EpollManager instance.");
  }
}

bool Manager::InitializeSignalFd() {
  assert(sigfd == -1);
  sigfd = signalfd(sigfd, &cur_set, SFD_NONBLOCK | SFD_CLOEXEC);
  if (sigfd < 0) {
    return false;
  } else {
    auto dispatcher = [this](struct epoll_event ev) {
      assert(ev.events & EPOLLIN);
      assert(ev.data.fd == sigfd);

      struct signalfd_siginfo si;
      for (;;) {
        // Consume everything until we get EAGAIN
        // TODO: can optimize this to buffer many in one go
        auto r = read(sigfd, &si, sizeof(si));
        if (r < 0) {
          if (errno == EAGAIN) {
            return;
          } else {
            PLOG(ERROR) << "Failed to consume signal from signalfd";
            return;
          }
        }
        auto it = signalfd_sig_map.find(static_cast<int>(si.ssi_signo));
        assert(it != signalfd_sig_map.end());
        it->second(si);
      }
    };
    epm.RegisterFd(sigfd, io::EpollManager::EpollIn, dispatcher,
                   io::EpollManager::EpollConfigDefault);
    return true;
  }
}

bool Manager::RegisterSignalEvent(int signal,
                                  std::function<void(struct signalfd_siginfo &)> handler) {
  if (sigfd == -1) {
    sigaddset(&cur_set, signal);
    if (InitializeSignalFd()) {
      return true;
    } else {
      sigdelset(&cur_set, signal);
      return false;
    }
  }

  if (sigismember(&cur_set, signal)) {
    return true;
  } else {
    sigaddset(&cur_set, signal);
    int r = signalfd(sigfd, &cur_set, 0);
    if (r < 0) {
      sigdelset(&cur_set, signal);
      return false;
    }
    signalfd_sig_map[signal] = std::move(handler);
    return true;
  }
}

bool Manager::DeleteSignalEvent(int signal) {
  if (sigismember(&cur_set, signal) == false) {
    return false;
  }
  auto it = signalfd_sig_map.find(signal);
  assert(it != signalfd_sig_map.end());
  signalfd_sig_map.erase(it);
  return true;
}

namespace {

void cb_pong(Server &s, const IRCMessage &m) {
  LOG(INFO) << "Received PING, replying with PONG to " << m.GetParameters()[0];
  s.SendMsg("PONG :" + std::string(m.GetParameters()[0].substr(1, std::string::npos)));
}

void cb_nickname(Server &s, const IRCMessageNick &m) {
  std::string_view new_nick = m.GetNewNickname();
  LOG(INFO) << "Nickname change received, applying " << new_nick;
  s.UpdateNickname(m.GetUser().nickname, new_nick);
}

bool ExpectUserCommandArgsRange(const IRCMessagePrivMsg &m, unsigned min, unsigned max = 1024) {
  // 2, one for the buffer name, and one for the command itself
  assert(min >= 1 && max <= 1024);
  if (m.GetParameters().size() >= min + 2 && m.GetParameters().size() <= max + 2) {
    return true;
  }
  return false;
}

// Can create as an entrypoint
void SendInvokerReply(Server &s, const IRCMessagePrivMsg &m, std::string_view str) {
  s.SendChannel(m.GetChannel(), std::string(m.GetUser().nickname).append(": ").append(str));
}

void cb_privmsg_hello(Server &s, const IRCMessagePrivMsg &m) { SendInvokerReply(s, m, "Hello!"); }

void cb_privmsg_nick(Server &s, const IRCMessagePrivMsg &m) {
  if (ExpectUserCommandArgsRange(m, 1, 1)) {
    if (Message::IsUserCapable(m.GetUser(), IRCUserCapability::kNickModify)) {
      s.SetNickname(m.GetUserCommandParameters().at(0));
    } else {
      SendInvokerReply(s, m, "You don't have permission to use ,join.");
    }
  } else {
    SendInvokerReply(s, m, "Incorrect number of arguments to ,nick, see ,help.");
  }
}

void cb_privmsg_join(Server &s, const IRCMessagePrivMsg &m) {
  if (ExpectUserCommandArgsRange(m, 1, 1)) {
    if (Message::IsUserCapable(m.GetUser(), IRCUserCapability::kJoin)) {
      s.JoinChannel(m.GetUserCommandParameters().at(0));
    } else {
      SendInvokerReply(s, m, "You don't have permission to use ,join.");
    }
  } else {
    SendInvokerReply(s, m, "Incorrect number of arguments to ,join, see ,help.");
  }
}

void cb_privmsg_part(Server &s, const IRCMessagePrivMsg &m) {
  if (ExpectUserCommandArgsRange(m, 1, 1)) {
    if (Message::IsUserCapable(m.GetUser(), IRCUserCapability::kPart)) {
      s.PartChannel(m.GetUserCommandParameters().at(0));
    } else {
      SendInvokerReply(s, m, "You don't have permission to use ,part.");
    }
  } else {
    SendInvokerReply(s, m, "Incorrect number of arguments to ,part, see ,help.");
  }
}

void cb_privmsg_load(Server &s, const IRCMessagePrivMsg &m) {
  SendInvokerReply(s, m, "Plugin system currently unimplemented.");
}

void cb_privmsg_unload(Server &s, const IRCMessagePrivMsg &m) { cb_privmsg_load(s, m); }

void cb_privmsg_help(Server &s, const IRCMessagePrivMsg &m) {
  SendInvokerReply(s, m, "Commands available: hi, join, part, load, unload, help");
}

void cb_privmsg(Server &s, const IRCMessagePrivMsg &m) {
  std::unique_lock lock(privmsg_callback_map_mtx);
  try {
    assert(m.GetParameters().size() >= 2);
    auto cb = GetPrivMsgCallback(m.GetUserCommand());
    if (cb != nullptr) cb(s, m);
  } catch (std::out_of_range &) {
    LOG(ERROR) << "Not enough arguments for user commands, please implement checks";
    return;
  }
}

// Map for bot commands
std::unordered_map<std::string_view, privmsg_callback_t> privmsg_callback_map = {
    {":,quit", nullptr},
    {":,nick", &cb_privmsg_nick},
    {":,hi", &cb_privmsg_hello},
    {":,join", &cb_privmsg_join},
    {":,part", &cb_privmsg_part},
    {":,load", &cb_privmsg_load},
    {":,unload", &cb_privmsg_unload},
    {":,help", &cb_privmsg_help},
};

}  // namespace

// Mutex held during insertion/deletion
std::recursive_mutex privmsg_callback_map_mtx;

bool AddPrivMsgCallback(std::string_view command, privmsg_callback_t cb_ptr) {
  assert(cb_ptr);
  std::unique_lock lock(privmsg_callback_map_mtx);
  auto it = privmsg_callback_map.find(command);
  if (it != privmsg_callback_map.end()) {
    return true;
  } else {
    return privmsg_callback_map.insert({command, cb_ptr}).second;
  }
}

auto GetPrivMsgCallback(std::string_view command) -> privmsg_callback_t {
  std::unique_lock lock(privmsg_callback_map_mtx);
  auto it = privmsg_callback_map.find(command);
  return it == privmsg_callback_map.end() ? nullptr : it->second;
}

bool DelPrivMsgCallback(std::string_view command) {
  std::unique_lock lock(privmsg_callback_map_mtx);
  return !!privmsg_callback_map.erase(command);
}

template <class... T>
struct OverloadSet : T... {
  using T::operator()...;
};

template <class... T>
OverloadSet(T...) -> OverloadSet<T...>;

constexpr auto IRCMessageVisitor = OverloadSet{
    [](Server &s, const std::monostate &) { LOG(ERROR) << "Visitor for monostate called"; },
    [](Server &s, const IRCMessage &m) {},
    [](Server &s, const IRCMessagePing &m) { cb_pong(s, m); },
    [](Server &s, const IRCMessageNick &m) { cb_nickname(s, m); },
    [](Server &s, const IRCMessagePrivMsg &m) { cb_privmsg(s, m); },
    [](Server &s, const IRCMessageQuit &m) {}};

using VisitorBase = decltype(IRCMessageVisitor);

struct Visitor : VisitorBase {
  Server &s;
  explicit Visitor(Server &s) : s(s) {}
  using VisitorBase::operator();
  void operator()(const auto &m) { VisitorBase::operator()(s, m); }
};

std::vector<std::string_view> TokenizeMessageMultiple(std::string &msg) {
  std::vector<std::string_view> ret;
  char *p = msg.data();
  const char *delim = "\r\n";
  char *saveptr;
  while ((p = strtok_r(p, delim, &saveptr))) {
    ret.emplace_back(p);
    p = nullptr;
  }
  return ret;
}

bool ProcessMessageLine(Server &s, std::string_view line) try {
  IRCMessage m(line);
  DLOG(INFO) << m;
  auto mv = GetIRCMessageVariantFrom(std::move(m));
  // Handle termination early
  if (std::holds_alternative<IRCMessageQuit>(mv)) return false;
  std::unique_lock lock(privmsg_callback_map_mtx);
  std::visit(Visitor{s}, mv);
  return true;
} catch (std::runtime_error &e) {
  LOG(INFO) << "Caught IRCMessage exception: (" << e.what() << ")";
  return false;
}

void WorkerRun(Manager m) {
  LOG(INFO) << "Main loop for Server: ";
  auto mgr = io::EpollManager::CreateNew();
  if (mgr) {
    bool r = true;
    mgr->RegisterFd(
        m.server.fd, io::EpollManager::EpollIn,
        [&mm = m, &r](struct epoll_event) {
          std::string msg(mm.server.RecvMsg());
          if (msg == "") return;
          std::vector<std::string_view> tok = TokenizeMessageMultiple(msg);
          for (auto &line : tok) {
            r = ProcessMessageLine(mm.server, line);
          }
        },
        io::EpollManager::EpollConfigDefault);
    while (r) {
      int k = mgr->RunEventLoop(-1);
      if (k < 0) {
        PLOG(ERROR) << "Exiting event loop";
        break;
      }
    }
    mgr->DeleteFd(m.server.fd);
  } else {
    LOG(ERROR) << "Failed to setup EpollManager instance";
    return;
  }
}

}  // namespace kbot
