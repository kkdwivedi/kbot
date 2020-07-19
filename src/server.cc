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

Server::Server(const int sockfd, const std::string addr, const uint16_t portnum, const char *nick)
  : fd(sockfd), address(addr), port(portnum), nickname(nick)
{
  std::clog << "Constructing Server object with credentials:\n"
	    << "  Address:  " << address << ":" << port << "\n"
	    << "  Nickname: " << nickname << "\n";
}

Server::~Server()
{
  std::clog << "Destructing Server object with credentials:\n"
	    << "  Address:  " << address << ":" << port << "\n"
	    << "  Nickname: " << nickname << "\n";
  close(fd);
}

enum ServerState Server::get_state() const
{
  return state;
}

void Server::set_state(enum ServerState state) const
{
  std::lock_guard<std::mutex> lock(state_mtx);
  this->state = state;
}

const std::string& Server::get_address() const
{
  return address;
}

uint16_t Server::get_port() const
{
  return port;
}

const std::string& Server::get_nickname() const
{
  return nickname;
}

void Server::set_nickname(std::string_view nickname) const
{
  std::lock_guard<std::mutex> lock(nick_mtx);
  this->nickname = nickname;
}

namespace {

// Lock held when creating/retrieving connections
std::mutex conn_mtx;
// Registry map for created connections
std::unordered_map<std::string, std::weak_ptr<Server>> conn_cache;

// Caller must hold the conn_mtx lock
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

// Get a shared_ptr to a new or preexisting Server object
std::shared_ptr<Server> connection_new(std::string_view address, const uint16_t port, const char *nickname)
{
  static size_t _id;
  std::string key(address);
  std::lock_guard<std::mutex> lock(conn_mtx);

  ++_id;
  auto id_str = [](std::ostream& o) -> std::ostream& { return o << "[Lookup ID=" << _id << "] "; };

  std::clog << id_str << "Initiating cache lookup for " << address << ":" << port << " (" << nickname << ")\n";
  if (auto it = conn_cache.find(key); it != conn_cache.end()) {
    auto& wptr = it->second;
    if (wptr.expired()) {
      std::clog << id_str << "Server object was erased, recreating...\n";
      auto fd = connection_fd(key.c_str(), port);
      if (fd < 0) {
	std::clog << id_str << "Failed to create socket fd\n";
	return nullptr;
      }
      auto sptr = std::shared_ptr<Server>(new Server(fd, std::move(key), port, nickname), connection_delete);
      wptr = sptr;
      std::clog << id_str << "Successfully created Server object\n";
      return sptr;
    } else {
      std::clog << id_str << "Found existing Server object in cache\n";
      return it->second.lock();
    }
  } else {
    std::clog << id_str << "Server object absent, creating a new object...\n";
    auto fd = connection_fd(key.c_str(), port);
    if (fd < 0) {
      std::clog << id_str << "Failed to create socket fd\n";
      return nullptr;
    }
    auto sptr = std::shared_ptr<Server>(new Server(fd, key, port, nickname), connection_delete);
    conn_cache[std::move(key)] = std::weak_ptr<Server>(sptr);
    std::clog << id_str << "Successfully created Server object\n";
    return sptr;
  }
}

void connection_delete(Server *s)
{
  assert(s);
  std::lock_guard<std::mutex> lock(conn_mtx);
  conn_cache.erase(s->get_address());
  std::clog << "Erasing Server object (" << s->get_address() << ":" << s->get_port()
	    << " [" << s->get_nickname() << "]) from connection cache\n";
  delete s;
}

} // namespace kbot
