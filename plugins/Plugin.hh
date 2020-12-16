#pragma once
#include <absl/container/flat_hash_map.h>

#include <Manager.hh>
#include <Server.hh>
#include <UserCommand.hh>
#include <string_view>

#define COMMAND_VECTOR(...)                                                                    \
  namespace {                                                                                  \
  const std::pair<std::string, kbot::UserCommand::callback_t> __command_map[] = {__VA_ARGS__}; \
  }

#define PLUGIN_COMMAND(command_str, name, min, max) \
  STATIC_REGISTER_USER_COMMAND(command_str, Plugin_##name, min, max)

#define COMMAND_HELP_VECTOR(...)                                                           \
  namespace {                                                                              \
  const absl::flat_hash_map<std::string, const char *> __command_help_map = {__VA_ARGS__}; \
  }

#define HELP(name, help_string) \
  { #name, help_string }

#define COMMAND_CALLBACK(name, manager, msg) \
  void Plugin_##name(kbot::Manager &manager, const kbot::IRCMessagePrivMsg &msg)

#define __INIT_CALLBACK(name) void RegisterPluginCommands_##name(void *p)
#define __DELETE_CALLBACK(name) void DeletePluginCommands_##name(void *p)
#define __HELP_CALLBACK(name) void HelpPluginCommands_##name(void *p)

#define INIT_CALLBACK(name) \
  extern "C" {              \
  __INIT_CALLBACK(name) { RegisterCallbackImpl(p, __command_map); }
#define DELETE_CALLBACK(name) \
  __DELETE_CALLBACK(name) { DeleteCallbackImpl(p, __command_map); }
#define HELP_CALLBACK(name)                                          \
  __HELP_CALLBACK(name) { HelpCallbackImpl(p, __command_help_map); } \
  }

#define DECLARE_PLUGIN(name) \
  INIT_CALLBACK(name);       \
  DELETE_CALLBACK(name);     \
  HELP_CALLBACK(name);

namespace {

template <size_t N>
void RegisterCallbackImpl(
    void *p, const std::pair<std::string, kbot::UserCommand::callback_t> (&command_map)[N]) {
  assert(p != nullptr);
  auto s = static_cast<kbot::Server *>(p);
  s->AddPluginCommands(std::span(std::begin(command_map), N));
  LOG(INFO) << "Successfully registered plugin commands for ";
}

template <size_t N>
void DeleteCallbackImpl(
    void *p, const std::pair<std::string, kbot::UserCommand::callback_t> (&command_map)[N]) {
  assert(p != nullptr);
  auto s = static_cast<kbot::Server *>(p);
  std::array<std::string_view, N> sv_map;
  for (size_t i = 0; i < N; i++) {
    sv_map[i] = command_map[i].first;
  }
  s->RemovePluginCommands(std::span(sv_map.begin(), N));
  LOG(INFO) << "Successfully removed plugin commands for ";
}

[[maybe_unused]] inline void HelpCallbackImpl(
    void *p, const absl::flat_hash_map<std::string, const char *> &command_help_map) {
  assert(p);
  auto s = static_cast<std::pair<kbot::Manager *, kbot::IRCMessagePrivMsg *> *>(p);
  kbot::UserCommand::SendInvokerReply(*s->first, *s->second,
                                      command_help_map.at(s->second->GetUserCommand().substr(1)));
}

}  // namespace
