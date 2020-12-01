#include <absl/container/flat_hash_map.h>

#include <UserCommand.hh>
#include <irc.hh>
#include <loop.hh>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>

namespace kbot {
namespace UserCommand {

void SendInvokerReply(Manager &m, const IRCMessagePrivMsg &msg, std::string_view reply) {
  m.server.SendChannel(msg.GetChannel(),
                       std::string(msg.GetUser().nickname).append(": ").append(reply));
}

bool InvokerPermissionCheck(Manager &m, const IRCMessagePrivMsg &msg, IRCUserCapability mask) {
  if (Message::IsUserCapable(msg.GetUser(), mask)) {
    return true;
  } else {
    SendInvokerReply(m, msg, "Error: Permission denied.");
  }
  return false;
}

namespace {

// Builtin User Commands

void BuiltinCommandHi(Manager &m, const IRCMessagePrivMsg &msg) {
  SendInvokerReply(m, msg, "Hello!");
}

void BuiltinCommandNick(Manager &m, const IRCMessagePrivMsg &msg) {
  if (InvokerPermissionCheck(m, msg, IRCUserCapability::kNickModify)) {
    m.server.SetNickname(msg.GetUserCommandParameters().at(0));
  }
}

void BuiltinCommandJoin(Manager &m, const IRCMessagePrivMsg &msg) {
  if (InvokerPermissionCheck(m, msg, IRCUserCapability::kJoin)) {
    m.server.JoinChannel(msg.GetUserCommandParameters().at(0));
  }
}

void BuiltinCommandPart(Manager &m, const IRCMessagePrivMsg &msg) {
  if (InvokerPermissionCheck(m, msg, IRCUserCapability::kPart)) {
    m.server.PartChannel(msg.GetUserCommandParameters().at(0));
  }
}

void BuiltinCommandLoadPlugin(Manager &m, const IRCMessagePrivMsg &msg) {
  SendInvokerReply(m, msg, "Unimplemented.");
}

void BuiltinCommandUnloadPlugin(Manager &m, const IRCMessagePrivMsg &msg) {
  SendInvokerReply(m, msg, "Unimplemented.");
}

void BuiltinCommandHelp(Manager &m, const IRCMessagePrivMsg &msg) {
  SendInvokerReply(m, msg, "Commands available: hi, join, part, load, unload, help");
}

}  // namespace

absl::flat_hash_map<std::string, callback_t> user_command_map = {
    STATIC_REGISTER_USER_COMMAND("hi", BuiltinCommandHi, 0, 0),
    STATIC_REGISTER_USER_COMMAND("nick", BuiltinCommandNick, 1, 1),
    STATIC_REGISTER_USER_COMMAND("join", BuiltinCommandJoin, 1, 1),
    STATIC_REGISTER_USER_COMMAND("part", BuiltinCommandPart, 1, 1),
    STATIC_REGISTER_USER_COMMAND("load", BuiltinCommandLoadPlugin, ARGS_MIN, ARGS_MAX),
    STATIC_REGISTER_USER_COMMAND("unload", BuiltinCommandUnloadPlugin, ARGS_MIN, ARGS_MAX),
    STATIC_REGISTER_USER_COMMAND("help", BuiltinCommandHelp, 0, 0),
};

}  // namespace UserCommand
}  // namespace kbot
