#pragma once

#include <cassert>
#include <mutex>
#include <deque>

#include <semaphore.h>

#include "server.hh"

namespace kbot {

// Threads request the supervisor to launch a new thread for a new server object, as users with
// admin privileges can request a new connection from any of the existing connections.
class ThreadRequest {
  std::mutex vec_mtx;
  std::deque<std::shared_ptr<Server>> req_deq;
public:
  sem_t req_sem;
  ThreadRequest()
  {
    sem_init(&req_sem, 0, 0);
  }
  ThreadRequest(const ThreadRequest&) = delete;
  ThreadRequest& operator=(const ThreadRequest&) = delete;
  ThreadRequest(ThreadRequest&&) = delete;
  ThreadRequest& operator=(ThreadRequest&&) = delete;
  ~ThreadRequest()
  {
    sem_destroy(&req_sem);
  }
  // Basic API
  void push_back(std::shared_ptr<Server> s)
  {
    std::lock_guard<std::mutex> lock(vec_mtx);
    req_deq.push_back(std::move(s));
    sem_post(&req_sem);
  }
  std::shared_ptr<Server> pop_front()
  {
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
auto get_privmsg_callback(std::string_view command) -> void(*)(const Server&, const IRCMessagePrivmsg&);
callback_t get_callback(std::string_view command);
bool del_privmsg_callback(std::string_view command);

} // namespace kbot
