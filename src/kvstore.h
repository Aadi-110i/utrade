#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <deque>
#include <list>
#include <vector>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <queue>
#include <functional>
#include <condition_variable>

/// @brief Thread-safe in-memory key-value store with TTL, LRU eviction,
///        list operations, pub/sub support, and persistence.
///
/// Design rationale (for trading/caching workloads):
///   - std::unordered_map for O(1) amortized lookup
///   - Min-heap (priority_queue) for efficient TTL expiry scanning
///   - Doubly-linked list for O(1) LRU move-to-front / evict-from-back
///   - std::deque for O(1) push/pop at both ends (list data type)
///   - Dual expiration: lazy (on access) + periodic (background thread every 1s)
class KVStore {
public:
    /// @param max_keys  Maximum number of keys before LRU eviction (0 = unlimited).
    explicit KVStore(size_t max_keys = 0);
    ~KVStore();

    // Non-copyable, non-movable (owns background thread)
    KVStore(const KVStore&) = delete;
    KVStore& operator=(const KVStore&) = delete;

    // ── Core commands ──────────────────────────────────────────────────
    std::string set(const std::string& key, const std::string& value, int ttl_seconds = -1);
    std::string get(const std::string& key);
    std::string del(const std::string& key);
    std::string keys(const std::string& pattern);
    std::string ttl(const std::string& key);

    // ── Extended commands ──────────────────────────────────────────────
    std::string exists(const std::string& key);
    std::string type(const std::string& key);
    std::string rename(const std::string& key, const std::string& newkey);
    std::string expire(const std::string& key, int seconds);
    std::string persist(const std::string& key);
    std::string append(const std::string& key, const std::string& value);
    std::string strlen(const std::string& key);
    std::string flushdb();
    std::string dbsize();

    // ── Persistence ────────────────────────────────────────────────────
    std::string save(const std::string& filename = "dump.json");
    std::string load(const std::string& filename = "dump.json");

    // ── Stats ──────────────────────────────────────────────────────────
    std::string stats();

    // ── Bonus: Integer operations ──────────────────────────────────────
    std::string incr(const std::string& key);
    std::string decr(const std::string& key);
    std::string incrby(const std::string& key, long long delta);

    // ── Bonus: List operations ─────────────────────────────────────────
    std::string lpush(const std::string& key, const std::string& value);
    std::string rpush(const std::string& key, const std::string& value);
    std::string lpop(const std::string& key);
    std::string rpop(const std::string& key);
    std::string lrange(const std::string& key, int start, int stop);
    std::string llen(const std::string& key);

    // ── Bonus: Pub/Sub ─────────────────────────────────────────────────
    using MessageCallback = std::function<void(const std::string& channel, const std::string& message)>;
    int  subscribe(const std::string& channel, MessageCallback cb);
    void unsubscribe(int subscriber_id);
    std::string publish(const std::string& channel, const std::string& message);

    // ── For testing ────────────────────────────────────────────────────
    size_t keyCount() const;

private:
    // ── Value types ────────────────────────────────────────────────────
    enum class Type { STRING, LIST };

    struct Entry {
        Type type = Type::STRING;
        std::string str_value;
        std::deque<std::string> list_value;

        bool has_expiry = false;
        std::chrono::steady_clock::time_point expiry;

        // LRU doubly-linked-list iterator (O(1) erase + re-insert)
        std::list<std::string>::iterator lru_it;
    };

    struct ExpiryItem {
        std::string key;
        std::chrono::steady_clock::time_point expiry;
        bool operator>(const ExpiryItem& o) const { return expiry > o.expiry; }
    };

    // ── Data members ───────────────────────────────────────────────────
    std::unordered_map<std::string, Entry> store_;
    std::list<std::string> lru_order_;                     // front = most recent
    std::priority_queue<ExpiryItem, std::vector<ExpiryItem>,
                        std::greater<ExpiryItem>> expiry_pq_;

    mutable std::mutex mu_;

    size_t max_keys_;
    std::atomic<size_t> expired_cleaned_{0};

    // Background cleanup (periodic expiration sweep every 1 second)
    std::thread cleanup_thread_;
    std::atomic<bool> running_{true};
    std::condition_variable cv_;
    std::mutex cv_mu_;

    // Pub/Sub (separate lock to avoid blocking data path)
    struct Subscriber {
        int id;
        std::string channel;
        MessageCallback callback;
    };
    std::mutex pubsub_mu_;
    std::vector<Subscriber> subscribers_;
    std::atomic<int> next_sub_id_{1};

    // ── Helpers ────────────────────────────────────────────────────────
    bool isExpired(const Entry& e) const;
    bool lazyExpire(const std::string& key);        // returns true if expired & removed
    void periodicCleanup();
    void evictLRU();
    void touchLRU(const std::string& key, Entry& e);
    bool globMatch(const std::string& pattern, const std::string& str) const;
    size_t estimateMemoryBytes() const;
    std::string escapeJson(const std::string& s) const;
};
