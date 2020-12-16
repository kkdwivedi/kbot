#pragma once

#include <absl/container/flat_hash_map.h>
#include <glog/logging.h>
#include <semaphore.h>
#include <signal.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>

#include <Epoll.hh>
#include <Server.hh>
#include <atomic>
#include <cassert>
#include <condition_variable>
#include <deque>
#include <mutex>
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
    sigfd = std::exchange(m.sigfd, -1);
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

struct ServerThreadSet {
  absl::flat_hash_map<std::jthread::id, std::jthread> thread_set;
  std::mutex thread_set_mtx;
  std::condition_variable thread_set_cv;

  ServerThreadSet() = default;
  ServerThreadSet(ServerThreadSet &) = delete;
  ServerThreadSet &operator=(ServerThreadSet &) = delete;
  ServerThreadSet(ServerThreadSet &&) = delete;
  ServerThreadSet &operator=(ServerThreadSet &&) = delete;
  ~ServerThreadSet() = default;

  void WaitAll();
  bool InsertNewThread(std::jthread &&jthr);
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

// RAII wrapper that handles cleanup of the thread's entry from the global set

struct ThreadCleanupSelf {
  ~ThreadCleanupSelf() {
    auto &s = kbot::server_thread_set;
    std::unique_lock lock(s.thread_set_mtx);
    auto it = s.thread_set.find(std::this_thread::get_id());
    // TODO: investigate crash
    assert(it != s.thread_set.end());
    // Detach ourselves, as we're going to die soon anyway
    it->second.detach();
    s.thread_set.erase(it);
    if (s.thread_set.size() == 0) {
      s.thread_set_cv.notify_all();
    }
  }
};

void WorkerRun(Manager m);

}  // namespace kbot
