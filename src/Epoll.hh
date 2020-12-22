#include <absl/container/flat_hash_map.h>
#include <errno.h>
#include <sys/epoll.h>

#include <concepts>
#include <functional>
#include <map>
#include <optional>
#include <vector>

namespace kbot {
namespace io {

enum class StaticEventType {
  Pre,
  Post,
  Exit,
};

class EpollManager;

template <StaticEventType type>
struct EpollStaticEvent {
  std::function<void(EpollManager &)> cb;

  explicit EpollStaticEvent(std::function<void(EpollManager &)> cb) : cb(std::move(cb)) {}
};

struct EpollContext {
  struct epoll_event ev;
  std::function<void(struct epoll_event)> cb;
  bool enabled;

  uint32_t GetConfigMask() {
    return ev.events & (EPOLLET | EPOLLONESHOT | EPOLLWAKEUP | EPOLLEXCLUSIVE);
  }
  uint32_t GetEventMask() { return ev.events & (EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLPRI); }
};

class EpollManager {
  int fd = -1;
  absl::flat_hash_map<int, EpollContext> fd_map;
  struct {
    std::vector<EpollStaticEvent<StaticEventType::Pre>> pre;
    std::vector<EpollStaticEvent<StaticEventType::Post>> post;
    std::vector<EpollStaticEvent<StaticEventType::Exit>> exit;
  } static_events;

 protected:
  explicit EpollManager(int fd) : fd(fd) {}
  ~EpollManager();

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

  template <StaticEventType type>
  void RegisterStaticEvent(std::function<void(EpollManager &)> cb);
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

template <StaticEventType type>
void EpollManager::RegisterStaticEvent(std::function<void(EpollManager &)> cb) {
  if constexpr (type == StaticEventType::Pre) {
    static_events.pre.push_back(EpollStaticEvent<type>{std::move(cb)});
  } else if constexpr (type == StaticEventType::Post) {
    static_events.post.push_back(EpollStaticEvent<type>{std::move(cb)});
  } else if constexpr (type == StaticEventType::Exit) {
    static_events.exit.push_back(EpollStaticEvent<type>{std::move(cb)});
  }
}

}  // namespace io
}  // namespace kbot
