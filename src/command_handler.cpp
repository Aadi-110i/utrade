#include "command_handler.h"
#include <algorithm>
#include <cctype>
#include <sstream>

// ════════════════════════════════════════════════════════════════════════
//  Construction
// ════════════════════════════════════════════════════════════════════════

CommandHandler::CommandHandler(KVStore& store) : store_(store) {}

// ════════════════════════════════════════════════════════════════════════
//  Command Execution
// ════════════════════════════════════════════════════════════════════════

std::string CommandHandler::execute(const std::string& line,
                                    std::function<void(const std::string&)> send_fn) {
    auto tokens = tokenize(line);
    if (tokens.empty()) return "";

    std::string cmd = toUpper(tokens[0]);

    // ── Core commands ──────────────────────────────────────────────────

    if (cmd == "SET") {
        if (tokens.size() < 3)
            return "(error) wrong number of arguments for 'SET' command";
        int ttl = -1;
        // Check for EX option
        for (size_t i = 3; i < tokens.size(); ++i) {
            if (toUpper(tokens[i]) == "EX" && i + 1 < tokens.size()) {
                try { ttl = std::stoi(tokens[i + 1]); } catch (...) {
                    return "(error) value is not an integer or out of range";
                }
                break;
            }
        }
        return store_.set(tokens[1], tokens[2], ttl);
    }

    if (cmd == "GET") {
        if (tokens.size() < 2)
            return "(error) wrong number of arguments for 'GET' command";
        return store_.get(tokens[1]);
    }

    if (cmd == "DEL") {
        if (tokens.size() < 2)
            return "(error) wrong number of arguments for 'DEL' command";
        return store_.del(tokens[1]);
    }

    if (cmd == "KEYS") {
        if (tokens.size() < 2)
            return "(error) wrong number of arguments for 'KEYS' command";
        return store_.keys(tokens[1]);
    }

    if (cmd == "TTL") {
        if (tokens.size() < 2)
            return "(error) wrong number of arguments for 'TTL' command";
        return store_.ttl(tokens[1]);
    }

    // ── Persistence ────────────────────────────────────────────────────

    if (cmd == "SAVE") {
        std::string fname = (tokens.size() >= 2) ? tokens[1] : "dump.json";
        return store_.save(fname);
    }

    if (cmd == "LOAD") {
        std::string fname = (tokens.size() >= 2) ? tokens[1] : "dump.json";
        return store_.load(fname);
    }

    // ── Stats ──────────────────────────────────────────────────────────

    if (cmd == "STATS") {
        return store_.stats();
    }

    // ── Extended commands ──────────────────────────────────────────────

    if (cmd == "EXISTS") {
        if (tokens.size() < 2)
            return "(error) wrong number of arguments for 'EXISTS' command";
        return store_.exists(tokens[1]);
    }

    if (cmd == "TYPE") {
        if (tokens.size() < 2)
            return "(error) wrong number of arguments for 'TYPE' command";
        return store_.type(tokens[1]);
    }

    if (cmd == "RENAME") {
        if (tokens.size() < 3)
            return "(error) wrong number of arguments for 'RENAME' command";
        return store_.rename(tokens[1], tokens[2]);
    }

    if (cmd == "EXPIRE") {
        if (tokens.size() < 3)
            return "(error) wrong number of arguments for 'EXPIRE' command";
        try {
            int sec = std::stoi(tokens[2]);
            return store_.expire(tokens[1], sec);
        } catch (...) {
            return "(error) value is not an integer or out of range";
        }
    }

    if (cmd == "PERSIST") {
        if (tokens.size() < 2)
            return "(error) wrong number of arguments for 'PERSIST' command";
        return store_.persist(tokens[1]);
    }

    if (cmd == "APPEND") {
        if (tokens.size() < 3)
            return "(error) wrong number of arguments for 'APPEND' command";
        return store_.append(tokens[1], tokens[2]);
    }

    if (cmd == "STRLEN") {
        if (tokens.size() < 2)
            return "(error) wrong number of arguments for 'STRLEN' command";
        return store_.strlen(tokens[1]);
    }

    if (cmd == "DBSIZE") {
        return store_.dbsize();
    }

    if (cmd == "FLUSHDB") {
        return store_.flushdb();
    }

    // ── Integer operations ─────────────────────────────────────────────

    if (cmd == "INCR") {
        if (tokens.size() < 2)
            return "(error) wrong number of arguments for 'INCR' command";
        return store_.incr(tokens[1]);
    }

    if (cmd == "DECR") {
        if (tokens.size() < 2)
            return "(error) wrong number of arguments for 'DECR' command";
        return store_.decr(tokens[1]);
    }

    if (cmd == "INCRBY") {
        if (tokens.size() < 3)
            return "(error) wrong number of arguments for 'INCRBY' command";
        try {
            long long delta = std::stoll(tokens[2]);
            return store_.incrby(tokens[1], delta);
        } catch (...) {
            return "(error) value is not an integer or out of range";
        }
    }

    if (cmd == "DECRBY") {
        if (tokens.size() < 3)
            return "(error) wrong number of arguments for 'DECRBY' command";
        try {
            long long delta = std::stoll(tokens[2]);
            return store_.incrby(tokens[1], -delta);
        } catch (...) {
            return "(error) value is not an integer or out of range";
        }
    }

    // ── List operations ────────────────────────────────────────────────

    if (cmd == "LPUSH") {
        if (tokens.size() < 3)
            return "(error) wrong number of arguments for 'LPUSH' command";
        return store_.lpush(tokens[1], tokens[2]);
    }

    if (cmd == "RPUSH") {
        if (tokens.size() < 3)
            return "(error) wrong number of arguments for 'RPUSH' command";
        return store_.rpush(tokens[1], tokens[2]);
    }

    if (cmd == "LPOP") {
        if (tokens.size() < 2)
            return "(error) wrong number of arguments for 'LPOP' command";
        return store_.lpop(tokens[1]);
    }

    if (cmd == "RPOP") {
        if (tokens.size() < 2)
            return "(error) wrong number of arguments for 'RPOP' command";
        return store_.rpop(tokens[1]);
    }

    if (cmd == "LRANGE") {
        if (tokens.size() < 4)
            return "(error) wrong number of arguments for 'LRANGE' command";
        try {
            int start = std::stoi(tokens[2]);
            int stop  = std::stoi(tokens[3]);
            return store_.lrange(tokens[1], start, stop);
        } catch (...) {
            return "(error) value is not an integer or out of range";
        }
    }

    if (cmd == "LLEN") {
        if (tokens.size() < 2)
            return "(error) wrong number of arguments for 'LLEN' command";
        return store_.llen(tokens[1]);
    }

    // ── Pub/Sub ────────────────────────────────────────────────────────

    if (cmd == "SUBSCRIBE") {
        if (tokens.size() < 2)
            return "(error) wrong number of arguments for 'SUBSCRIBE' command";
        if (!send_fn)
            return "(error) SUBSCRIBE is only supported in TCP mode";

        const std::string& channel = tokens[1];
        auto cb = [send_fn, channel](const std::string& ch, const std::string& msg) {
            send_fn("message " + ch + " " + msg);
        };
        int id = store_.subscribe(channel, cb);
        subscriptions_.emplace_back(channel, id);
        return "Subscribed to channel: " + channel;
    }

    if (cmd == "UNSUBSCRIBE") {
        if (tokens.size() < 2) {
            clearSubscriptions();
            return "Unsubscribed from all channels";
        }
        const std::string& channel = tokens[1];
        auto it = std::remove_if(subscriptions_.begin(), subscriptions_.end(),
            [&](const std::pair<std::string, int>& p) {
                if (p.first == channel) { store_.unsubscribe(p.second); return true; }
                return false;
            });
        subscriptions_.erase(it, subscriptions_.end());
        return "Unsubscribed from channel: " + channel;
    }

    if (cmd == "PUBLISH") {
        if (tokens.size() < 3)
            return "(error) wrong number of arguments for 'PUBLISH' command";
        // Join all tokens after channel as the message
        std::string msg;
        for (size_t i = 2; i < tokens.size(); ++i) {
            if (i > 2) msg += ' ';
            msg += tokens[i];
        }
        return store_.publish(tokens[1], msg);
    }

    // ── Misc ───────────────────────────────────────────────────────────

    if (cmd == "PING") {
        return "PONG";
    }

    if (cmd == "QUIT" || cmd == "EXIT") {
        clearSubscriptions();
        return "__QUIT__";
    }

    return "(error) unknown command '" + tokens[0] + "'";
}

