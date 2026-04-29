/// @file test_kvstore.cpp
/// @brief Comprehensive test suite for KVStore – zero external dependencies.
///
/// Tests cover: core ops, TTL expiration (lazy + periodic), persistence,
///              LRU eviction, integer ops, list ops, extended commands,
///              glob matching, edge cases, and concurrent stress testing.

#include "../src/kvstore.h"

#include <cassert>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <chrono>
#include <atomic>
#include <cstdio>
#include <sstream>
#include <functional>

// ── Minimal test framework ─────────────────────────────────────────────

static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT_EQ(actual, expected)                                          \
    do {                                                                     \
        auto _a = (actual); auto _e = (expected);                            \
        if (_a != _e) {                                                      \
            std::cerr << "  ✗ FAIL  " << __FILE__ << ":" << __LINE__         \
                      << "\n    expected: " << _e                            \
                      << "\n    actual:   " << _a << "\n";                   \
            ++tests_failed; return;                                          \
        }                                                                    \
    } while (0)

#define ASSERT_TRUE(cond)                                                    \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::cerr << "  ✗ FAIL  " << __FILE__ << ":" << __LINE__         \
                      << "  condition false: " #cond "\n";                   \
            ++tests_failed; return;                                          \
        }                                                                    \
    } while (0)

#define ASSERT_STARTS_WITH(actual, prefix)                                   \
    do {                                                                     \
        auto _a = (actual); std::string _p = (prefix);                       \
        if (_a.substr(0, _p.size()) != _p) {                                 \
            std::cerr << "  ✗ FAIL  " << __FILE__ << ":" << __LINE__         \
                      << "\n    expected prefix: " << _p                     \
                      << "\n    actual:          " << _a << "\n";            \
            ++tests_failed; return;                                          \
        }                                                                    \
    } while (0)

#define RUN_TEST(fn)                                                         \
    do {                                                                     \
        std::cout << "  ▸ " << #fn << "...";                                 \
        fn();                                                                \
        ++tests_passed;                                                      \
        std::cout << " ✓" << std::endl;                                      \
    } while (0)

// ════════════════════════════════════════════════════════════════════════
//  1. Core Command Tests
// ════════════════════════════════════════════════════════════════════════

void test_set_get_basic() {
    KVStore s;
    ASSERT_EQ(s.set("key1", "hello"), "OK");
    ASSERT_EQ(s.get("key1"), "hello");
}

void test_set_overwrite() {
    KVStore s;
    s.set("k", "v1");
    s.set("k", "v2");
    ASSERT_EQ(s.get("k"), "v2");
}

void test_get_missing_key() {
    KVStore s;
    ASSERT_EQ(s.get("nonexistent"), "(nil)");
}

void test_del_existing() {
    KVStore s;
    s.set("k", "v");
    ASSERT_EQ(s.del("k"), "(integer) 1");
    ASSERT_EQ(s.get("k"), "(nil)");
}

void test_del_missing() {
    KVStore s;
    ASSERT_EQ(s.del("nope"), "(integer) 0");
}

void test_keys_glob_star() {
    KVStore s;
    s.set("user:1", "a");
    s.set("user:2", "b");
    s.set("session:1", "c");
    std::string result = s.keys("user:*");
    ASSERT_TRUE(result.find("user:1") != std::string::npos);
    ASSERT_TRUE(result.find("user:2") != std::string::npos);
    ASSERT_TRUE(result.find("session") == std::string::npos);
}

void test_keys_glob_question() {
    KVStore s;
    s.set("ab", "1");
    s.set("ac", "2");
    s.set("abc", "3");
    std::string result = s.keys("a?");
    ASSERT_TRUE(result.find("ab") != std::string::npos);
    ASSERT_TRUE(result.find("ac") != std::string::npos);
    ASSERT_TRUE(result.find("abc") == std::string::npos);
}

