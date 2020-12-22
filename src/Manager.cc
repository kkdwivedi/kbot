#include <fmt/format.h>
#include <glog/logging.h>
#include <poll.h>
#include <sys/signalfd.h>

#include <IRC.hh>
#include <Manager.hh>
#include <Server.hh>
#include <UserCommand.hh>
#include <cassert>
#include <cstring>
#include <exception>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace kbot {

static std::atomic<int> sig_id = SIGRTMIN;

void Manager::SetupSignalDelivery(std::string_view server_name) {
  int cur_sig_id;
  // The CAS is uncontended almost all the time
  do {
    cur_sig_id = sig_id.load(std::memory_order_relaxed);
    if (cur_sig_id == SIGRTMAX) {
      LOG(WARNING) << "Signal IDs exhausted, skipping delivery setup";
      return;
    }
  } while (!sig_id.compare_exchange_strong(cur_sig_id, cur_sig_id + 1, std::memory_order_relaxed,
                                           std::memory_order_relaxed));
  sigset_t set;
  sigfillset(&set);
  sigdelset(&set, cur_sig_id);
  if (pthread_sigmask(SIG_BLOCK, &set, nullptr)) {
    PLOG(WARNING) << "Failed to setup signal mask";
  }

  std::string name = fmt::format("{}-{}", cur_sig_id - SIGRTMIN, server_name);
  if (name.size() > 15) {
    // Requires length of thread name to be capped at 16 (including '\0')
    name.erase(15);
  }
  if (pthread_setname_np(pthread_self(), name.c_str())) {
    PLOG(WARNING) << "Failed to set thread name";
    return;
  }
}

void Manager::TearDownSignalDelivery() { sig_id.fetch_sub(1, std::memory_order_relaxed); }

// Manager
Manager Manager::CreateNew(Server &&server) {
  int fd = epoll_create1(EPOLL_CLOEXEC);
  if (fd < 0) {
    throw std::runtime_error("Failed to create epoll fd");
  }
  return Manager(fd, std::move(server));
}

// ServerThreadSet
// Implements a collection that allows waiting for completion of all server threads from the main
// thread, and insertion of new threads to the existing set.

void ServerThreadSet::WaitAll() {
  std::unique_lock lock(thread_set_mtx);
  thread_set_cv.wait(lock, [this] { return thread_set.empty(); });
}

bool ServerThreadSet::InsertNewThread(std::jthread &&jthr) {
  std::unique_lock lock(thread_set_mtx);
  return thread_set.insert({jthr.get_id(), std::move(jthr)}).second;
}

namespace {

void BuiltinPong(Manager &m, const IRCMessage &msg) {
  LOG(INFO) << "Received PING, replying with PONG to " << msg.GetParameters()[0];
  m.server.SendMsg(fmt::format("PONG :{}", msg.GetParameters()[0].substr(1)));
}

void BuiltinNickname(Manager &m, const IRCMessageNick &msg) {
  std::string_view new_nick = msg.GetNewNickname();
  LOG(INFO) << "Nickname change received, applying " << new_nick;
  m.server.UpdateNickname(msg.GetUser().nickname, new_nick);
}

void BuiltinJoin(Manager &m, const IRCMessageJoin &msg) {
  DLOG(INFO) << "Join request completion received for " << msg.GetChannel();
  m.server.UpdateJoinChannel(msg.GetChannel());
}

void BuiltinPart(Manager &m, const IRCMessagePart &msg) {
  DLOG(INFO) << "Part request completion received for " << msg.GetChannel();
  m.server.UpdatePartChannel(msg.GetChannel());
}

void BuiltinPrivMsg(Manager &m, const IRCMessagePrivMsg &msg) {
  try {
    assert(msg.GetParameters().size() >= 2);
    auto cb_it = UserCommand::user_command_map.find(msg.GetUserCommand());
    if (cb_it != UserCommand::user_command_map.end()) {
      cb_it->second(m, msg);
    } else {
      // Take shared_lock here, as taking it early would mean deadlock when invoking plugin
      // loading/unloading commands, which modify the server's command map (otherwise DEADLOCK)
      std::shared_lock lock(m.server.user_command_mtx);
      auto cb_local_it = m.server.user_command_map.find(msg.GetUserCommand());
      if (cb_local_it != m.server.user_command_map.end()) {
        cb_local_it->second(m, msg);
      }
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
    [](Manager &m, const IRCMessageJoin &msg) { BuiltinJoin(m, msg); },
    [](Manager &m, const IRCMessagePart &msg) { BuiltinPart(m, msg); },
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
  LOG(INFO) << "Malformed IRCMessage exception: (" << e.what() << ")";
  return true;
}

void WorkerRun(Manager m) {
  m.server.SetState(ServerState::kConnected);
  Manager::SetupSignalDelivery(m.server.GetAddress());
  struct Cleanup {
    ~Cleanup() { Manager::TearDownSignalDelivery(); }
  } _;
  LOG(INFO) << "Main loop for Server: ";
  // TODO;
  io::EpollManager *mgr = static_cast<io::EpollManager *>(&m);
  if (mgr) {
    bool r = true;
    mgr->RegisterFd(
        m.server.fd, io::EpollManager::EpollIn,
        [&mm = m, &r](struct epoll_event) {
          std::string msg(mm.server.RecvMsg());
          if (msg == "") return;
          auto tok = TokenizeMessageMultiple(msg);
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
