#pragma once

#include <absl/container/flat_hash_map.h>
#include <glog/logging.h>
#include <semaphore.h>
#include <signal.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>

#include <atomic>
#include <cassert>
#include <condition_variable>
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
  absl::flat_hash_map<int, std::function<void(struct signalfd_siginfo &)>> signalfd_sig_map;
  absl::flat_hash_map<int, std::function<void(int)>> timerfd_map;

  static inline std::atomic<int> sigId = SIGRTMIN;

  explicit Manager(io::EpollManager &&epm, Server &&server, int curSigId)
      : epm(std::move(epm)), server(std::move(server)) {
    sigemptyset(&cur_set);
    sigaddset(&cur_set, curSigId);
  }
  bool InitializeSignalFd();

 public:
  Server server;

  Manager(const Manager &) = delete;
  Manager &operator=(const Manager &) = delete;
  Manager(Manager &&m) : epm(std::move(m.epm)), server(std::move(m.server)) {
    sigfd = m.sigfd;
    m.sigfd = -1;
  }
  Manager &operator=(Manager &&) = delete;
  ~Manager() {
    if (sigfd >= 0) {
      epm.DeleteFd(sigfd);
      close(sigfd);
    }
    sigId.fetch_sub(1, std::memory_order_relaxed);
  }

  static Manager CreateNew(Server &&server);
  // Signal Events
  bool RegisterSignalEvent(int signal, std::function<void(struct signalfd_siginfo &)> handler);
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

class ServerThreadSet {
  absl::flat_hash_map<std::jthread::id, std::jthread> thread_set;
  std::mutex thread_set_mtx;
  std::condition_variable thread_set_cv;

 public:
  ServerThreadSet() = default;
  ServerThreadSet(ServerThreadSet &) = delete;
  ServerThreadSet &operator=(ServerThreadSet &) = delete;
  ServerThreadSet(ServerThreadSet &&) = delete;
  ServerThreadSet &operator=(ServerThreadSet &&) = delete;
  ~ServerThreadSet() = default;

  void WaitAll();
  bool InsertNewThread(std::jthread &&jthr);
  friend void WorkerRun(Manager m);
};

inline ServerThreadSet server_thread_set;

template <class Callable, class... Args>
requires ServerThreadCallable<Callable, Args...> void LaunchServerThread(Callable &&thread_main,
                                                                         Args &&...args) {
  // TODO: handling of exceptions thrown by thread_main
  // TODO: cancellation support
  server_thread_set.InsertNewThread(
      std::jthread(std::forward<Callable>(thread_main), std::forward<Args>(args)...));
}

void WorkerRun(Manager m);

}  // namespace kbot
