#include <glog/logging.h>
#include <unistd.h>

#include <cstdio>
#include <iostream>
#include <loop.hh>
#include <memory>
#include <server.hh>
#include <stdexcept>
#include <thread>

#define KBOT_VERSION "0.1"

[[noreturn]] void usage(void) {
  LOG(INFO) << "Usage:   kbot -s <server> -p <port> -c <channel> -n <nickname>";
  LOG(INFO) << "              -x <password> -l (ssl)";
  LOG(INFO) << "Example: kbot chat.freenode.net 6667 ##kbot kbot";
  LOG(INFO) << "         kbot -s chat.freenode.net -n kbot -p 6667 -c ##kbot";
  LOG(INFO) << "Version " << KBOT_VERSION << " (" << __DATE__ << ", " << __TIME__ << ")";
  exit(0);
}

int main(int argc, char *argv[]) {
  // Default config
  const char *address = "chat.freenode.net";
  uint16_t port = 6667;
  const char *nickname = "kbot";
  const char *channel = "##kbot";
  std::string password = "";
  bool ssl = false;

  google::InitGoogleLogging(argv[0]);
  google::InstallFailureSignalHandler();
  int opt = -1;
  while ((opt = getopt(argc, argv, "hs:n:p:c:x::l")) != -1) {
    switch (opt) {
      case 's':
        address = optarg;
        break;
      case 'n':
        nickname = optarg;
        break;
      case 'p':
        int r;
        try {
          r = std::stoi(optarg);
        } catch (std::invalid_argument &) {
          LOG(INFO) << "Error: Port value invalid.";
          usage();
        } catch (std::out_of_range &) {
          LOG(INFO) << "Error: Port value out of range.";
          usage();
        }
        // Check for overflow
        if (r < 0 || static_cast<size_t>(r) > UINT16_MAX) {
          LOG(INFO) << "Port value passed is invalid or out of range, pass a "
                       "positive 16-bit integer.";
          usage();
        }
        port = static_cast<uint16_t>(r);
        break;
      case 'c':
        channel = optarg;
        break;
      case 'x':
        if (optarg != nullptr)
          password = optarg;
        else {
          std::cout << "Please enter your password: ";
          std::cin >> password;
        }
        break;
      case 'l':
        ssl = true;
        break;
      default:
        usage();
    }
  }

  auto server_opt = kbot::ConnectionNew(address, port, nickname);
  if (server_opt.has_value() == false) {
    LOG(INFO) << "Failed to establish connection to server";
    usage();
    return 1;
  }
  auto root = kbot::LaunchServerThread(
      [nickname, password, channel](kbot::Server &&server) {
        auto m = kbot::Manager::CreateNew(std::move(server));
        auto r = m.server.Login(nickname, password);
        if (r < 0) {
          PLOG(ERROR) << "Login failed";
          return;
        }
        m.server.JoinChannel(channel);
        m.server.SendChannel(channel, "Hello!");
        m.server.DumpInfo();
        kbot::WorkerRun(std::move(m));
      },
      std::move(server_opt.value()));

  root.join();
  LOG(INFO) << "Shutting down";
  return 0;
}
