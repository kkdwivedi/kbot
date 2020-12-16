#include <Plugin.hh>
#include <UserCommand.hh>

COMMAND_CALLBACK(version, m, msg) { kbot::UserCommand::SendInvokerReply(m, msg, "Beta."); }
COMMAND_HELP_VECTOR(HELP(version, "Usage: ,version"));
COMMAND_VECTOR(PLUGIN_COMMAND("version", version, 0, 0));
DECLARE_PLUGIN(version);
