#include <iostream>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <thread>

#include <unistd.h>

#include <log.hh>
#include <loop.hh>
#include <server.hh>

#define KBOT_VERSION "0.1"

[[noreturn]] void usage(void)
{
  std::cerr << "Usage:   kbot -s <server> -p <port> -c <channel> -n <nickname>\n";
  std::cerr << "              -x <password> -l (ssl)\n";
  std::cerr << "Example: kbot chat.freenode.net 6667 ##kbot kbot\n";
  std::cerr << "         kbot -s chat.freenode.net -n kbot -p 6667 -c ##kbot\n";
  std::cerr << "Version " << KBOT_VERSION << " (" << __DATE__ << ", " << __TIME__ << ")\n";
  exit(0);
}

int main(int argc, char *argv[])
{
  // Default config
  const char *address = "chat.freenode.net";
  uint16_t port = 6667;
  const char *nickname = "kbot";
  const char *channel = "##kbot";
  std::string password = "";
  bool ssl = false;

  int opt = -1;
  while ((opt = getopt(argc, argv, "s:n:p:c:x::l")) != -1) {
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
      } catch (std::invalid_argument&) {
	std::cerr << "Error: Port value invalid.\n\n";
	usage();
      } catch (std::out_of_range&) {
	std::cerr << "Error: Port value out of range.\n\n";
	usage();
      }
      // Check for overflow
      if (r < 0 || static_cast<size_t>(r) > UINT16_MAX) {
	std::cerr << "Port value passed is invalid or out of range, please pass a positive 16-bit integer.\n\n";
	usage();
      }
      port = static_cast<uint16_t>(r);
      break;
    case 'c':
      channel = optarg;
      break;
    case 'x':
      if (optarg != nullptr) password = optarg;
      else std::cin >> password;
      break;
    case 'l':
      ssl = true;
      break;
    default:
      usage();
    }
  }

  auto ptr = kbot::connection_new(address, port, nickname);
  if (ptr == nullptr) {
    std::cerr << "Failed to establish connection to server.\n\n";
    usage();
  }

  std::jthread root(kbot::worker_run, ptr);
  auto r = ptr->LOGIN(nickname);
  if (r < 0) {
    std::cerr << "LOGIN failed\n";
    ptr = nullptr;
    return 0;
  }
  auto id = ptr->join_channel(channel);
  ptr->send_channel(id, "Hello World!");
  ptr->dump_info();
  kbot::supervisor_run();
  log_debug("Shutting down...");

  return 0;
}
