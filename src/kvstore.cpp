#include "kvstore.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <cstring>
#include <iostream>
#include <regex>

// ════════════════════════════════════════════════════════════════════════
//  Construction / Destruction
// ════════════════════════════════════════════════════════════════════════

KVStore::KVStore(size_t max_keys) : max_keys_(max_keys) {
    // Periodic cleanup thread – wakes every 1 s to purge expired keys
    cleanup_thread_ = std::thread([this]() { periodicCleanup(); });
}

KVStore::~KVStore() {
    running_ = false;
    cv_.notify_all();
    if (cleanup_thread_.joinable()) cleanup_thread_.join();
}

// ════════════════════════════════════════════════════════════════════════
//  Core Commands
// ════════════════════════════════════════════════════════════════════════

std::string KVStore::set(const std::string& key, const std::string& value,
                         int ttl_seconds) {
    std::lock_guard<std::mutex> lk(mu_);

    auto it = store_.find(key);
    if (it != store_.end()) {
        // Update in place
        Entry& e = it->second;
        e.type        = Type::STRING;
        e.str_value   = value;
        e.list_value.clear();
        e.has_expiry  = (ttl_seconds > 0);
        if (e.has_expiry)
            e.expiry = std::chrono::steady_clock::now()
                     + std::chrono::seconds(ttl_seconds);
        if (e.has_expiry)
            expiry_pq_.push({key, e.expiry});
        // Touch LRU
        lru_order_.erase(e.lru_it);
        lru_order_.push_front(key);
        e.lru_it = lru_order_.begin();
    } else {
        // Evict if capacity reached
        if (max_keys_ > 0 && store_.size() >= max_keys_)
            evictLRU();

        Entry e;
        e.type       = Type::STRING;
        e.str_value  = value;
        e.has_expiry = (ttl_seconds > 0);
        if (e.has_expiry) {
            e.expiry = std::chrono::steady_clock::now()
                     + std::chrono::seconds(ttl_seconds);
            expiry_pq_.push({key, e.expiry});
        }
        lru_order_.push_front(key);
        e.lru_it = lru_order_.begin();
        store_.emplace(key, std::move(e));
    }
    return "OK";
}

std::string KVStore::get(const std::string& key) {
    std::lock_guard<std::mutex> lk(mu_);

    auto it = store_.find(key);
    if (it == store_.end()) return "(nil)";

    if (isExpired(it->second)) {
        lru_order_.erase(it->second.lru_it);
        store_.erase(it);
        ++expired_cleaned_;
        return "(nil)";
    }

    Entry& e = it->second;
    if (e.type != Type::STRING)
        return "(error) WRONGTYPE Operation against a key holding the wrong kind of value";

    touchLRU(key, e);
    return e.str_value;
}

std::string KVStore::del(const std::string& key) {
    std::lock_guard<std::mutex> lk(mu_);

    auto it = store_.find(key);
    if (it == store_.end()) return "(integer) 0";

    lru_order_.erase(it->second.lru_it);
    store_.erase(it);
    return "(integer) 1";
}

std::string KVStore::keys(const std::string& pattern) {
    std::lock_guard<std::mutex> lk(mu_);

    std::vector<std::string> matched;
    for (auto it = store_.begin(); it != store_.end(); ) {
        if (isExpired(it->second)) {
            lru_order_.erase(it->second.lru_it);
            it = store_.erase(it);
            ++expired_cleaned_;
            continue;
        }
        if (globMatch(pattern, it->first))
            matched.push_back(it->first);
        ++it;
    }

    if (matched.empty()) return "(empty list or set)";

    std::sort(matched.begin(), matched.end());
    std::string result;
    for (size_t i = 0; i < matched.size(); ++i) {
        if (i > 0) result += '\n';
        result += matched[i];
    }
    return result;
}

std::string KVStore::ttl(const std::string& key) {
    std::lock_guard<std::mutex> lk(mu_);

    auto it = store_.find(key);
    if (it == store_.end()) return "-2";

    if (isExpired(it->second)) {
        lru_order_.erase(it->second.lru_it);
        store_.erase(it);
        ++expired_cleaned_;
        return "-2";
    }

    if (!it->second.has_expiry) return "-1";

    auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
        it->second.expiry - std::chrono::steady_clock::now()).count();
    return std::to_string(remaining > 0 ? remaining : 0);
}

// ════════════════════════════════════════════════════════════════════════
//  Persistence
// ════════════════════════════════════════════════════════════════════════