void test_keys_all() {
    KVStore s;
    s.set("a", "1");
    s.set("b", "2");
    std::string result = s.keys("*");
    ASSERT_TRUE(result.find("a") != std::string::npos);
    ASSERT_TRUE(result.find("b") != std::string::npos);
}

void test_keys_empty() {
    KVStore s;
    ASSERT_EQ(s.keys("*"), "(empty list or set)");
}

// ════════════════════════════════════════════════════════════════════════
//  2. TTL / Expiration Tests
// ════════════════════════════════════════════════════════════════════════

void test_ttl_no_expiry() {
    KVStore s;
    s.set("k", "v");
    ASSERT_EQ(s.ttl("k"), "-1");
}

void test_ttl_not_found() {
    KVStore s;
    ASSERT_EQ(s.ttl("nope"), "-2");
}

void test_ttl_with_expiry() {
    KVStore s;
    s.set("k", "v", 60);
    std::string t = s.ttl("k");
    int remaining = std::stoi(t);
    ASSERT_TRUE(remaining >= 58 && remaining <= 60);
}

void test_lazy_expiration() {
    KVStore s;
    s.set("k", "v", 1);  // expires in 1 second
    ASSERT_EQ(s.get("k"), "v");
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    ASSERT_EQ(s.get("k"), "(nil)");  // lazy expire on access
    ASSERT_EQ(s.ttl("k"), "-2");
}

void test_periodic_cleanup() {
    KVStore s;
    s.set("temp", "val", 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));
    // Key should have been cleaned by background thread
    ASSERT_EQ(s.keyCount(), (size_t)0);
}

// ════════════════════════════════════════════════════════════════════════
//  3. Extended Command Tests
// ════════════════════════════════════════════════════════════════════════

void test_exists() {
    KVStore s;
    ASSERT_EQ(s.exists("k"), "(integer) 0");
    s.set("k", "v");
    ASSERT_EQ(s.exists("k"), "(integer) 1");
}

void test_type_command() {
    KVStore s;
    ASSERT_EQ(s.type("nope"), "none");
    s.set("str", "hello");
    ASSERT_EQ(s.type("str"), "string");
    s.lpush("lst", "a");
    ASSERT_EQ(s.type("lst"), "list");
}

void test_rename() {
    KVStore s;
    s.set("old", "value");
    ASSERT_EQ(s.rename("old", "new"), "OK");
    ASSERT_EQ(s.get("old"), "(nil)");
    ASSERT_EQ(s.get("new"), "value");
}

void test_rename_missing() {
    KVStore s;
    ASSERT_EQ(s.rename("nope", "new"), "(error) no such key");
}

void test_expire_and_persist() {
    KVStore s;
    s.set("k", "v");
    ASSERT_EQ(s.ttl("k"), "-1");
    ASSERT_EQ(s.expire("k", 100), "(integer) 1");
    int t = std::stoi(s.ttl("k"));
    ASSERT_TRUE(t >= 98 && t <= 100);
    ASSERT_EQ(s.persist("k"), "(integer) 1");
    ASSERT_EQ(s.ttl("k"), "-1");
}

void test_append() {
    KVStore s;
    ASSERT_EQ(s.append("k", "hello"), "(integer) 5");
    ASSERT_EQ(s.append("k", " world"), "(integer) 11");
    ASSERT_EQ(s.get("k"), "hello world");
}

void test_strlen() {
    KVStore s;
    ASSERT_EQ(s.strlen("nope"), "(integer) 0");
    s.set("k", "hello");
    ASSERT_EQ(s.strlen("k"), "(integer) 5");
}

void test_dbsize_and_flushdb() {
    KVStore s;
    s.set("a", "1");
    s.set("b", "2");
    s.set("c", "3");
    ASSERT_EQ(s.dbsize(), "(integer) 3");
    ASSERT_EQ(s.flushdb(), "OK");
    ASSERT_EQ(s.dbsize(), "(integer) 0");
}

// ════════════════════════════════════════════════════════════════════════
//  4. Integer Operation Tests
// ════════════════════════════════════════════════════════════════════════

