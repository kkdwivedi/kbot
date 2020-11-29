#include <errno.h>
#include <sys/epoll.h>
#include <unistd.h>

#include <cassert>
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
  if (fd >= 0) close(fd);
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
  if (fd >= 0) {
    close(fd);
  }
}

std::optional<EpollManager> EpollManager::CreateNew() {
  int fd = epoll_create1(EPOLL_CLOEXEC);
  if (fd < 0) {
    return std::nullopt;
  }
  return EpollManager{fd};
}

void EpollManager::RegisterStaticEvent(EpollStaticEvent::Type type,
                                       std::function<void(EpollStaticEvent &)> cb) {
  static_events.push_back(EpollStaticEvent{type, std::move(cb)});
}

bool EpollManager::RegisterFd(int fd, EventFlags events,
                              std::function<void(struct epoll_event)> callback,
                              ConfigFlags config) {
  auto it = fd_map.find(fd);
  if (it != fd_map.end()) {
    errno = EEXIST;
    return false;
  }
  userdata_un data = {.fd = fd};
  struct epoll_event ev {
    ((uint32_t)events | config), data
  };
  int r = epoll_ctl(this->fd, EPOLL_CTL_ADD, fd, &ev);
  if (r < 0) {
    return false;
  }
  fd_map[fd] = EpollContext{ev, std::move(callback), true};
  return true;
}

bool EpollManager::EnableFd(int fd) {
  auto it = fd_map.find(fd);
  if (it != fd_map.end()) {
    if (it->second.enabled == false) {
      int r = epoll_ctl(this->fd, EPOLL_CTL_ADD, fd, &it->second.ev);
      if (r < 0) {
        assert(errno != EEXIST);
        return false;
      }
    }
    return it->second.enabled = true;
  } else {
    return false;
  }
}

bool EpollManager::DisableFd(int fd) {
  auto it = fd_map.find(fd);
  if (it != fd_map.end()) {
    if (it->second.enabled == true) {
      int r = epoll_ctl(this->fd, EPOLL_CTL_DEL, fd, &it->second.ev);
      if (r < 0) {
        assert(errno != ENOENT);
        return false;
      }
    }
    it->second.enabled = false;
    return true;
  } else {
    return false;
  }
}

bool EpollManager::ModifyFdEvents(int fd, EventFlags events) {
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
  ev.events = ctx.GetConfigMask() | events;
  int r = epoll_ctl(this->fd, EPOLL_CTL_MOD, fd, &ev);
  if (r < 0) {
    return false;
  }
  ctx.ev = ev;
  return true;
}

bool EpollManager::ModifyFdConfig(int fd, ConfigFlags config) {
  if ((uint32_t)config & EpollEventFullMask) {
    throw std::logic_error("Event flags must not be passed as config flags");
  }
  if ((uint32_t)config & EPOLLEXCLUSIVE) {
    throw std::logic_error("EpollExclusive cannot be passed during a modify operation");
  }
  auto it = fd_map.find(fd);
  if (it == fd_map.end()) {
    errno = ENOENT;
    return false;
  }
  auto &ctx = it->second;
  auto ev = ctx.ev;
  ev.events = ctx.GetEventMask() | config;
  int r = epoll_ctl(this->fd, EPOLL_CTL_MOD, fd, &ev);
  if (r < 0) {
    return false;
  }
  ctx.ev = ev;
  return true;
}

bool EpollManager::ModifyFdCallback(int fd, std::function<void(struct epoll_event)> callback) {
  auto it = fd_map.find(fd);
  if (it == fd_map.end()) {
    errno = ENOENT;
    return false;
  }
  it->second.cb = std::move(callback);
  return true;
}

bool EpollManager::DeleteFd(int fd) {
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

int EpollManager::RunEventLoop(int timeout = 0) {
  for (auto &ctx : static_events) {
    if (ctx.type == EpollStaticEvent::Type::Pre) {
      ctx.cb(ctx);
    }
  }

  static std::vector<struct epoll_event> events_vec;
  events_vec.resize(fd_map.size());

  for (;;) {
    int r = epoll_wait(fd, events_vec.data(), static_cast<int>(events_vec.size()), timeout);
    if (r < 0) {
      if (errno != EINTR) {
        return r;
      } else {
        continue;
      }
    }

    assert(r);
    for (size_t i = 0; i < (size_t)r; i++) {
      auto it = fd_map.find(events_vec[i].data.fd);
      if (it == fd_map.end()) {
        // fd is not in map, but being polled, something is borked...
        errno = ENOENT;
        return r = -1;
      } else if (it->second.enabled) {
        it->second.cb(events_vec[i]);
      }
    }

    break;
  }

  for (auto &ctx : static_events) {
    if (ctx.type == EpollStaticEvent::Type::Post) {
      ctx.cb(ctx);
    }
  }

  return 0;
}

}  // namespace io
}  // namespace kbot