std::string KVStore::save(const std::string& filename) {
    std::lock_guard<std::mutex> lk(mu_);

    std::ofstream ofs(filename);
    if (!ofs) return "(error) Could not open file for writing";

    ofs << "[\n";
    bool first = true;
    auto now = std::chrono::steady_clock::now();

    for (auto it = store_.begin(); it != store_.end(); ++it) {
        if (isExpired(it->second)) continue;   // skip expired

        if (!first) ofs << ",\n";
        first = false;

        ofs << "  {";
        ofs << "\"key\":\"" << escapeJson(it->first) << "\",";
        ofs << "\"type\":\"" << (it->second.type == Type::STRING ? "string" : "list") << "\",";

        if (it->second.type == Type::STRING) {
            ofs << "\"value\":\"" << escapeJson(it->second.str_value) << "\",";
        } else {
            ofs << "\"value\":[";
            for (size_t i = 0; i < it->second.list_value.size(); ++i) {
                if (i > 0) ofs << ",";
                ofs << "\"" << escapeJson(it->second.list_value[i]) << "\"";
            }
            ofs << "],";
        }

        if (it->second.has_expiry) {
            auto rem = std::chrono::duration_cast<std::chrono::seconds>(
                it->second.expiry - now).count();
            ofs << "\"ttl\":" << (rem > 0 ? rem : 0);
        } else {
            ofs << "\"ttl\":-1";
        }
        ofs << "}";
    }
    ofs << "\n]\n";
    ofs.close();
    return "OK";
}

std::string KVStore::load(const std::string& filename) {
    std::lock_guard<std::mutex> lk(mu_);

    std::ifstream ifs(filename);
    if (!ifs) return "(error) Could not open file for reading";

    // Read entire file
    std::string content((std::istreambuf_iterator<char>(ifs)),
                         std::istreambuf_iterator<char>());
    ifs.close();

    // Clear current store
    store_.clear();
    lru_order_.clear();
    while (!expiry_pq_.empty()) expiry_pq_.pop();

    // Simple JSON array parser for our known schema
    // Each object: {"key":"...","type":"string|list","value":"..."|[...],"ttl":N}
    size_t pos = 0;
    auto skipWs = [&]() { while (pos < content.size() && isspace(content[pos])) ++pos; };
    auto expect = [&](char c) { skipWs(); if (pos < content.size() && content[pos] == c) ++pos; };

    auto readString = [&]() -> std::string {
        skipWs();
        if (pos >= content.size() || content[pos] != '"') return "";
        ++pos; // skip opening "
        std::string result;
        while (pos < content.size() && content[pos] != '"') {
            if (content[pos] == '\\' && pos + 1 < content.size()) {
                ++pos;
                switch (content[pos]) {
                    case '"':  result += '"';  break;
                    case '\\': result += '\\'; break;
                    case 'n':  result += '\n'; break;
                    case 't':  result += '\t'; break;
                    case '/':  result += '/';  break;
                    default:   result += content[pos]; break;
                }
            } else {
                result += content[pos];
            }
            ++pos;
        }
        if (pos < content.size()) ++pos; // skip closing "
        return result;
    };

    auto readNumber = [&]() -> long long {
        skipWs();
        std::string num;
        if (pos < content.size() && content[pos] == '-') { num += '-'; ++pos; }
        while (pos < content.size() && isdigit(content[pos])) { num += content[pos]; ++pos; }
        return num.empty() ? 0 : std::stoll(num);
    };

    expect('[');
    skipWs();

    while (pos < content.size() && content[pos] != ']') {
        expect('{');
        std::string key, type, str_val;
        std::deque<std::string> list_val;
        long long ttl_val = -1;

        // Read key-value pairs in the JSON object
        for (int field = 0; field < 4; ++field) {
            skipWs();
            if (pos < content.size() && content[pos] == '}') break;
            if (field > 0) expect(',');

            std::string fname = readString();
            expect(':');

            if (fname == "key") {
                key = readString();
            } else if (fname == "type") {
                type = readString();
            } else if (fname == "value") {
                skipWs();
                if (pos < content.size() && content[pos] == '[') {
                    // Array
                    expect('[');
                    skipWs();
                    while (pos < content.size() && content[pos] != ']') {
                        if (!list_val.empty()) expect(',');
                        list_val.push_back(readString());
                        skipWs();
                    }
                    expect(']');
                } else {
                    str_val = readString();
                }
            } else if (fname == "ttl") {
                ttl_val = readNumber();
            }
        }
        expect('}');
        skipWs();
        if (pos < content.size() && content[pos] == ',') ++pos;

        // Reconstruct entry
        if (!key.empty()) {
            Entry e;
            e.type = (type == "list") ? Type::LIST : Type::STRING;
            e.str_value = str_val;
            e.list_value = list_val;
            if (ttl_val > 0) {
                e.has_expiry = true;
                e.expiry = std::chrono::steady_clock::now()
                         + std::chrono::seconds(ttl_val);
                expiry_pq_.push({key, e.expiry});
            }
            lru_order_.push_front(key);
            e.lru_it = lru_order_.begin();
            store_.emplace(key, std::move(e));
        }
    }

    return "OK";
}