void test_incr_new_key() {
    KVStore s;
    ASSERT_EQ(s.incr("counter"), "(integer) 1");
    ASSERT_EQ(s.incr("counter"), "(integer) 2");
    ASSERT_EQ(s.get("counter"), "2");
}

void test_decr() {
    KVStore s;
    s.set("c", "10");
    ASSERT_EQ(s.decr("c"), "(integer) 9");
    ASSERT_EQ(s.decr("c"), "(integer) 8");
}

void test_incr_non_integer() {
    KVStore s;
    s.set("k", "not_a_number");
    ASSERT_STARTS_WITH(s.incr("k"), "(error)");
}

void test_incrby() {
    KVStore s;
    ASSERT_EQ(s.incrby("k", 100), "(integer) 100");
    ASSERT_EQ(s.incrby("k", -30), "(integer) 70");
}

void test_incr_wrongtype() {
    KVStore s;
    s.lpush("lst", "a");
    ASSERT_STARTS_WITH(s.incr("lst"), "(error) WRONGTYPE");
}

// ════════════════════════════════════════════════════════════════════════
//  5. List Operation Tests
// ════════════════════════════════════════════════════════════════════════

void test_lpush_rpush() {
    KVStore s;
    s.rpush("list", "a");
    s.rpush("list", "b");
    s.lpush("list", "z");
    ASSERT_EQ(s.llen("list"), "(integer) 3");
    ASSERT_EQ(s.lpop("list"), "z");
    ASSERT_EQ(s.rpop("list"), "b");
    ASSERT_EQ(s.lpop("list"), "a");
}

void test_lpop_rpop_empty() {
    KVStore s;
    ASSERT_EQ(s.lpop("nope"), "(nil)");
    ASSERT_EQ(s.rpop("nope"), "(nil)");
}

void test_list_auto_delete() {
    KVStore s;
    s.rpush("lst", "only");
    ASSERT_EQ(s.lpop("lst"), "only");
    ASSERT_EQ(s.exists("lst"), "(integer) 0");
}

void test_lrange() {
    KVStore s;
    s.rpush("l", "a");
    s.rpush("l", "b");
    s.rpush("l", "c");
    s.rpush("l", "d");
    std::string r = s.lrange("l", 0, -1);
    ASSERT_TRUE(r.find("a") != std::string::npos);
    ASSERT_TRUE(r.find("d") != std::string::npos);
    // Negative indexing
    std::string r2 = s.lrange("l", -2, -1);
    ASSERT_TRUE(r2.find("c") != std::string::npos);
    ASSERT_TRUE(r2.find("d") != std::string::npos);
}

void test_list_wrongtype() {
    KVStore s;
    s.set("str", "hello");
    ASSERT_STARTS_WITH(s.lpush("str", "x"), "(error) WRONGTYPE");
}

// ════════════════════════════════════════════════════════════════════════
//  6. LRU Eviction Tests
// ════════════════════════════════════════════════════════════════════════

void test_lru_basic_eviction() {
    KVStore s(3);  // max 3 keys
    s.set("a", "1");
    s.set("b", "2");
    s.set("c", "3");
    // Adding a 4th key should evict the LRU (a)
    s.set("d", "4");
    ASSERT_EQ(s.get("a"), "(nil)");
    ASSERT_EQ(s.get("d"), "4");
}

void test_lru_access_refreshes() {
    KVStore s(3);
    s.set("a", "1");
    s.set("b", "2");
    s.set("c", "3");
    s.get("a");  // touch 'a', making 'b' the LRU
    s.set("d", "4");
    ASSERT_EQ(s.get("b"), "(nil)");  // b should be evicted
    ASSERT_EQ(s.get("a"), "1");      // a should survive
}

// ════════════════════════════════════════════════════════════════════════
//  7. Persistence Tests
// ════════════════════════════════════════════════════════════════════════

