#include <atomic>
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
#include <glog/logging.h>

#include <irc.hh>
#include <server.hh>

namespace kbot {

// Server

void Server::dump_info() const
{
  DLOG(INFO) << "Dump for Server: " << address << '/' << port;
  {
    std::lock_guard<std::mutex> lock(nick_mtx);
    DLOG(INFO) << "Nickname: " << nickname;
  }
  std::lock_guard<std::mutex> lock(chan_mtx);
  for (auto& p : chan_id_map) {
    DLOG(INFO) << " Channel: " << p.second->get_name();
  }
}

void Server::set_state(const ServerState state_) const
{
  LOG(INFO) << "State transition for server: " << state_to_string(state) << " -> " << state_to_string(state_);
  state.store(state_, std::memory_order_relaxed);
}

// Channel API

Server::ChannelID Server::join_channel(const std::string& channel) {
  std::lock_guard<std::mutex> lock(chan_mtx);
  ChannelID id = SIZE_MAX;
  if (auto it = chan_string_map.find(channel); it == chan_string_map.end()) {
    auto r = IRC::JOIN(channel);
    if (r < 0) {
      LOG(ERROR) << "Failed to join channel: " << channel;
      return id;
    }
    id = ++chan_id;
    std::unique_ptr<Channel> uniq(new Channel(*this, channel, id));
    chan_id_map.insert({id, std::move(uniq)});
    chan_string_map.insert({std::move(channel), id});
  } else {
    return it->second;
  }
  return id;
}

bool Server::send_channel(ChannelID id, const std::string_view msg)
{
  std::lock_guard<std::mutex> lock(chan_mtx);
  auto it = chan_id_map.find(id);
  assert(it != chan_id_map.end());
  return it->second->send_msg(msg);
}

void Server::part_channel(Server::ChannelID id)
{
  std::lock_guard<std::mutex> lock(chan_mtx);
  auto it = chan_id_map.find(id);
  assert(it != chan_id_map.end());
  auto& chan_str = it->second->get_name();
  auto r = IRC::PART(chan_str);
  if (r < 0) {
    LOG(ERROR) << "Failed to part channel: " << chan_str;
    return;
  }
  chan_string_map.erase(chan_str);
  chan_id_map.erase(id);
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
    LOG(ERROR) << "Failed to resolve name: " << gai_strerror(errno);
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
std::shared_ptr<Server> connection_new(std::string address, const uint16_t port, const char *nickname)
{
  std::string key(address);
  std::lock_guard<std::mutex> lock(conn_mtx);
  DLOG(INFO) << "Initiating cache lookup for " << address.c_str() << '/' << port << '(' << nickname << ')';
  if (auto it = conn_cache.find(key); it != conn_cache.end()) {
    auto& wptr = it->second;
    if (wptr.expired()) {
      DLOG(INFO) << "Entry expired, recreating";
      auto fd = connection_fd(key.c_str(), port);
      // Due to releasing lock during the address resolution, possibly another thread succeeded in
      // creating the entry, in that case we return it, otherwise we know we failed.
      auto sptr = wptr.lock();
      if (fd < 0) {
	if (sptr != nullptr) {
	  DLOG(INFO) << "Resolution failed but entry was created";
	  return sptr;
	}
	DLOG(ERROR) << "Failed to create socket fd";
	return sptr;
      }
      sptr = std::shared_ptr<Server>(new Server(fd, std::move(key), port, nickname), connection_delete);
      wptr = sptr;
      DLOG(INFO) << "Successfully created server";
      sptr->set_state(ServerState::kConnected);
      return sptr;
    } else {
      DLOG(INFO) << "Found existing connection";
      return it->second.lock();
    }
  } else {
    DLOG(INFO) << "Creating new server";
    auto fd = connection_fd(key.c_str(), port);
    if (fd < 0) {
      if (auto it = conn_cache.find(key); it != conn_cache.end()) {
	DLOG(INFO) << "Resolution failed but entry was created";
	return it->second.lock();
      }
      DLOG(INFO) << "Failed to create socket fd";
      return nullptr;
    }
    auto sptr = std::shared_ptr<Server>(new Server(fd, key, port, nickname), connection_delete);
    conn_cache[std::move(key)] = std::weak_ptr<Server>(sptr);
    DLOG(INFO) << "Successfully created server";
    sptr->set_state(ServerState::kConnected);
    return sptr;
  }
}

void connection_delete(const Server *s)
{
  assert(s);
  std::lock_guard<std::mutex> lock(conn_mtx);
  conn_cache.erase(s->get_address());
  DLOG(INFO) << "Erasing server from cache: ";
  s->dump_info();
  delete s;
}

} // namespace kbot
