#include <iostream>
#include <cassert>
#include <cstring>
#include <string_view>
#include <vector>
#include <thread>

#include <poll.h>

#include "loop.hh"
#include "server.hh"

namespace kbot {

std::vector<std::string> tokenize_msg_multi(std::string msg)
{
  std::vector<std::string> ret;
  char *p = msg.data();
  const char *delim = "\r\n";
  while ((p = std::strtok(p, delim))) {
    ret.emplace_back(p);
    p = nullptr;
  }
  return ret;
}

bool is_server_line(const std::string& line)
{}

void process_server_msg_line(Server *ptr, std::string line)
{}

void process_user_msg_line(Server *ptr, std::string line)
{}

void process_msg_line(Server *ptr, std::string line)
{
  if (line.substr(0, 4) == "PING") {
    std::clog << "Received PING messge, sending PONG\n";
    ptr->PONG(line);
  }
  if (is_server_line(line))
    process_server_msg_line(ptr, std::move(line));
  else process_user_msg_line(ptr, std::move(line));
}

void worker_run(std::shared_ptr<Server> ptr)
{
  std::clog << "Main loop for Server: " << *ptr;
  struct pollfd pfd = { .fd = ptr->fd, .events = POLLIN };
  for (;;) {
    std::string msg(ptr->recv_msg());
    if (msg == "") continue;
    std::vector<std::string> tok = tokenize_msg_multi(std::move(msg));
    for (auto& line : tok)
      process_msg_line(ptr.get(), std::move(line));
    poll(&pfd, 1, -1);
  }
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
