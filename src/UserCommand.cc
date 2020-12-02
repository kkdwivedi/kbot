#include <absl/container/flat_hash_map.h>
#include <dlfcn.h>

#include <UserCommand.hh>
#include <irc.hh>
#include <loop.hh>
#include <mutex>
#include <server.hh>
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
  m.server.SendChannel(recv, std::string(msg.GetUser().nickname).append(": ").append(reply));
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
  UserCommandPlugin u;
  std::string_view plugin_name = msg.GetUserCommandParameters().at(0);
  if (u.OpenHandle(plugin_name)) {
    auto reg_func = u.GetRegistrationFunc(plugin_name);
    assert(reg_func);
    reg_func(&m.server);
    LOG(INFO) << "Successfully loaded plugin";
    std::unique_lock lock(m.server.user_command_plugin_map_mtx);
    m.server.user_command_plugin_map.insert(
        {std::string(plugin_name), UserCommandPlugin{std::move(u)}});
  } else {
    LOG(ERROR) << "Failed to load plugin";
  }
}

void BuiltinCommandUnloadPlugin(Manager &m, const IRCMessagePrivMsg &msg) {
  std::string_view plugin_name = msg.GetUserCommandParameters().at(0);
  if (auto it = m.server.user_command_plugin_map.find(plugin_name);
      it != m.server.user_command_plugin_map.end()) {
    auto del_func = it->second.GetDeletionFunc(it->first);
    assert(del_func);
    del_func(&m.server);
    m.server.user_command_plugin_map.erase(it);
  } else {
    SendInvokerReply(m, msg, "No such plugin loaded.");
  }
}

void BuiltinCommandHelp(Manager &m, const IRCMessagePrivMsg &msg) {
  SendInvokerReply(m, msg, "Commands available: hi, join, part, load, unload, help");
}

}  // namespace

const absl::flat_hash_map<std::string, callback_t> user_command_map = {
    STATIC_REGISTER_USER_COMMAND("hi", BuiltinCommandHi, 0, 0),
    STATIC_REGISTER_USER_COMMAND("nick", BuiltinCommandNick, 1, 1),
    STATIC_REGISTER_USER_COMMAND("join", BuiltinCommandJoin, 1, 1),
    STATIC_REGISTER_USER_COMMAND("part", BuiltinCommandPart, 1, 1),
    STATIC_REGISTER_USER_COMMAND("load", BuiltinCommandLoadPlugin, 1, 1),
    STATIC_REGISTER_USER_COMMAND("unload", BuiltinCommandUnloadPlugin, 1, 1),
    STATIC_REGISTER_USER_COMMAND("help", BuiltinCommandHelp, 0, 0),
};

// UserCommandPlugin

bool UserCommandPlugin::OpenHandle(std::string_view name) {
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

UserCommandPlugin::registration_callback_t GetFunc(void *handle, std::string_view symbol) {
  (void)dlerror();
  auto sym = dlsym(handle, symbol.data());
  if (sym) {
    return reinterpret_cast<UserCommandPlugin::registration_callback_t>(sym);
  } else {
    LOG(ERROR) << (dlerror() ?: "Symbol not found");
    return nullptr;
  }
}

}  // namespace

UserCommandPlugin::registration_callback_t UserCommandPlugin::GetRegistrationFunc(
    std::string_view plugin_name) {
  return GetFunc(handle, std::string("RegisterPluginCommands_").append(plugin_name));
}

UserCommandPlugin::registration_callback_t UserCommandPlugin::GetDeletionFunc(
    std::string_view plugin_name) {
  return GetFunc(handle, std::string("DeletePluginCommands_").append(plugin_name));
}

}  // namespace UserCommand
}  // namespace kbot
