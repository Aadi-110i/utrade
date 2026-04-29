#pragma once

#include "command_handler.h"
#include <string>
#include <atomic>
#include <thread>
#include <vector>

/// @brief TCP + stdin server that accepts text-based commands.
class Server {
public:
    /// @param handler  Shared command handler (each TCP client gets its own copy).
    /// @param store    Shared store reference.
    /// @param port     TCP port to listen on.
    Server(KVStore& store, int port = 7379);
    ~Server();

    /// Run the server in stdin mode (blocking, reads from console).
    void runSTDIN();

    /// Run the server in TCP mode (blocking, spawns threads per client).
    void runTCP();

    /// Stop the server.
    void stop();

private:
    KVStore& store_;
    int port_;
    std::atomic<bool> running_{true};
    std::vector<std::thread> client_threads_;

#ifdef _WIN32
    using socket_t = unsigned long long;  // SOCKET
#else
    using socket_t = int;
#endif

    void handleClient(socket_t client_sock);
};
