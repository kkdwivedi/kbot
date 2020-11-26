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
#include <vector>

namespace kbot {

// Manager

Manager Manager::CreateNew(std::string_view server_name = "[unnamed]") {
  auto epm = io::EpollManager::CreateNew();
  if (epm.has_value()) {
    auto curSigId = sigId.fetch_add(1, std::memory_order_relaxed);
    if (curSigId > SIGRTMAX) {
      sigId.fetch_sub(1, std::memory_order_relaxed);
      throw std::runtime_error(
          "No more Manager instances can be created, signal ID exhausted.");
    }

    sigset_t set;
    sigfillset(&set);
    sigdelset(&set, curSigId);
    if (pthread_sigmask(SIG_BLOCK, &set, nullptr)) {
      throw std::runtime_error("Failed to setup signal mask.");
    }

    std::string name = std::to_string(curSigId - SIGRTMIN);
    name.append("-").append(server_name);
    if (name.size() > 15) {
      // Requires length of thread name is capped at 16 (including '\0')
      name.erase(15);
    }
    if (pthread_setname_np(pthread_self(), name.c_str())) {
      throw std::runtime_error("Failed to set thread name.");
    }

    return Manager{std::move(*epm), curSigId};
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

bool Manager::RegisterSignalEvent(
    int signal, std::function<void(struct signalfd_siginfo&)> handler) {
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
void cb_pong(const Server& s, const IRCMessage& m) {
  LOG(INFO) << "Received PING, replying with PONG to" << m.GetParameters()[0];
  s.SendMsg("PONG: " +
            std::string(m.GetParameters()[0].substr(1, std::string::npos)));
}

void cb_nickname(const Server& s, const IRCMessage& m) {
  std::string_view new_nick = m.GetParameters()[0].substr(1);
  auto u = static_cast<const IRCMessagePrivmsg&>(m).GetUser();
  LOG(INFO) << "Nickname change received: " << u.nickname << " -> " << new_nick;
  if (std::lock_guard<std::mutex> lock(s.nick_mtx); u.nickname == s.nickname) {
    s.nickname = new_nick;
  }
}

void cb_privmsg_hello(const Server& s, const IRCMessagePrivmsg& m) {
  if (m.GetParameters()[0] == "##kbot")
    s.PrivMsg("##kbot", std::string(m.GetUser().nickname) += ": Hey buddy!");
}

void cb_privmsg(const Server& s, const IRCMessage& m) {
  std::lock_guard<std::recursive_mutex> lock(privmsg_callback_map_mtx);
  auto cb = get_privmsg_callback(m.GetParameters()[1]);
  if (cb != nullptr) cb(s, static_cast<const IRCMessagePrivmsg&>(m));
}

// Map for bot commands
std::unordered_map<std::string_view,
                   void (*)(const Server&, const IRCMessagePrivmsg&)>
    privmsg_callback_map = {
        {":,quit", nullptr},
        {":,hi", &cb_privmsg_hello},
};

}  // namespace

// Mutex held during insertion/deletion
std::recursive_mutex privmsg_callback_map_mtx;

auto get_privmsg_callback(std::string_view command)
    -> void (*)(const Server&, const IRCMessagePrivmsg&) {
  std::lock_guard<std::recursive_mutex> lock(privmsg_callback_map_mtx);
  auto it = privmsg_callback_map.find(command);
  return it == privmsg_callback_map.end() ? nullptr : it->second;
}

static constexpr uint64_t get_lookup_mask(std::string_view command) {
  const char* p = command.data();
  uint64_t mask = 0;
  if (command.size() <= 8) {
    for (size_t i = 0; i < command.size(); i++) {
      mask |= static_cast<uint64_t>(static_cast<unsigned char>(*p++) & 0xff)
              << (i * 8);
    }
  }
  return mask;
}

callback_t get_callback(std::string_view command) {
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

bool del_privmsg_callback(std::string_view command) {
  std::lock_guard<std::recursive_mutex> lock(privmsg_callback_map_mtx);
  return !!privmsg_callback_map.erase(command);
}

std::vector<std::string_view> TokenizeMessageMultiple(std::string& msg) {
  std::vector<std::string_view> ret;
  char* p = msg.data();
  const char* delim = "\r\n";
  char* saveptr;
  while ((p = strtok_r(p, delim, &saveptr))) {
    ret.emplace_back(p);
    p = nullptr;
  }
  return ret;
}

bool ProcessMessageLine(Server* ptr, std::string_view line) try {
  auto m = std::make_unique<IRCMessage>(line);
  if (Message::IsPrivMsgMessage(*m)) {
    m->message_type = IRCMessageType::PRIVMSG;
    m = std::make_unique<IRCMessagePrivmsg>(std::move(*m));
  }
  LOG(INFO) << ">>> " << *m;
  // Handle termination as early as possible
  if (Message::IsQuitMessage(*m)) return false;
  std::lock_guard<std::recursive_mutex> lock(privmsg_callback_map_mtx);
  auto cb = get_callback(m->GetCommand());
  if (cb != nullptr) {
    LOG(INFO) << "Command found ... Dispatching callback";
    cb(*ptr, *m);
  }
  return true;
} catch (std::runtime_error& e) {
  LOG(INFO) << "Caught IRCMessage exception: (" << e.what() << ")";
  return false;
}

void worker_run(Manager m, std::shared_ptr<Server> ptr) {
  LOG(INFO) << "Main loop for Server: ";
  auto mgr = io::EpollManager::CreateNew();
  if (mgr) {
    bool r = true;
    mgr->RegisterFd(
        ptr->fd, io::EpollManager::EpollIn,
        [&ptr, &r](struct epoll_event) {
          std::string msg(ptr->RecvMsg());
          if (msg == "") return;
          std::vector<std::string_view> tok = TokenizeMessageMultiple(msg);
          for (auto& line : tok) {
            r = ProcessMessageLine(ptr.get(), line);
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
    mgr->DeleteFd(ptr->fd);
  } else {
    LOG(ERROR) << "Failed to setup EpollManager instance";
    return;
  }
}

}  // namespace kbot
