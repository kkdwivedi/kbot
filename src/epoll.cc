#include <errno.h>
#include <sys/epoll.h>
#include <unistd.h>

#include <epoll.hh>
#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace kbot {
namespace io {

EpollManager::EpollManager(EpollManager &&m) {
  fd = m.fd;
  m.fd = -1;
  fd_map = std::move(m.fd_map);
  static_events = std::move(m.static_events);
}

EpollManager &EpollManager::operator=(EpollManager &&m) {
  close(fd);
  fd = m.fd;
  m.fd = -1;

  fd_map = std::move(m.fd_map);
  static_events = std::move(m.static_events);
  return *this;
}

EpollManager::~EpollManager() {
  for (auto &ctx : static_events) {
    if (ctx.type == EpollStaticEvent::Type::Exit) {
      ctx.cb(ctx);
    }
  }
}

std::optional<EpollManager> EpollManager::createNew() {
  int fd = epoll_create1(EPOLL_CLOEXEC);
  if (fd < 0) {
    return std::nullopt;
  }
  return EpollManager{fd};
}

void EpollManager::registerStaticEvent(
    EpollStaticEvent::Type type, std::function<void(EpollStaticEvent &)> cb) {
  static_events.push_back(EpollStaticEvent{type, std::move(cb)});
}

bool EpollManager::registerFd(int fd, EventFlags events, userdata_un data,
                              std::function<void(struct epoll_event)> callback,
                              ConfigFlags config) {
  auto it = fd_map.find(fd);
  if (it != fd_map.end()) {
    errno = EEXIST;
    return false;
  }
  struct epoll_event ev {
    ((uint32_t)events | config), data
  };
  int r = epoll_ctl(this->fd, EPOLL_CTL_ADD, fd, &ev);
  if (r < 0) {
    return false;
  }
  it->second = {{(uint32_t)events | config, data}, std::move(callback)};
  return true;
}

bool EpollManager::modifyFdEvents(int fd, EventFlags events) {
  if ((uint32_t)events & EpollConfigFullMask) {
    throw std::logic_error("Config flags must not be passed as event flags");
  }
  auto it = fd_map.find(fd);
  if (it == fd_map.end()) {
    errno = ENOENT;
    return false;
  }
  auto &ctx = it->second;
  auto ev = ctx.ev;
  ev.events = ctx.getConfigMask() | events;
  int r = epoll_ctl(this->fd, EPOLL_CTL_MOD, fd, &ev);
  if (r < 0) {
    return false;
  }
  ctx.ev = ev;
  return true;
}

bool EpollManager::modifyFdConfig(int fd, ConfigFlags config) {
  if ((uint32_t)config & EpollEventFullMask) {
    throw std::logic_error("Event flags must not be passed as config flags");
  }
  auto it = fd_map.find(fd);
  if (it == fd_map.end()) {
    errno = ENOENT;
    return false;
  }
  auto &ctx = it->second;
  auto ev = ctx.ev;
  ev.events = ctx.getEventMask() | config;
  int r = epoll_ctl(this->fd, EPOLL_CTL_MOD, fd, &ev);
  if (r < 0) {
    return false;
  }
  ctx.ev = ev;
  return true;
}

bool EpollManager::modifyFdCallback(
    int fd, std::function<void(struct epoll_event)> callback) {
  auto it = fd_map.find(fd);
  if (it == fd_map.end()) {
    errno = ENOENT;
    return false;
  }
  it->second.cb = std::move(callback);
  return true;
}

bool EpollManager::deleteFd(int fd) {
  auto it = fd_map.find(fd);
  if (it == fd_map.end()) {
    errno = ENOENT;
    return false;
  }
  fd_map.erase(it);
  int r = epoll_ctl(this->fd, EPOLL_CTL_DEL, fd, nullptr);
  if (r < 0) {
    return false;
  }
  return true;
}

}  // namespace io
}  // namespace kbot
