#pragma once

#include <glog/logging.h>
#include <semaphore.h>
#include <signal.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>

#include <atomic>
#include <cassert>
#include <deque>
#include <epoll.hh>
#include <mutex>
#include <server.hh>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace kbot {

// NOTE: Make sure createNew is called in context of thread owning
// the instance. This essentially means that copying or moving this
// instance across threads has broken semantics. Threads owning these
// instances handle all signals through the respective signalfd anyway.
class Manager {
  io::EpollManager epm;
  int sigfd = -1;
  sigset_t cur_set;
  std::unordered_map<int, std::function<void(struct signalfd_siginfo&)>>
      signalfd_sig_map;
  std::unordered_map<int, std::function<void(int)>> timerfd_set;

  static inline std::atomic<int> sigId = SIGRTMIN;

  explicit Manager(io::EpollManager&& epm, Server&& server, int curSigId)
      : epm(std::move(epm)), server(std::move(server)) {
    sigemptyset(&cur_set);
    sigaddset(&cur_set, curSigId);
  }
  bool InitializeSignalFd();

 public:
  Server server;

  Manager(const Manager&) = delete;
  Manager& operator=(const Manager&) = delete;
  Manager(Manager&& m) : epm(std::move(m.epm)), server(std::move(m.server)) {
    sigfd = m.sigfd;
    m.sigfd = -1;
  }
  Manager& operator=(Manager&&) = delete;
  ~Manager() {
    if (sigfd >= 0) {
      epm.DeleteFd(sigfd);
      close(sigfd);
    }
    sigId.fetch_sub(1, std::memory_order_relaxed);
  }

  static Manager CreateNew(Server&& server);
  // Signal Events
  bool RegisterSignalEvent(
      int signal, std::function<void(struct signalfd_siginfo&)> handler);
  bool DeleteSignalEvent(int signal);
  // Timer Events
  int RegisterTimerEvent(int clockid, std::function<void(int)> handler);
  bool RearmTimerEvent(int timerfd);
  bool DisarmTimerEvent(int timerfd);
  bool DeleteTimerEvent(int timerfd);
  // Generic
  bool RegisterChildEvent(int pidfd);

  void RunEventLoop();
};

// clang-format off
template <class T, class... Args>
concept ServerThreadCallable = requires (T t, Args...) {
  t(std::declval<Args>()...);
};
// clang-format on

template <class Callable, class... Args>
requires ServerThreadCallable<Callable, Args...> std::jthread
LaunchServerThread(Callable&& thread_main, Args&&... args) {
  // TODO: handling of exceptions thrown by thread_main
  // TODO: cancellation support
  std::jthread worker(std::forward<Callable>(thread_main),
                      std::forward<Args>(args)...);
  return worker;
}

void WorkerRun(Manager m);

using callback_t = void (*)(Server& s, const IRCMessage& m);

extern std::recursive_mutex privmsg_callback_map_mtx;

bool add_privmsg_callback(std::string_view command, callback_t cb_ptr);
auto get_privmsg_callback(std::string_view command)
    -> void (*)(Server&, const IRCMessagePrivMsg&);
callback_t get_callback(std::string_view command);
bool del_privmsg_callback(std::string_view command);

}  // namespace kbot