void test_save_load() {
    const std::string fname = "test_dump.json";
    {
        KVStore s;
        s.set("str_key", "hello");
        s.set("ttl_key", "ephemeral", 3600);
        s.rpush("list_key", "a");
        s.rpush("list_key", "b");
        ASSERT_EQ(s.save(fname), "OK");
    }
    {
        KVStore s;
        ASSERT_EQ(s.load(fname), "OK");
        ASSERT_EQ(s.get("str_key"), "hello");
        ASSERT_EQ(s.get("ttl_key"), "ephemeral");
        ASSERT_EQ(s.lpop("list_key"), "a");
        ASSERT_EQ(s.lpop("list_key"), "b");
    }
    std::remove(fname.c_str());
}

void test_save_skips_expired() {
    const std::string fname = "test_dump2.json";
    {
        KVStore s;
        s.set("live", "yes");
        s.set("dead", "no", 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
        ASSERT_EQ(s.save(fname), "OK");
    }
    {
        KVStore s;
        s.load(fname);
        ASSERT_EQ(s.get("live"), "yes");
        ASSERT_EQ(s.get("dead"), "(nil)");
    }
    std::remove(fname.c_str());
}

void test_load_missing_file() {
    KVStore s;
    ASSERT_STARTS_WITH(s.load("nonexistent_file.json"), "(error)");
}

// ════════════════════════════════════════════════════════════════════════
//  8. Edge Case Tests
// ════════════════════════════════════════════════════════════════════════

void test_special_characters_in_value() {
    KVStore s;
    std::string json_val = R"({"name":"Alice","age":30})";
    s.set("user", json_val);
    ASSERT_EQ(s.get("user"), json_val);
}

void test_empty_value() {
    KVStore s;
    s.set("k", "");
    ASSERT_EQ(s.get("k"), "");
    ASSERT_EQ(s.strlen("k"), "(integer) 0");
}

void test_set_changes_type() {
    KVStore s;
    s.lpush("k", "list_val");
    ASSERT_EQ(s.type("k"), "list");
    s.set("k", "string_val");  // SET should overwrite type
    ASSERT_EQ(s.type("k"), "string");
    ASSERT_EQ(s.get("k"), "string_val");
}

void test_persist_save_load_special_chars() {
    const std::string fname = "test_special.json";
    {
        KVStore s;
        s.set("q", "line1\nline2\ttab\\backslash\"quote");
        ASSERT_EQ(s.save(fname), "OK");
    }
    {
        KVStore s;
        s.load(fname);
        std::string v = s.get("q");
        ASSERT_TRUE(v.find("line1\nline2") != std::string::npos);
        ASSERT_TRUE(v.find("\\backslash") != std::string::npos);
    }
    std::remove(fname.c_str());
}

// ════════════════════════════════════════════════════════════════════════
//  9. Concurrent Stress Test
// ════════════════════════════════════════════════════════════════════════

void test_concurrent_set_get() {
    KVStore s;
    const int NUM_THREADS = 8;
    const int OPS_PER_THREAD = 5000;
    std::atomic<int> errors{0};

    std::vector<std::thread> threads;
    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&s, &errors, t, OPS_PER_THREAD]() {
            for (int i = 0; i < OPS_PER_THREAD; ++i) {
                std::string key = "t" + std::to_string(t) + "_k" + std::to_string(i);
                std::string val = "v" + std::to_string(i);
                s.set(key, val);
                std::string got = s.get(key);
                if (got != val && got != "(nil)") {
                    ++errors;
                }
            }
        });
    }

    for (auto& t : threads) t.join();
    ASSERT_EQ(errors.load(), 0);
    ASSERT_TRUE(s.keyCount() > 0);
}

void test_concurrent_incr() {
    KVStore s;
    s.set("atomic_counter", "0");
    const int NUM_THREADS = 8;
    const int INCR_PER_THREAD = 1000;

    std::vector<std::thread> threads;
    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&s, INCR_PER_THREAD]() {
            for (int i = 0; i < INCR_PER_THREAD; ++i) {
                s.incr("atomic_counter");
            }
        });
    }
    for (auto& t : threads) t.join();

    std::string val = s.get("atomic_counter");
    ASSERT_EQ(val, std::to_string(NUM_THREADS * INCR_PER_THREAD));
}

