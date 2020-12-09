#include <arpa/inet.h>
#include <glog/logging.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <Database.hh>
#include <IRC.hh>
#include <Server.hh>
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <unordered_map>

namespace kbot {

// CommandPlugin
// Implements the class that owns each plugin shared object. Internally the object is ref-counted
// and access is thread safe so we don't need to take care of synchronization except when accessing
// our own data.

bool CommandPlugin::OpenHandle(std::string_view name) {
  struct RAII {
    char *ptr = get_current_dir_name();
    ~RAII() { free(ptr); }
  } r;
  if (r.ptr) {
    std::string cwd(r.ptr);
    cwd.append("/lib").append(name).append(".so");
    if (cwd.size() > PATH_MAX) {
      return false;
    } else {
      handle = dlopen(cwd.c_str(), RTLD_LAZY);
      if (handle != nullptr) {
        return true;
      } else {
        PLOG(ERROR) << "Failed to load plugin";
        return false;
      }
    }
  }
  return false;
}

namespace {

CommandPlugin::registration_callback_t GetFunc(void *handle, std::string_view symbol) {
  (void)dlerror();
  auto sym = dlsym(handle, symbol.data());
  if (sym) {
    return reinterpret_cast<CommandPlugin::registration_callback_t>(sym);
  } else {
    LOG(ERROR) << (dlerror() ?: "Symbol not found");
    return nullptr;
  }
}

}  // namespace

CommandPlugin::registration_callback_t CommandPlugin::GetRegistrationFunc(
    std::string_view plugin_name) {
  return GetFunc(handle, std::string("RegisterPluginCommands_").append(plugin_name));
}

CommandPlugin::registration_callback_t CommandPlugin::GetDeletionFunc(
    std::string_view plugin_name) {
  return GetFunc(handle, std::string("DeletePluginCommands_").append(plugin_name));
}

CommandPlugin::registration_callback_t CommandPlugin::GetHelpFunc(std::string_view plugin_name) {
  return GetFunc(handle, std::string("HelpPluginCommands_").append(plugin_name));
}

// Server

// Move support is only for initial setup (and move into Manager instance)
// Do not call in multithreaded context, things WILL break, no mutexes are held
Server::Server(Server &&s)
    : IRC(static_cast<IRC &&>(s)),
      address(std::move(s.address)),
      chan_map(std::move(s.chan_map)),
      nickname(std::move(s.nickname)),
      local_db(std::move(s.local_db)) {
  assert(s.state.load(std::memory_order_relaxed) == ServerState::kSetup);
  port = s.port;
}

// Likewise, don't move Server around except when setting things up
Server &Server::operator=(Server &&s) {
  assert(s.state.load(std::memory_order_relaxed) == ServerState::kSetup);
  static_cast<IRC &>(*this) = static_cast<IRC &&>(s);
  address = std::move(s.address);
  chan_map = std::move(s.chan_map);
  nickname = std::move(s.nickname);
  local_db = std::move(s.local_db);
  port = s.port;
  return *this;
}

void Server::DumpInfo() {
  DLOG(INFO) << "Dump for Server: " << address << '/' << port;
  {
    std::unique_lock lock(nick_mtx);
    DLOG(INFO) << "Nickname: " << nickname;
  }
  std::shared_lock read_lock(chan_mtx);
  DLOG(INFO) << " Channel(s): ";
  bool count = false;
  for (auto &p : chan_map) {
    if (p.second.state == Channel::Joined) {
      DLOG(INFO) << p.first << " ";
      count = true;
    }
  }
  if (!count) {
    DLOG(INFO) << "(none)";
  }
}

void Server::SetState(const ServerState state_) {
  LOG(INFO) << "State transition for server: " << StateToString(state_) << " -> "
            << StateToString(state_);
  state.store(state_, std::memory_order_relaxed);
}

// Channel API

void Server::JoinChannel(std::string_view channel) {
  std::unique_lock lock(chan_mtx);
  auto it = chan_map.find(channel);
  auto r = IRC::Join(channel);
  if (r < 0) {
    PLOG(ERROR) << "Failed to initiate Join request for channel: " << channel;
    return;
  }
  if (it == chan_map.end()) {
    chan_map.insert({std::string(channel), Channel{Channel::JoinRequested}});
  } else {
    it->second.state = Channel::JoinRequested;
    return;
  }
}

void Server::UpdateJoinChannel(std::string_view channel) {
  std::unique_lock lock(chan_mtx);
  if (auto it = chan_map.find(channel); it != chan_map.end()) {
    if (it->second.state == Channel::JoinRequested) {
      it->second.state = Channel::Joined;
    } else {
      DLOG(INFO) << "Part has already been requested for " << channel;
    }
  }
}

void Server::UpdatePartChannel(std::string_view channel) {
  std::unique_lock lock(chan_mtx);
  if (auto it = chan_map.find(channel); it != chan_map.end()) {
    if (it->second.state == Channel::PartRequested) {
      chan_map.erase(it);
    } else {
      DLOG(INFO) << "Rejoin has already been requested for " << channel;
    }
  }
}

bool Server::SendChannel(std::string_view channel, std::string_view msg) {
  return IRC::PrivMsg(channel, msg);
}

bool Server::PartChannel(std::string_view channel) {
  std::unique_lock lock(chan_mtx);
  if (auto it = chan_map.find(channel); it != chan_map.end()) {
    auto r = IRC::Part(channel);
    if (r < 0) {
      PLOG(ERROR) << "Failed to initiate Part request for channel: " << channel;
      return false;
    }
    it->second.state = Channel::PartRequested;
    return true;
  } else {
    LOG(ERROR) << "Failed to part channel: " << channel << "; No such channel present";
    return false;
  }
}

// Plugin API
// Ensure that read lock is not held for the map when calling these methods, especially if the call
// is not punted to the workqueue

bool Server::AddPluginCommands(std::string_view name, Server::callback_t callback) {
  assert(callback);
  std::unique_lock lock(user_command_mtx);
  return user_command_map.insert({std::string(name), callback}).second;
}

bool Server::RemovePluginCommands(std::string_view name) {
  std::unique_lock lock(user_command_mtx);
  if (auto it = user_command_map.find(name); it != user_command_map.end()) {
    user_command_map.erase(it);
    return true;
  }
  LOG(ERROR) << "Command " << name << " not found, cannot remove for server " << GetAddress();
  return false;
}

namespace {

int GetConnectionFd(const char *addr, uint16_t port) {
  int fd = -1;

  struct addrinfo hints, *result;
  std::memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = 0;

  int r = getaddrinfo(addr, std::to_string(port).c_str(), &hints, &result);
  if (r != 0) {
    LOG(ERROR) << "Failed to resolve name: " << gai_strerror(errno);
    return fd;
  }

  struct addrinfo *i = result;
  for (; i != nullptr; i = i->ai_next) {
    fd = socket(i->ai_family, i->ai_socktype | SOCK_CLOEXEC, i->ai_protocol);
    if (fd < 0) break;
    if (connect(fd, i->ai_addr, i->ai_addrlen) == 0) break;
  }

  freeaddrinfo(result);
  return fd;
}

}  // namespace

std::optional<Server> ConnectionNew(std::string address, uint16_t port, const char *nickname) {
  int fd = GetConnectionFd(address.c_str(), port);
  if (fd < 0) {
    PLOG(ERROR) << "Failed to create server for " << address << "/" << port << " (" << nickname
                << ')';
    return std::nullopt;
  }
  return Server{fd, address, port, nickname};
}

}  // namespace kbot