// ════════════════════════════════════════════════════════════════════════
//  Stats
// ════════════════════════════════════════════════════════════════════════

std::string KVStore::stats() {
    std::lock_guard<std::mutex> lk(mu_);

    std::ostringstream os;
    os << "---- Store Statistics ----\n";
    os << "total_keys        : " << store_.size() << "\n";
    os << "expired_cleaned   : " << expired_cleaned_.load() << "\n";
    os << "max_keys_limit    : " << (max_keys_ == 0 ? std::string("unlimited") : std::to_string(max_keys_)) << "\n";
    os << "memory_estimate   : " << estimateMemoryBytes() << " bytes\n";
    os << "--------------------------";
    return os.str();
}

// ════════════════════════════════════════════════════════════════════════
//  Bonus – Integer Operations
// ════════════════════════════════════════════════════════════════════════

std::string KVStore::incr(const std::string& key) {
    std::lock_guard<std::mutex> lk(mu_);

    auto it = store_.find(key);
    if (it == store_.end()) {
        // Auto-create with value "0", then increment
        if (max_keys_ > 0 && store_.size() >= max_keys_) evictLRU();
        Entry e;
        e.type = Type::STRING;
        e.str_value = "1";
        lru_order_.push_front(key);
        e.lru_it = lru_order_.begin();
        store_.emplace(key, std::move(e));
        return "(integer) 1";
    }

    if (isExpired(it->second)) {
        lru_order_.erase(it->second.lru_it);
        store_.erase(it);
        ++expired_cleaned_;
        // Create fresh
        if (max_keys_ > 0 && store_.size() >= max_keys_) evictLRU();
        Entry e;
        e.type = Type::STRING;
        e.str_value = "1";
        lru_order_.push_front(key);
        e.lru_it = lru_order_.begin();
        store_.emplace(key, std::move(e));
        return "(integer) 1";
    }

    Entry& e = it->second;
    if (e.type != Type::STRING)
        return "(error) WRONGTYPE Operation against a key holding the wrong kind of value";

    try {
        long long val = std::stoll(e.str_value);
        ++val;
        e.str_value = std::to_string(val);
        touchLRU(key, e);
        return "(integer) " + e.str_value;
    } catch (...) {
        return "(error) value is not an integer or out of range";
    }
}

std::string KVStore::decr(const std::string& key) {
    std::lock_guard<std::mutex> lk(mu_);

    auto it = store_.find(key);
    if (it == store_.end()) {
        if (max_keys_ > 0 && store_.size() >= max_keys_) evictLRU();
        Entry e;
        e.type = Type::STRING;
        e.str_value = "-1";
        lru_order_.push_front(key);
        e.lru_it = lru_order_.begin();
        store_.emplace(key, std::move(e));
        return "(integer) -1";
    }

    if (isExpired(it->second)) {
        lru_order_.erase(it->second.lru_it);
        store_.erase(it);
        ++expired_cleaned_;
        if (max_keys_ > 0 && store_.size() >= max_keys_) evictLRU();
        Entry e;
        e.type = Type::STRING;
        e.str_value = "-1";
        lru_order_.push_front(key);
        e.lru_it = lru_order_.begin();
        store_.emplace(key, std::move(e));
        return "(integer) -1";
    }

    Entry& e = it->second;
    if (e.type != Type::STRING)
        return "(error) WRONGTYPE Operation against a key holding the wrong kind of value";

    try {
        long long val = std::stoll(e.str_value);
        --val;
        e.str_value = std::to_string(val);
        touchLRU(key, e);
        return "(integer) " + e.str_value;
    } catch (...) {
        return "(error) value is not an integer or out of range";
    }
}

