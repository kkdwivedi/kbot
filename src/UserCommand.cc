#include <absl/container/flat_hash_map.h>
#include <dlfcn.h>
#include <fmt/format.h>

#include <IRC.hh>
#include <Manager.hh>
#include <Server.hh>
#include <UserCommand.hh>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <string_view>

namespace kbot {
namespace UserCommand {

void SendInvokerReply(Manager &m, const IRCMessagePrivMsg &msg, std::string_view reply) {
  auto u = msg.GetUser();
  std::string_view recv = msg.GetChannel();
  if (recv == m.server.GetNickname()) {
    // Private buffer
    recv = u.nickname;
  }
  m.server.SendChannel(recv, fmt::format("{}: {}", msg.GetUser().nickname, reply));
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
    if (!m.server.PartChannel(msg.GetUserCommandParameters().at(0))) {
      SendInvokerReply(m, msg, "Error: No such channel exists.");
    }
  }
}

void BuiltinCommandLoadPlugin(Manager &m, const IRCMessagePrivMsg &msg) {
  CommandPlugin u;
  std::string_view plugin_name = msg.GetUserCommandParameters().at(0);
  if (u.OpenHandle(plugin_name)) {
    auto reg_func = u.GetRegistrationFunc(plugin_name);
    assert(reg_func);
    reg_func(&m.server);
    LOG(INFO) << "Successfully loaded plugin";
    std::unique_lock lock(m.server.plugins_map_mtx);
    m.server.plugins_map.insert({std::string(plugin_name), CommandPlugin{std::move(u)}});
    SendInvokerReply(m, msg, fmt::format("Loaded {}", plugin_name));
  } else {
    SendInvokerReply(m, msg, "Failed to load plugin.");
  }
}

void BuiltinCommandUnloadPlugin(Manager &m, const IRCMessagePrivMsg &msg) {
  std::unique_lock lock(m.server.plugins_map_mtx);
  std::string_view plugin_name = msg.GetUserCommandParameters().at(0);
  if (auto it = m.server.plugins_map.find(plugin_name); it != m.server.plugins_map.end()) {
    auto del_func = it->second.GetDeletionFunc(it->first);
    assert(del_func);
    del_func(&m.server);
    LOG(INFO) << "Successfully unloaded plugin";
    m.server.plugins_map.erase(it);
    SendInvokerReply(m, msg, fmt::format("Unloaded {}", plugin_name));
  } else {
    SendInvokerReply(m, msg, "No such plugin loaded.");
  }
}

void BuiltinCommandHelp(Manager &m, const IRCMessagePrivMsg &msg) {
  auto v = msg.GetUserCommandParameters();
  if (v.size()) {
    std::shared_lock lock(m.server.plugins_map_mtx);
    auto it = m.server.plugins_map.find(v[0]);
    if (it != m.server.plugins_map.end()) {
      auto p = std::make_pair(&m, &msg);
      it->second.GetHelpFunc(v[0])(&p);
    } else {
      SendInvokerReply(m, msg, "No such plugin loaded.");
    }
  } else {
    SendInvokerReply(m, msg, "Commands available: ,hi ,nick ,join ,part ,load ,unload ,quit ,help");
    std::string plugin_list;
    {
      std::unique_lock lock(m.server.user_command_mtx);
      plugin_list.append("Plugin commands available: ");
      for (auto &p : m.server.user_command_map) {
        plugin_list.append(p.first.substr(1)).append(" ");
      }
    }
    SendInvokerReply(m, msg, plugin_list);
  }
}

}  // namespace

const absl::flat_hash_map<std::string, callback_t> user_command_map = {
    STATIC_REGISTER_USER_COMMAND("hi", BuiltinCommandHi, 0, 0),
    STATIC_REGISTER_USER_COMMAND("nick", BuiltinCommandNick, 1, 1),
    STATIC_REGISTER_USER_COMMAND("join", BuiltinCommandJoin, 1, 1),
    STATIC_REGISTER_USER_COMMAND("part", BuiltinCommandPart, 1, 1),
    STATIC_REGISTER_USER_COMMAND("load", BuiltinCommandLoadPlugin, 1, 1),
    STATIC_REGISTER_USER_COMMAND("unload", BuiltinCommandUnloadPlugin, 1, 1),
    STATIC_REGISTER_USER_COMMAND("help", BuiltinCommandHelp, 0, 1),
};

}  // namespace UserCommand
}  // namespace kbot
