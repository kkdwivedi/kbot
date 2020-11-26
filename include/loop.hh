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
  std::map<int, std::function<void(struct signalfd_siginfo&)>> signalfd_sig_map;
  static inline std::atomic<int> sigId = SIGRTMIN;

  explicit Manager(io::EpollManager&& epm, int curSigId) : epm(std::move(epm)) {
    sigemptyset(&cur_set);
    sigaddset(&cur_set, curSigId);
  }
  bool initializeSignalFd();

 public:
  Manager(const Manager&) = delete;
  Manager& operator=(const Manager&) = delete;
  Manager(Manager&& m) : epm(std::move(m.epm)) {
    sigfd = m.sigfd;
    m.sigfd = -1;
  }
  Manager& operator=(Manager&&) = delete;
  ~Manager() {
    epm.deleteFd(sigfd);
    close(sigfd);
    sigId.fetch_sub(1, std::memory_order_relaxed);
  }

  static Manager createNew(std::string_view server_name);
  bool registerSignalEvent(
      int signal, std::function<void(struct signalfd_siginfo&)> handler);
  bool deleteSignalEvent(int signal);
};

class ThreadRequest {
  std::mutex vec_mtx;
  std::deque<std::shared_ptr<Server>> req_deq;

 public:
  sem_t req_sem;
  ThreadRequest() { sem_init(&req_sem, 0, 0); }
  ThreadRequest(const ThreadRequest&) = delete;
  ThreadRequest& operator=(const ThreadRequest&) = delete;
  ThreadRequest(ThreadRequest&&) = delete;
  ThreadRequest& operator=(ThreadRequest&&) = delete;
  ~ThreadRequest() { sem_destroy(&req_sem); }
  // Basic API
  void push_back(std::shared_ptr<Server> s) {
    std::lock_guard<std::mutex> lock(vec_mtx);
    req_deq.push_back(std::move(s));
    sem_post(&req_sem);
  }
  std::shared_ptr<Server> pop_front() {
    std::lock_guard<std::mutex> lock(vec_mtx);
    assert(req_deq.size());
    auto sptr = std::move(req_deq[0]);
    req_deq.pop_front();
    return sptr;
  }
};

inline ThreadRequest tr;

void worker_run(std::shared_ptr<Server> ptr);
void supervisor_run();

using callback_t = void (*)(const Server& s, const IRCMessage& m);

extern std::recursive_mutex privmsg_callback_map_mtx;

bool add_privmsg_callback(std::string_view command, callback_t cb_ptr);
auto get_privmsg_callback(std::string_view command)
    -> void (*)(const Server&, const IRCMessagePrivmsg&);
callback_t get_callback(std::string_view command);
bool del_privmsg_callback(std::string_view command);

}  // namespace kbot
