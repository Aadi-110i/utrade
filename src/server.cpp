#include "server.h"
#include <iostream>
#include <sstream>
#include <cstring>

// ── Platform-specific socket headers ───────────────────────────────────
#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    #define CLOSE_SOCKET closesocket
    static constexpr unsigned long long INVALID_SOCK = (unsigned long long)(~0);
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #define CLOSE_SOCKET ::close
    static constexpr int INVALID_SOCK = -1;
#endif

// ════════════════════════════════════════════════════════════════════════
//  Construction / Destruction
// ════════════════════════════════════════════════════════════════════════

Server::Server(KVStore& store, int port)
    : store_(store), port_(port) {}

Server::~Server() {
    stop();
}

void Server::stop() {
    running_ = false;
    for (auto& t : client_threads_) {
        if (t.joinable()) t.join();
    }
    client_threads_.clear();
}

// ════════════════════════════════════════════════════════════════════════
//  STDIN Mode
// ════════════════════════════════════════════════════════════════════════

void Server::runSTDIN() {
    CommandHandler handler(store_);
    std::string line;

    std::cout << "kvstore> ";
    std::cout.flush();

    while (running_ && std::getline(std::cin, line)) {
        // Trim trailing \r (Windows)
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        if (line.empty()) {
            std::cout << "kvstore> ";
            std::cout.flush();
            continue;
        }

        std::string response = handler.execute(line);

        if (response == "__QUIT__") {
            std::cout << "Bye!" << std::endl;
            break;
        }

        if (!response.empty())
            std::cout << response << std::endl;

        std::cout << "kvstore> ";
        std::cout.flush();
    }
}

// ════════════════════════════════════════════════════════════════════════
//  TCP Mode
// ════════════════════════════════════════════════════════════════════════

void Server::runTCP() {
#ifdef _WIN32
    // Initialize Winsock
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "[ERROR] WSAStartup failed: " << WSAGetLastError() << std::endl;
        return;
    }
#endif

    socket_t server_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_sock == INVALID_SOCK) {
        std::cerr << "[ERROR] Could not create socket." << std::endl;
        return;
    }

    // Allow port reuse
    int opt = 1;
#ifdef _WIN32
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&opt), sizeof(opt));
#else
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(static_cast<unsigned short>(port_));

    if (bind(server_sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[ERROR] Bind failed on port " << port_ << std::endl;
        CLOSE_SOCKET(server_sock);
        return;
    }

    if (listen(server_sock, SOMAXCONN) < 0) {
        std::cerr << "[ERROR] Listen failed." << std::endl;
        CLOSE_SOCKET(server_sock);
        return;
    }

    std::cout << "[INFO] KVStore TCP server listening on port " << port_ << std::endl;

    while (running_) {
        struct sockaddr_in client_addr{};
#ifdef _WIN32
        int client_len = sizeof(client_addr);
#else
        socklen_t client_len = sizeof(client_addr);
#endif
        socket_t client_sock = accept(server_sock,
            reinterpret_cast<struct sockaddr*>(&client_addr), &client_len);

        if (client_sock == INVALID_SOCK) {
            if (running_) std::cerr << "[WARN] Accept failed." << std::endl;
            continue;
        }

        char ip_buf[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &client_addr.sin_addr, ip_buf, sizeof(ip_buf));
        std::cout << "[INFO] Client connected from " << ip_buf
                  << ":" << ntohs(client_addr.sin_port) << std::endl;

        client_threads_.emplace_back([this, client_sock]() {
            handleClient(client_sock);
        });
    }

    CLOSE_SOCKET(server_sock);

#ifdef _WIN32
    WSACleanup();
#endif
}

// ════════════════════════════════════════════════════════════════════════
//  Per-client handler (runs in its own thread)
// ════════════════════════════════════════════════════════════════════════

void Server::handleClient(socket_t client_sock) {
    CommandHandler handler(store_);

    // Send function for pub/sub async messages
    auto send_fn = [client_sock](const std::string& msg) {
        std::string out = msg + "\n";
        send(client_sock, out.c_str(), static_cast<int>(out.size()), 0);
    };

    char buf[4096];
    std::string leftover;

    while (running_) {
        int n = recv(client_sock, buf, sizeof(buf) - 1, 0);
        if (n <= 0) break;  // client disconnected or error

        buf[n] = '\0';
        leftover += buf;

        // Process complete lines
        size_t pos;
        while ((pos = leftover.find('\n')) != std::string::npos) {
            std::string line = leftover.substr(0, pos);
            leftover = leftover.substr(pos + 1);

            // Trim \r
            if (!line.empty() && line.back() == '\r')
                line.pop_back();

            if (line.empty()) continue;

            std::string response = handler.execute(line, send_fn);

            if (response == "__QUIT__") {
                handler.clearSubscriptions();
                CLOSE_SOCKET(client_sock);
                return;
            }

            if (!response.empty()) {
                response += "\n";
                send(client_sock, response.c_str(),
                     static_cast<int>(response.size()), 0);
            }
        }
    }

    handler.clearSubscriptions();
    CLOSE_SOCKET(client_sock);
}