// ════════════════════════════════════════════════════════════════════════
//  Bonus – List Operations
// ════════════════════════════════════════════════════════════════════════

std::string KVStore::lpush(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lk(mu_);

    auto it = store_.find(key);
    if (it != store_.end() && isExpired(it->second)) {
        lru_order_.erase(it->second.lru_it);
        store_.erase(it);
        ++expired_cleaned_;
        it = store_.end();
    }

    if (it == store_.end()) {
        if (max_keys_ > 0 && store_.size() >= max_keys_) evictLRU();
        Entry e;
        e.type = Type::LIST;
        e.list_value.push_front(value);
        lru_order_.push_front(key);
        e.lru_it = lru_order_.begin();
        store_.emplace(key, std::move(e));
        return "(integer) 1";
    }

    Entry& e = it->second;
    if (e.type != Type::LIST)
        return "(error) WRONGTYPE Operation against a key holding the wrong kind of value";

    e.list_value.push_front(value);
    touchLRU(key, e);
    return "(integer) " + std::to_string(e.list_value.size());
}

std::string KVStore::rpush(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lk(mu_);

    auto it = store_.find(key);
    if (it != store_.end() && isExpired(it->second)) {
        lru_order_.erase(it->second.lru_it);
        store_.erase(it);
        ++expired_cleaned_;
        it = store_.end();
    }

    if (it == store_.end()) {
        if (max_keys_ > 0 && store_.size() >= max_keys_) evictLRU();
        Entry e;
        e.type = Type::LIST;
        e.list_value.push_back(value);
        lru_order_.push_front(key);
        e.lru_it = lru_order_.begin();
        store_.emplace(key, std::move(e));
        return "(integer) 1";
    }

    Entry& e = it->second;
    if (e.type != Type::LIST)
        return "(error) WRONGTYPE Operation against a key holding the wrong kind of value";

    e.list_value.push_back(value);
    touchLRU(key, e);
    return "(integer) " + std::to_string(e.list_value.size());
}

std::string KVStore::lpop(const std::string& key) {
    std::lock_guard<std::mutex> lk(mu_);

    auto it = store_.find(key);
    if (it == store_.end()) return "(nil)";

    if (isExpired(it->second)) {
        lru_order_.erase(it->second.lru_it);
        store_.erase(it);
        ++expired_cleaned_;
        return "(nil)";
    }

    Entry& e = it->second;
    if (e.type != Type::LIST)
        return "(error) WRONGTYPE Operation against a key holding the wrong kind of value";

    if (e.list_value.empty()) return "(nil)";

    std::string val = e.list_value.front();
    e.list_value.pop_front();
    touchLRU(key, e);

    // Auto-delete empty lists
    if (e.list_value.empty()) {
        lru_order_.erase(e.lru_it);
        store_.erase(it);
    }

    return val;
}

std::string KVStore::rpop(const std::string& key) {
    std::lock_guard<std::mutex> lk(mu_);

    auto it = store_.find(key);
    if (it == store_.end()) return "(nil)";

    if (isExpired(it->second)) {
        lru_order_.erase(it->second.lru_it);
        store_.erase(it);
        ++expired_cleaned_;
        return "(nil)";
    }

    Entry& e = it->second;
    if (e.type != Type::LIST)
        return "(error) WRONGTYPE Operation against a key holding the wrong kind of value";

    if (e.list_value.empty()) return "(nil)";

    std::string val = e.list_value.back();
    e.list_value.pop_back();
    touchLRU(key, e);

    if (e.list_value.empty()) {
        lru_order_.erase(e.lru_it);
        store_.erase(it);
    }

    return val;
}

std::string KVStore::lrange(const std::string& key, int start, int stop) {
    std::lock_guard<std::mutex> lk(mu_);

    auto it = store_.find(key);
    if (it == store_.end()) return "(empty list or set)";

    if (isExpired(it->second)) {
        lru_order_.erase(it->second.lru_it);
        store_.erase(it);
        ++expired_cleaned_;
        return "(empty list or set)";
    }

    Entry& e = it->second;
    if (e.type != Type::LIST)
        return "(error) WRONGTYPE Operation against a key holding the wrong kind of value";

    int sz = static_cast<int>(e.list_value.size());
    if (start < 0) start = std::max(0, sz + start);
    if (stop  < 0) stop  = sz + stop;
    stop = std::min(stop, sz - 1);

    if (start > stop || start >= sz) return "(empty list or set)";

    touchLRU(key, e);

    std::string result;
    for (int i = start; i <= stop; ++i) {
        if (i > start) result += '\n';
        result += std::to_string(i - start + 1) + ") " + e.list_value[i];
    }
    return result;
}

