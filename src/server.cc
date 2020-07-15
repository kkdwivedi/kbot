#include <iostream>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "server.hh"

namespace kbot {

server::server(const int sockfd, const std::string addr, const uint16_t portnum, const char *nick)
  : fd(sockfd), address(addr), port(portnum), nickname(nick)
{}

server::~server()
{
  close(fd);
}

const std::string& server::get_address()
{
  return address;
}

uint16_t server::get_port()
{
  return port;
}

const std::string& server::get_nickname()
{
  return nickname;
}

namespace {

// Lock held when creating/retrieving connections
std::mutex conn_mutex;
// Registry map for created connections
std::unordered_map<std::string, std::weak_ptr<server>> conn_cache;

// Caller must hold the conn_mutex lock
int connection_fd(const char *addr, const uint16_t port)
{
  int fd = -1;

  struct addrinfo hints, *result;
  std::memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = 0;

  int r = getaddrinfo(addr, std::to_string(port).c_str(), &hints, &result);
  if (r != 0)
    return fd;

  struct addrinfo *i = result;
  for (; i != nullptr; i = i->ai_next) {
    fd = socket(i->ai_family, i->ai_socktype|SOCK_CLOEXEC, i->ai_protocol);
    if (fd < 0)
      break;
    if (connect(fd, i->ai_addr, i->ai_addrlen) == 0)
      break;
  }

  freeaddrinfo(result);
  return fd;
}

} // namespace

// Get a shared_ptr to a new or preexisting server object
std::shared_ptr<server> connection_new(std::string_view address, const uint16_t port, const char *nickname)
{
  static size_t _id;
  std::string key(address);
  std::lock_guard<std::mutex> lock(conn_mutex);

  ++_id;
  if (auto it = conn_cache.find(key); it != conn_cache.end()) {
    auto& wptr = it->second;
    if (wptr.expired()) {
      auto fd = connection_fd(key.c_str(), port);
      if (fd < 0) {
	return nullptr;
      }
      auto sptr = std::shared_ptr<server>(new server(fd, std::move(key), port, nickname), connection_delete);
      wptr = sptr;
      return sptr;
    } else {
      return it->second.lock();
    }
  } else {
    auto fd = connection_fd(key.c_str(), port);
    if (fd < 0) {
      return nullptr;
    }
    auto sptr = std::shared_ptr<server>(new server(fd, key, port, nickname), connection_delete);
    conn_cache[std::move(key)] = std::weak_ptr<server>(sptr);
    return sptr;
  }
}

void connection_delete(server *s)
{
  assert(s);
  std::lock_guard<std::mutex> lock(conn_mutex);
  conn_cache.erase(s->get_address());
  delete s;
}

} // namespace kbot
