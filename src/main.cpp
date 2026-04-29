#include "kvstore.h"
#include "server.h"

#include <iostream>
#include <string>
#include <cstdlib>
#include <cstring>

static void printUsage(const char* prog) {
    std::cout << "Usage: " << prog << " [OPTIONS]\n\n"
              << "Options:\n"
              << "  --port PORT       TCP port to listen on (default: 7379)\n"
              << "  --stdin           Use stdin/stdout instead of TCP (default)\n"
              << "  --tcp             Run in TCP server mode\n"
              << "  --max-keys N      Max keys before LRU eviction (0 = unlimited)\n"
              << "  --help            Show this help message\n\n"
              << "Examples:\n"
              << "  " << prog << " --stdin\n"
              << "  " << prog << " --tcp --port 7379 --max-keys 10000\n";
}

int main(int argc, char* argv[]) {
    int port        = 7379;
    size_t max_keys = 0;
    bool tcp_mode   = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "--port" && i + 1 < argc) {
            port = std::atoi(argv[++i]);
        } else if (arg == "--max-keys" && i + 1 < argc) {
            max_keys = static_cast<size_t>(std::atoll(argv[++i]));
        } else if (arg == "--tcp") {
            tcp_mode = true;
        } else if (arg == "--stdin") {
            tcp_mode = false;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            printUsage(argv[0]);
            return 1;
        }
    }

    std::cout << "╔══════════════════════════════════════════════╗\n"
              << "║        KVStore – In-Memory Key-Value Store   ║\n"
              << "╠══════════════════════════════════════════════╣\n"
              << "║  Mode      : " << (tcp_mode ? "TCP" : "STDIN") << "                              \n"
              << "║  Port      : " << port << "                             \n"
              << "║  Max Keys  : " << (max_keys == 0 ? "unlimited" : std::to_string(max_keys)) << "                       \n"
              << "╚══════════════════════════════════════════════╝\n"
              << std::endl;

    KVStore store(max_keys);
    Server server(store, port);

    if (tcp_mode) {
        server.runTCP();
    } else {
        server.runSTDIN();
    }

    return 0;
}
