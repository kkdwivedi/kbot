#pragma once

#include <string>

namespace kbot {

class Server;

class Channel {
  Server& sref;
  const std::string name;
  std::size_t id;
public:
  explicit Channel(Server& s, std::string_view namesv, std::size_t _id) : sref(s), name(namesv), id(_id) {}
  ~Channel() {}
  const std::string& get_name() {
    return name;
  };
  std::size_t get_id() {
    return id;
  };
  Server& get_server() {
    return sref;
  };
};

} // namespace kbot
