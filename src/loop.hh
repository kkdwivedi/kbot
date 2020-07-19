#include <cassert>
#include <mutex>
#include <deque>

#include <semaphore.h>

#include "server.hh"

namespace kbot {

// Threads request the supervisor to launch a new thread for a new server object, as users with
// admin privileges can request a new connection from any of the existing connections.
class ThreadRequest {
  mutable std::mutex vec_mtx;
  mutable std::deque<std::shared_ptr<Server>> req_deq;
public:
  mutable sem_t req_sem;
  ThreadRequest() {
    sem_init(&req_sem, 0, 0);
  }
  ThreadRequest(const ThreadRequest&) = delete;
  ThreadRequest& operator=(const ThreadRequest&) = delete;
  ThreadRequest(ThreadRequest&&) = delete;
  ThreadRequest& operator=(ThreadRequest&&) = delete;
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
    // sem_wait outside
  }
  ~ThreadRequest() {
    sem_destroy(&req_sem);
  }
};

inline ThreadRequest tr;

void worker_run(std::shared_ptr<Server> ptr);
void supervisor_run();

} // namespace kbot
