#include <glog/logging.h>
#include <poll.h>
#include <sys/signalfd.h>

#include <UserCommand.hh>
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

void BuiltinPong(Manager &m, const IRCMessage &msg) {
  LOG(INFO) << "Received PING, replying with PONG to " << msg.GetParameters()[0];
  m.server.SendMsg("PONG :" + std::string(msg.GetParameters()[0].substr(1, std::string::npos)));
}

void BuiltinNickname(Manager &m, const IRCMessageNick &msg) {
  std::string_view new_nick = msg.GetNewNickname();
  LOG(INFO) << "Nickname change received, applying " << new_nick;
  m.server.UpdateNickname(msg.GetUser().nickname, new_nick);
}

void BuiltinPrivMsg(Manager &m, const IRCMessagePrivMsg &msg) {
  std::shared_lock lock(UserCommand::user_command_mtx);
  try {
    assert(msg.GetParameters().size() >= 2);
    auto cb_it = UserCommand::user_command_map.find(msg.GetUserCommand());
    if (cb_it != UserCommand::user_command_map.end()) {
      cb_it->second(m, msg);
    }
  } catch (std::out_of_range &) {
    LOG(ERROR) << "Not enough arguments for user commands, please implement checks";
    return;
  }
}

}  // namespace

template <class... T>
struct OverloadSet : T... {
  using T::operator()...;
};

template <class... T>
OverloadSet(T...) -> OverloadSet<T...>;

constexpr auto IRCMessageVisitor = OverloadSet{
    [](Manager &, const std::monostate &) { LOG(ERROR) << "Visitor for monostate called"; },
    [](Manager &, const IRCMessage &) {},
    [](Manager &m, const IRCMessagePing &msg) { BuiltinPong(m, msg); },
    [](Manager &m, const IRCMessageNick &msg) { BuiltinNickname(m, msg); },
    [](Manager &m, const IRCMessagePrivMsg &msg) { BuiltinPrivMsg(m, msg); },
    [](Manager &, const IRCMessageQuit &) {}};

using VisitorBase = decltype(IRCMessageVisitor);

struct Visitor : VisitorBase {
  Manager &m;
  explicit Visitor(Manager &m) : m(m) {}
  using VisitorBase::operator();
  void operator()(const auto &msg) { VisitorBase::operator()(m, msg); }
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

bool ProcessMessageLine(Manager &m, std::string_view line) try {
  IRCMessage msg(line);
  DLOG(INFO) << msg;
  auto mv = GetIRCMessageVariantFrom(std::move(msg));
  // Handle termination early
  if (std::holds_alternative<IRCMessageQuit>(mv)) return false;
  std::visit(Visitor{m}, mv);
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
            r = ProcessMessageLine(mm, line);
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
