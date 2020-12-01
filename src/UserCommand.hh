#pragma once

#include <absl/container/flat_hash_map.h>

#include <irc.hh>
#include <loop.hh>
#include <shared_mutex>
#include <string>
#include <string_view>

namespace kbot {
namespace UserCommand {

#define _S(s) #s
#define S(s) _S(s)

#define COMMAND_PREFIX ","
#define ARGS_MIN 0
#define ARGS_MAX 1024

#define STATIC_REGISTER_USER_COMMAND(command, callback, min, max) \
  { ":" COMMAND_PREFIX command, &UserCommandForward<min, max, &callback> }

using callback_t = void (*)(Manager &, const IRCMessagePrivMsg &);

// Mutex held while operating on hash map
inline std::shared_mutex user_command_mtx;
// Hash map storing calback
extern absl::flat_hash_map<std::string, callback_t> user_command_map;

void SendInvokerReply(Manager &m, const IRCMessagePrivMsg &msg, std::string_view reply);
bool InvokerPermissionCheck(Manager &m, const IRCMessagePrivMsg &msg, IRCUserCapability mask);

template <unsigned min = ARGS_MIN, unsigned max = ARGS_MAX>
bool ExpectArgsRange(const IRCMessagePrivMsg &m) {
  // There are atleast two minimum arguments
  static_assert(min >= ARGS_MIN && max <= ARGS_MAX,
                "Ensure that min >=" S(ARGS_MIN) " and max <= " S(ARGS_MAX));
  if (m.GetParameters().size() >= min + 2 && m.GetParameters().size() <= max + 2) {
    return true;
  }
  return false;
}

template <unsigned min, unsigned max, callback_t cb_ptr>
void UserCommandForward(Manager &m, const IRCMessagePrivMsg &msg) {
  static_assert(cb_ptr != nullptr, "No callback present");
  if (ExpectArgsRange<min, max>(msg)) {
    cb_ptr(m, msg);
  } else {
    SendInvokerReply(m, msg,
                     "Incorrect number of arguments passed to command, see " COMMAND_PREFIX "help");
  }
}

}  // namespace UserCommand
}  // namespace kbot
