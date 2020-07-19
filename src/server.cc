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
  std::clog << "Constructing Server with credentials:\n" << *this;
}

Server::~Server()
{
  std::clog << "Destructing Server with credentials:\n" << *this;
  close(fd);
}

constexpr const char* Server::state_to_string(const enum ServerState state)
{
  return ServerStateStringTable[static_cast<int>(state)];
}

void Server::dump_info() const
{
  std::clog << *this;
}

enum ServerState Server::get_state() const
{
  return state;
}

void Server::set_state(const enum ServerState state) const
{
  std::clog << "State transition for Server with credentials: ";
  std::lock_guard<std::mutex> lock(state_mtx);
  std::clog << state_to_string(this->state) << " -> " << state_to_string(state);
  this->state = state;
  std::clog << "\n" << *this;
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

  // Temporarily release lock to allow others to proceed
  conn_mtx.unlock();
  int r = getaddrinfo(addr, std::to_string(port).c_str(), &hints, &result);
  conn_mtx.lock();
  if (r != 0) {
    std::clog << "Failed to resolve name: " << gai_strerror(errno) << '\n';
    return fd;
  }

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

// TODO: the whole approach of dropping the lock is suboptimal and error prone, find an alternative

// Get a shared_ptr to a new or preexisting Server
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
      std::clog << id_str << "Server was erased, recreating...\n";
      auto fd = connection_fd(key.c_str(), port);
      // Due to releasing lock during the address resolution, possibly another thread succeeded in
      // creating the entry, in that case we return it, otherwise we know we failed.
      auto sptr = wptr.lock();
      if (fd < 0) {
	if (sptr != nullptr) {
	  std::clog << id_str << "Failed resolution but entry was created, returning\n";
	  return sptr;
	}
	std::clog << id_str << "Failed to create socket fd\n";
	return sptr;
      }
      sptr = std::shared_ptr<Server>(new Server(fd, std::move(key), port, nickname), connection_delete);
      wptr = sptr;
      std::clog << id_str << "Successfully created Server\n";
      sptr->set_state(ServerState::kConnected);
      return sptr;
    } else {
      std::clog << id_str << "Found existing Server in cache, returning\n";
      return it->second.lock();
    }
  } else {
    std::clog << id_str << "Server absent, creating a new Server...\n";
    auto fd = connection_fd(key.c_str(), port);
    if (fd < 0) {
      if (auto it = conn_cache.find(key); it != conn_cache.end()) {
	std::clog << id_str << "Failed resolution but entry was created, returning\n";
	return it->second.lock();
      }
      std::clog << id_str << "Failed to create socket fd\n";
      return nullptr;
    }
    auto sptr = std::shared_ptr<Server>(new Server(fd, key, port, nickname), connection_delete);
    conn_cache[std::move(key)] = std::weak_ptr<Server>(sptr);
    std::clog << id_str << "Successfully created Server\n";
    sptr->set_state(ServerState::kConnected);
    return sptr;
  }
}

void connection_delete(const Server *s)
{
  assert(s);
  std::lock_guard<std::mutex> lock(conn_mtx);
  conn_cache.erase(s->get_address());
  std::clog << "Erasing Server with credentials from connection cache: \n" << *s;
  delete s;
}

std::ostream& operator<<(std::ostream& o, const Server& s)
{
  return o << "  Address:  " << s.address << ":" << s.port << " (" << s.state_to_string(s.state)
	   << ")" << " [" << s.nickname << "]\n";

}

} // namespace kbot
