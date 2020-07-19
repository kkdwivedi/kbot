#include <iostream>
#include <cassert>
#include <chrono>
#include <thread>
#include <vector>

#include "loop.hh"
#include "server.hh"

using namespace std::chrono_literals;

namespace kbot {

void worker_run(std::shared_ptr<Server> ptr)
{
  std::clog << "Main loop for Server: " << *ptr;
  std::this_thread::sleep_for(1s);
  kbot::tr.push_back(nullptr);
}

void supervisor_run()
{
  std::vector<std::jthread> jthr_vec;
  for (;;) {
    sem_wait(&tr.req_sem);
    auto sptr = tr.pop_front();
    if (sptr == nullptr) {
      std::clog << "Received notification to quit\n";
      break;
    }
    jthr_vec.emplace_back(worker_run, sptr);
  }
}

} // namespace kbot