void test_concurrent_list_ops() {
    KVStore s;
    const int NUM_THREADS = 4;
    const int OPS = 500;
    std::atomic<int> errors{0};

    std::vector<std::thread> threads;
    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&s, &errors, OPS]() {
            for (int i = 0; i < OPS; ++i) {
                s.rpush("shared_list", std::to_string(i));
            }
        });
    }
    for (auto& t : threads) t.join();

    std::string len = s.llen("shared_list");
    ASSERT_EQ(len, "(integer) " + std::to_string(NUM_THREADS * OPS));
}

// ════════════════════════════════════════════════════════════════════════
//  Main
// ════════════════════════════════════════════════════════════════════════

int main() {
    std::cout << "\n╔══════════════════════════════════════════════╗\n"
              <<   "║       KVStore Test Suite                     ║\n"
              <<   "╚══════════════════════════════════════════════╝\n\n";

    std::cout << "── Core Commands ──\n";
    RUN_TEST(test_set_get_basic);
    RUN_TEST(test_set_overwrite);
    RUN_TEST(test_get_missing_key);
    RUN_TEST(test_del_existing);
    RUN_TEST(test_del_missing);
    RUN_TEST(test_keys_glob_star);
    RUN_TEST(test_keys_glob_question);
    RUN_TEST(test_keys_all);
    RUN_TEST(test_keys_empty);

    std::cout << "\n── TTL / Expiration ──\n";
    RUN_TEST(test_ttl_no_expiry);
    RUN_TEST(test_ttl_not_found);
    RUN_TEST(test_ttl_with_expiry);
    RUN_TEST(test_lazy_expiration);
    RUN_TEST(test_periodic_cleanup);

    std::cout << "\n── Extended Commands ──\n";
    RUN_TEST(test_exists);
    RUN_TEST(test_type_command);
    RUN_TEST(test_rename);
    RUN_TEST(test_rename_missing);
    RUN_TEST(test_expire_and_persist);
    RUN_TEST(test_append);
    RUN_TEST(test_strlen);
    RUN_TEST(test_dbsize_and_flushdb);

    std::cout << "\n── Integer Operations ──\n";
    RUN_TEST(test_incr_new_key);
    RUN_TEST(test_decr);
    RUN_TEST(test_incr_non_integer);
    RUN_TEST(test_incrby);
    RUN_TEST(test_incr_wrongtype);

    std::cout << "\n── List Operations ──\n";
    RUN_TEST(test_lpush_rpush);
    RUN_TEST(test_lpop_rpop_empty);
    RUN_TEST(test_list_auto_delete);
    RUN_TEST(test_lrange);
    RUN_TEST(test_list_wrongtype);

    std::cout << "\n── LRU Eviction ──\n";
    RUN_TEST(test_lru_basic_eviction);
    RUN_TEST(test_lru_access_refreshes);

    std::cout << "\n── Persistence ──\n";
    RUN_TEST(test_save_load);
    RUN_TEST(test_save_skips_expired);
    RUN_TEST(test_load_missing_file);

    std::cout << "\n── Edge Cases ──\n";
    RUN_TEST(test_special_characters_in_value);
    RUN_TEST(test_empty_value);
    RUN_TEST(test_set_changes_type);
    RUN_TEST(test_persist_save_load_special_chars);

    std::cout << "\n── Concurrency (stress) ──\n";
    RUN_TEST(test_concurrent_set_get);
    RUN_TEST(test_concurrent_incr);
    RUN_TEST(test_concurrent_list_ops);

    std::cout << "\n══════════════════════════════════════════════\n";
    std::cout << "  Results: " << tests_passed << " passed, "
              << tests_failed << " failed\n";
    std::cout << "══════════════════════════════════════════════\n\n";

    return tests_failed > 0 ? 1 : 0;
}
