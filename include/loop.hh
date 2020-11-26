#pragma once

#include <glog/logging.h>
#include <semaphore.h>
#include <signal.h>
#include <sys/signalfd.h>

#include <atomic>
#include <cassert>
#include <deque>
#include <epoll.hh>
#include <mutex>
#include <server.hh>
#include <thread>
#include <type_traits>
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
  static inline std::atomic<int> sigId = SIGRTMIN;

  explicit Manager(io::EpollManager&& epm, int curSigId) : epm(std::move(epm)) {
    sigemptyset(&cur_set);
    sigaddset(&cur_set, curSigId);
  }
  bool InitializeSignalFd();

 public:
  Manager(const Manager&) = delete;
  Manager& operator=(const Manager&) = delete;
  Manager(Manager&& m) : epm(std::move(m.epm)) {
    sigfd = m.sigfd;
    m.sigfd = -1;
  }
  Manager& operator=(Manager&&) = delete;
  ~Manager() {
    epm.DeleteFd(sigfd);
    close(sigfd);
    sigId.fetch_sub(1, std::memory_order_relaxed);
  }

  static Manager CreateNew(std::string_view server_name);
  bool RegisterSignalEvent(
      int signal, std::function<void(struct signalfd_siginfo&)> handler);
  bool DeleteSignalEvent(int signal);
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

void worker_run(Manager m, std::shared_ptr<Server> ptr);

using callback_t = void (*)(const Server& s, const IRCMessage& m);

extern std::recursive_mutex privmsg_callback_map_mtx;

bool add_privmsg_callback(std::string_view command, callback_t cb_ptr);
auto get_privmsg_callback(std::string_view command)
    -> void (*)(const Server&, const IRCMessagePrivmsg&);
callback_t get_callback(std::string_view command);
bool del_privmsg_callback(std::string_view command);

}  // namespace kbot