void CommandHandler::clearSubscriptions() {
    for (auto& [ch, id] : subscriptions_)
        store_.unsubscribe(id);
    subscriptions_.clear();
}

// ════════════════════════════════════════════════════════════════════════
//  Tokenizer – supports single & double quotes
// ════════════════════════════════════════════════════════════════════════

std::vector<std::string> CommandHandler::tokenize(const std::string& input) {
    std::vector<std::string> tokens;
    size_t i = 0;

    while (i < input.size()) {
        // Skip whitespace
        while (i < input.size() && std::isspace(static_cast<unsigned char>(input[i]))) ++i;
        if (i >= input.size()) break;

        std::string token;

        if (input[i] == '\'' || input[i] == '"') {
            // Quoted token
            char quote = input[i++];
            while (i < input.size() && input[i] != quote) {
                if (input[i] == '\\' && i + 1 < input.size()) {
                    ++i;
                    switch (input[i]) {
                        case 'n':  token += '\n'; break;
                        case 't':  token += '\t'; break;
                        case '\\': token += '\\'; break;
                        default:   token += input[i]; break;
                    }
                } else {
                    token += input[i];
                }
                ++i;
            }
            if (i < input.size()) ++i; // skip closing quote
        } else {
            // Unquoted token
            while (i < input.size() && !std::isspace(static_cast<unsigned char>(input[i]))) {
                token += input[i++];
            }
        }

        if (!token.empty()) tokens.push_back(std::move(token));
    }

    return tokens;
}

std::string CommandHandler::toUpper(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return result;
}
