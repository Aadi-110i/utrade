#pragma once

#include "kvstore.h"
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>

/// @brief Parses text-based commands and dispatches them to KVStore.
class CommandHandler {
public:
    /// @param store  Reference to the backing key-value store.
    explicit CommandHandler(KVStore& store);

    /// Execute a single command line and return the response string.
    /// @param line  Raw command text (e.g. "SET foo bar EX 60").
    /// @param send_fn  Optional callback for async output (pub/sub).
    /// @returns  Response string to send back to the client.
    std::string execute(const std::string& line,
                        std::function<void(const std::string&)> send_fn = nullptr);

    /// Check if the handler is in subscription mode (for a client).
    bool isSubscribed() const { return !subscriptions_.empty(); }

    /// Remove all subscriptions for this handler instance.
    void clearSubscriptions();

private:
    KVStore& store_;

    // Active subscription IDs for this client
    std::vector<std::pair<std::string, int>> subscriptions_; // channel -> sub_id

    /// Tokenize a command line, respecting single & double quotes.
    static std::vector<std::string> tokenize(const std::string& input);

    /// Convert a string to uppercase.
    static std::string toUpper(const std::string& s);
};
