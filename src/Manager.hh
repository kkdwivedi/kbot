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

using io::EpollManager;

// NOTE: Make sure createNew is called in context of thread owning
// the instance. This essentially means that copying or moving this
// instance across threads has broken semantics. Threads owning these
// instances handle all signals through the respective signalfd anyway.
class Manager : public EpollManager {
  explicit Manager(int fd, Server &&server) : EpollManager(fd), server(std::move(server)) {}

 public:
  Server server;

  Manager(const Manager &) = delete;
  Manager &operator=(const Manager &) = delete;
  Manager(Manager &&m) = default;
  Manager &operator=(Manager &&) = default;
  ~Manager() = default;

  static Manager CreateNew(Server &&server);
  static void SetupSignalDelivery(std::string_view server_name);
  static void TearDownSignalDelivery();
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
  ServerThreadSet(const ServerThreadSet &) = delete;
  ServerThreadSet &operator=(const ServerThreadSet &) = delete;
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
