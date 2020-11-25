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
  enum Type {
    Pre = 0,
    Post = 1,
    Exit = 2,
  } type;
  explicit EpollStaticEvent(Type type,
                            std::function<void(EpollStaticEvent& self)> cb)
      : type(type), cb(std::move(cb)) {}
  void preEnable() { type = Pre; }
  void postEnable() { type = Post; }
  void exitEnable() { type = Exit; }
  std::function<void(EpollStaticEvent& self)> cb;
};

struct EpollContext {
  struct epoll_event ev;
  std::function<void(struct epoll_event)> cb;

  uint32_t getConfigMask() {
    uint32_t mask = 0;
    if (ev.events & EPOLLET) mask |= EPOLLET;
    if (ev.events & EPOLLONESHOT) mask |= EPOLLONESHOT;
    if (ev.events & EPOLLWAKEUP) mask |= EPOLLWAKEUP;
    if (ev.events & EPOLLEXCLUSIVE) mask |= EPOLLEXCLUSIVE;
    return mask;
  }
  uint32_t getEventMask() {
    uint32_t mask = 0;
    if (ev.events & EPOLLIN) mask |= EPOLLIN;
    if (ev.events & EPOLLOUT) mask |= EPOLLOUT;
    if (ev.events & EPOLLRDHUP) mask |= EPOLLRDHUP;
    if (ev.events & EPOLLPRI) mask |= EPOLLPRI;
    return mask;
  }
};

class EpollManager {
  int fd;
  std::vector<struct epoll_event> events;
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

  EpollManager(const EpollManager&) = delete;
  EpollManager& operator=(const EpollManager&) = delete;
  EpollManager(EpollManager&&);
  EpollManager& operator=(EpollManager&&);
  ~EpollManager();

  static std::optional<EpollManager> createNew();
  bool registerFd(int fd, EventFlags events, userdata_un data,
                  std::function<void(struct epoll_event)> callback,
                  ConfigFlags config);
  bool modifyFdEvents(int fd, EventFlags events);
  bool modifyFdConfig(int fd, ConfigFlags configs);
  bool modifyFdCallback(std::function<void(struct epoll_event)> callback);
  bool deleteFd(int fd);
};

// clang-format off
template <class T>
concept EventSource = requires (T t) {
  { t.getEpollContext() } -> std::same_as<EpollContext>;
};
// clang-format on

}  // namespace io
}  // namespace kbot
