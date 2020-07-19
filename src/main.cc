#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

#include "loop.hh"
#include "server.hh"

#define KBOT_VERSION "0.1"

[[noreturn]] void usage(void)
{
  std::cerr << "Usage:   kbot <server> <port> <channel> <nickname>\n";
  std::cerr << "         kbot -s <server> -p <port> -c <channel> -n <nickname>\n";
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
  const char *nickname = "nullptr";
  const char *channel = "##kbot";

  if (argc > 1)
    address = argv[1];
  if (argc > 2) {
    int r;
    try {
      r = std::stoi(argv[2]);
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
  }
  if (argc > 3)
    channel = argv[3];
  if (argc > 4)
    nickname = argv[4];

  auto ptr = kbot::connection_new(address, port, nickname);
  if (ptr == nullptr) {
    std::cerr << "Failed to establish connection to server.\n\n";
    usage();
  }

  std::jthread root(kbot::worker_run, ptr);
  auto id = ptr->join_channel(channel);
  ptr->send_channel(id, "Hello World!");
  kbot::supervisor_run();
  ptr->part_channel(id);
  std::clog << "Shutting down...\n";

  return 0;
}
