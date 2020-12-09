#include <absl/container/flat_hash_set.h>

#include <IRC.hh>
#include <Manager.hh>
#include <Server.hh>
#include <UserCommand.hh>
#include <mutex>

#define VERSION_STRING "0.1"

namespace {

void PluginVersion(kbot::Manager &m, const kbot::IRCMessagePrivMsg &msg) {
  kbot::UserCommand::SendInvokerReply(m, msg, VERSION_STRING);
}

std::pair<std::string, kbot::UserCommand::callback_t> command_map[] = {
    STATIC_REGISTER_USER_COMMAND("version", PluginVersion, 0, 0),
};

std::mutex server_set_mtx;
absl::flat_hash_set<kbot::Server *> server_set;

bool AddServerToSet(kbot::Server *p) {
  assert(p);
  std::unique_lock lock(server_set_mtx);
  return server_set.insert(p).second;
}

void RemoveServerFromSet(kbot::Server *p) {
  assert(p);
  std::unique_lock lock(server_set_mtx);
  if (auto it = server_set.find(p); it != server_set.end()) {
    server_set.erase(it);
  } else {
    throw std::runtime_error("Server never registered with plugin");
  }
}

}  // namespace

extern "C" {

void RegisterPluginCommands_version(void *p) {
  assert(p);
  auto s = static_cast<kbot::Server *>(p);
  bool r = s->AddPluginCommands(command_map[0].first, command_map[0].second);
  if (r) {
    LOG(INFO) << "Successfully registered plugin commands";
    AddServerToSet(s);
  } else {
    LOG(ERROR) << "Failed to register plugin commands";
  }
}

void DeletePluginCommands_version(void *p) {
  assert(p);
  auto s = static_cast<kbot::Server *>(p);
  if (s->RemovePluginCommands(command_map[0].first)) {
    RemoveServerFromSet(s);
  }
}

void HelpPluginCommands_version(void *p) {
  assert(p);
  auto s = static_cast<std::pair<kbot::Manager *, kbot::IRCMessagePrivMsg *> *>(p);
  kbot::UserCommand::SendInvokerReply(*s->first, *s->second, "Usage: ,version");
}

}  // extern "C"
