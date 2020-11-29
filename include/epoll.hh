#include <errno.h>
#include <sys/epoll.h>

#include <concepts>
#include <functional>
#include <map>
#include <optional>
#include <vector>

namespace kbot {
namespace io {

struct EpollStaticEvent {
  enum class Type {
    Pre = 0,
    Post = 1,
    Exit = 2,
  } type;
  explicit EpollStaticEvent(Type type, std::function<void(EpollStaticEvent &self)> cb)
      : type(type), cb(std::move(cb)) {}
  void PreEnable() { type = Type::Pre; }
  void PostEnable() { type = Type::Post; }
  void ExitEnable() { type = Type::Exit; }
  std::function<void(EpollStaticEvent &self)> cb;
};

struct EpollContext {
  struct epoll_event ev;
  std::function<void(struct epoll_event)> cb;
  bool enabled;

  uint32_t GetConfigMask() {
    uint32_t mask = 0;
    if (ev.events & EPOLLET) mask |= EPOLLET;
    if (ev.events & EPOLLONESHOT) mask |= EPOLLONESHOT;
    if (ev.events & EPOLLWAKEUP) mask |= EPOLLWAKEUP;
    if (ev.events & EPOLLEXCLUSIVE) mask |= EPOLLEXCLUSIVE;
    return mask;
  }
  uint32_t GetEventMask() {
    uint32_t mask = 0;
    if (ev.events & EPOLLIN) mask |= EPOLLIN;
    if (ev.events & EPOLLOUT) mask |= EPOLLOUT;
    if (ev.events & EPOLLRDHUP) mask |= EPOLLRDHUP;
    if (ev.events & EPOLLPRI) mask |= EPOLLPRI;
    return mask;
  }
};

class EpollManager {
  int fd = -1;
  std::map<int, EpollContext> fd_map;
  std::vector<EpollStaticEvent> static_events;

  explicit EpollManager(int fd) : fd(fd) {}

 public:
  using userdata_un = decltype(epoll_event{}.data);

  enum EventFlags : uint32_t {
    EpollDefault = 0,
    EpollIn = EPOLLIN,
    EpollOut = EPOLLOUT,
    EpollRdHup = EPOLLRDHUP,
    EpollPri = EPOLLPRI,
    EpollEventFullMask = EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLPRI | EPOLLERR,
  };

  enum ConfigFlags : uint32_t {
    EpollConfigDefault = 0,
    EpollEdgeTriggered = EPOLLET,
    EpollOneshot = EPOLLONESHOT,
    EpollWakeup = EPOLLWAKEUP,
    EpollExclusive = EPOLLEXCLUSIVE,
    EpollConfigFullMask = EPOLLET | EPOLLONESHOT | EPOLLWAKEUP | EPOLLEXCLUSIVE,
  };

  EpollManager(const EpollManager &) = delete;
  EpollManager &operator=(const EpollManager &) = delete;
  EpollManager(EpollManager &&);
  EpollManager &operator=(EpollManager &&);
  ~EpollManager();

  static std::optional<EpollManager> CreateNew();
  void RegisterStaticEvent(EpollStaticEvent::Type type,
                           std::function<void(EpollStaticEvent &self)> cb);
  bool RegisterFd(int fd, EventFlags events, std::function<void(struct epoll_event)> callback,
                  ConfigFlags config);
  bool EnableFd(int fd);
  bool DisableFd(int fd);
  bool ModifyFdEvents(int fd, EventFlags events);
  bool ModifyFdConfig(int fd, ConfigFlags configs);
  bool ModifyFdCallback(int fd, std::function<void(struct epoll_event)> callback);
  bool DeleteFd(int fd);
  int RunEventLoop(int timeout);
};

}  // namespace io
}  // namespace kbot