// ════════════════════════════════════════════════════════════════════════
//  Bonus – Pub/Sub
// ════════════════════════════════════════════════════════════════════════

int KVStore::subscribe(const std::string& channel, MessageCallback cb) {
    std::lock_guard<std::mutex> lk(pubsub_mu_);
    int id = next_sub_id_++;
    subscribers_.push_back({id, channel, std::move(cb)});
    return id;
}

void KVStore::unsubscribe(int subscriber_id) {
    std::lock_guard<std::mutex> lk(pubsub_mu_);
    subscribers_.erase(
        std::remove_if(subscribers_.begin(), subscribers_.end(),
                       [subscriber_id](const Subscriber& s) { return s.id == subscriber_id; }),
        subscribers_.end());
}

std::string KVStore::publish(const std::string& channel, const std::string& message) {
    std::lock_guard<std::mutex> lk(pubsub_mu_);
    int delivered = 0;
    for (auto& sub : subscribers_) {
        if (sub.channel == channel) {
            sub.callback(channel, message);
            ++delivered;
        }
    }
    return "(integer) " + std::to_string(delivered);
}

// ════════════════════════════════════════════════════════════════════════
//  Private Helpers
// ════════════════════════════════════════════════════════════════════════

bool KVStore::isExpired(const Entry& e) const {
    if (!e.has_expiry) return false;
    return std::chrono::steady_clock::now() >= e.expiry;
}

bool KVStore::lazyExpire(const std::string& key) {
    auto it = store_.find(key);
    if (it == store_.end()) return false;
    if (!isExpired(it->second)) return false;
    lru_order_.erase(it->second.lru_it);
    store_.erase(it);
    ++expired_cleaned_;
    return true;
}

void KVStore::periodicCleanup() {
    while (running_) {
        {
            std::unique_lock<std::mutex> lk(cv_mu_);
            cv_.wait_for(lk, std::chrono::seconds(1), [this]() { return !running_.load(); });
        }
        if (!running_) break;

        std::lock_guard<std::mutex> lk(mu_);
        auto now = std::chrono::steady_clock::now();

        // Drain the min-heap of expired entries
        while (!expiry_pq_.empty() && expiry_pq_.top().expiry <= now) {
            auto item = expiry_pq_.top();
            expiry_pq_.pop();
            auto it = store_.find(item.key);
            if (it == store_.end()) continue;
            // Verify the expiry time matches (key may have been re-SET)
            if (it->second.has_expiry && it->second.expiry <= now) {
                lru_order_.erase(it->second.lru_it);
                store_.erase(it);
                ++expired_cleaned_;
            }
        }
    }
}

void KVStore::evictLRU() {
    // mu_ must already be held by caller
    if (lru_order_.empty()) return;
    const std::string& victim = lru_order_.back();
    store_.erase(victim);
    lru_order_.pop_back();
}

void KVStore::touchLRU(const std::string& key, Entry& e) {
    lru_order_.erase(e.lru_it);
    lru_order_.push_front(key);
    e.lru_it = lru_order_.begin();
}

bool KVStore::globMatch(const std::string& pattern, const std::string& str) const {
    size_t pi = 0, si = 0;
    size_t starIdx = std::string::npos, matchIdx = 0;

    while (si < str.size()) {
        if (pi < pattern.size() && (pattern[pi] == str[si] || pattern[pi] == '?')) {
            ++pi; ++si;
        } else if (pi < pattern.size() && pattern[pi] == '*') {
            starIdx = pi++;
            matchIdx = si;
        } else if (starIdx != std::string::npos) {
            pi = starIdx + 1;
            si = ++matchIdx;
        } else {
            return false;
        }
    }
    while (pi < pattern.size() && pattern[pi] == '*') ++pi;
    return pi == pattern.size();
}

size_t KVStore::estimateMemoryBytes() const {
    size_t total = sizeof(*this);
    total += store_.bucket_count() * sizeof(void*);
    for (auto& [k, v] : store_) {
        total += k.capacity() + sizeof(Entry);
        total += v.str_value.capacity();
        for (auto& s : v.list_value) total += s.capacity() + sizeof(std::string);
    }
    return total;
}

std::string KVStore::escapeJson(const std::string& s) const {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;      break;
        }
    }
    return out;
}
