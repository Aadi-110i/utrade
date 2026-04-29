# KVStore – In-Memory Key-Value Store with TTL

A **thread-safe, Redis-like** in-memory key-value store built in **C++17** with TTL-based expiration, JSON persistence, LRU eviction, list operations, atomic integer ops, and pub/sub messaging — accessible via **TCP socket** or **stdin** text protocol.

> **Domain:** Caching / Session Management  
> **Use Case:** Low-latency lookup tables, session stores, and caching layers in trading systems.

---

## ✅ Features at a Glance

| Category | Status | Commands |
|----------|--------|----------|
| **Core Data Ops** | ✅ | `SET`, `GET`, `DEL`, `KEYS`, `TTL` |
| **TTL Expiration** | ✅ | Lazy (on access) + Periodic (every 1s) |
| **Thread Safety** | ✅ | `std::mutex` protecting all operations |
| **TCP / STDIN I/O** | ✅ | Configurable port, multi-client |
| **Persistence** | ✅ | `SAVE` / `LOAD` (JSON snapshots) |
| **Memory Stats** | ✅ | `STATS`, `DBSIZE` |
| ⭐ **Integer Ops** | ✅ | `INCR`, `DECR`, `INCRBY`, `DECRBY` |
| ⭐ **List Ops** | ✅ | `LPUSH`, `RPUSH`, `LPOP`, `RPOP`, `LRANGE`, `LLEN` |
| ⭐ **Pub/Sub** | ✅ | `SUBSCRIBE`, `UNSUBSCRIBE`, `PUBLISH` |
| ⭐ **LRU Eviction** | ✅ | `--max-keys N` with O(1) eviction |
| **Extended Ops** | ✅ | `EXISTS`, `TYPE`, `RENAME`, `EXPIRE`, `PERSIST`, `APPEND`, `STRLEN`, `FLUSHDB` |
| **Test Suite** | ✅ | **44 tests** – unit, integration, concurrency stress |
| **Benchmarks** | ✅ | Single & multi-threaded throughput |

---

## 🏗️ Architecture

```
┌──────────────────────────────────────────────────┐
│                     main.cpp                     │
│              CLI argument parsing                │
└────────────────────┬─────────────────────────────┘
                     │
         ┌───────────┴───────────┐
         ▼                       ▼
  ┌─────────────┐        ┌─────────────┐
  │   Server    │        │   Server    │
  │ (TCP Mode)  │        │(STDIN Mode) │
  │ thread/client│       │  blocking   │
  └──────┬──────┘        └──────┬──────┘
         │                      │
         ▼                      ▼
  ┌──────────────────────────────────┐
  │        CommandHandler            │
  │   Tokenizer (quote-aware)       │
  │   Command dispatch (30+ cmds)   │
  └──────────────┬───────────────────┘
                 │
                 ▼
  ┌──────────────────────────────────┐
  │            KVStore               │
  │                                  │
  │  ┌──────────────────────────┐    │
  │  │ std::unordered_map       │    │   O(1) lookup
  │  │ key → Entry              │    │
  │  └──────────────────────────┘    │
  │  ┌──────────────────────────┐    │
  │  │ std::list (LRU order)    │    │   O(1) move-to-front
  │  └──────────────────────────┘    │
  │  ┌──────────────────────────┐    │
  │  │ priority_queue (min-heap)│    │   O(log n) TTL expiry
  │  └──────────────────────────┘    │
  │  ┌──────────────────────────┐    │
  │  │ Background thread        │    │   1s periodic cleanup
  │  └──────────────────────────┘    │
  └──────────────────────────────────┘
```

### Thread Safety Strategy
- **Data operations**: Protected by `std::mutex` (all SET/GET/DEL etc.)
- **Pub/Sub**: Separate `std::mutex` to avoid blocking the data path
- **Background cleanup**: Uses `std::condition_variable` for graceful shutdown
- **Atomic counters**: `std::atomic<size_t>` for expired-key statistics

### TTL Expiration (Dual Strategy)
1. **Lazy expiration** – Every `GET`, `KEYS`, `TTL`, `EXISTS` access checks and removes expired keys inline
2. **Periodic cleanup** – Background thread wakes every **1 second**, drains the min-heap of all entries past their deadline

### LRU Eviction
- `std::list<string>` tracks access order (front = most recent, back = LRU)
- Every access (`GET`, `SET`, `INCR`, etc.) calls `touchLRU()` → O(1) move-to-front
- When `store_.size() >= max_keys_`, `evictLRU()` removes the tail → O(1)

---

## 📊 Benchmark Results

Measured on a single machine (MSYS2/MinGW g++ 14.2, Windows, -O2):

| Workload | Threads | Throughput |
|----------|---------|------------|
| SET | 1 | **~1.1M ops/sec** |
| GET | 1 | **~3.9M ops/sec** |
| SET | 8 | **~831K ops/sec** |
| GET | 8 | **~1.6M ops/sec** |
| INCR (contended) | 8 | **~3.5M ops/sec** |
| Mixed 80/20 R/W | 8 | **~3.1M ops/sec** |

Run benchmarks yourself:
```bash
g++ -std=c++17 -O2 -o benchmark tools/benchmark.cpp src/kvstore.cpp -lpthread
./benchmark
```

---

## 🧪 Test Suite

**44 tests** across 9 categories – zero external dependencies:

| Category | Tests | Coverage |
|----------|-------|----------|
| Core Commands | 9 | SET, GET, DEL, KEYS (glob), edge cases |
| TTL / Expiration | 5 | Lazy expire, periodic cleanup, TTL values |
| Extended Commands | 8 | EXISTS, TYPE, RENAME, EXPIRE, PERSIST, APPEND, STRLEN, FLUSHDB |
| Integer Operations | 5 | INCR, DECR, INCRBY, wrong-type, non-integer |
| List Operations | 5 | LPUSH/RPUSH/LPOP/RPOP, auto-delete, LRANGE, wrong-type |
| LRU Eviction | 2 | Basic eviction, access-refresh order |
| Persistence | 3 | SAVE/LOAD round-trip, skip-expired, missing file |
| Edge Cases | 4 | JSON values, empty strings, type change, special chars |
| Concurrency | 3 | 8-thread SET/GET stress, atomic INCR, concurrent list ops |

```bash
g++ -std=c++17 -O2 -o run_tests tests/test_kvstore.cpp src/kvstore.cpp -lpthread
./run_tests
```

---

## 📋 Full Command Reference

### Core
| Command | Syntax | Description |
|---------|--------|-------------|
| `SET` | `SET key value [EX seconds]` | Set a key with optional TTL |
| `GET` | `GET key` | Retrieve value (`(nil)` if missing/expired) |
| `DEL` | `DEL key` | Delete a key |
| `KEYS` | `KEYS pattern` | Glob pattern matching (`*`, `?`) |
| `TTL` | `TTL key` | Remaining seconds (`-1` no expiry, `-2` not found) |

### Extended
| Command | Syntax | Description |
|---------|--------|-------------|
| `EXISTS` | `EXISTS key` | Check if key exists (returns 0/1) |
| `TYPE` | `TYPE key` | Returns `string`, `list`, or `none` |
| `RENAME` | `RENAME key newkey` | Rename a key |
| `EXPIRE` | `EXPIRE key seconds` | Set TTL on existing key |
| `PERSIST` | `PERSIST key` | Remove TTL (make persistent) |
| `APPEND` | `APPEND key value` | Append to string value |
| `STRLEN` | `STRLEN key` | String length |
| `DBSIZE` | `DBSIZE` | Number of keys |
| `FLUSHDB` | `FLUSHDB` | Delete all keys |

### Persistence & Stats
| Command | Syntax | Description |
|---------|--------|-------------|
| `SAVE` | `SAVE [filename]` | Snapshot to JSON (default: `dump.json`) |
| `LOAD` | `LOAD [filename]` | Restore from snapshot |
| `STATS` | `STATS` | Memory usage, key count, expired cleaned |

### Integer Operations ⭐
| Command | Syntax | Description |
|---------|--------|-------------|
| `INCR` | `INCR key` | Atomically increment by 1 |
| `DECR` | `DECR key` | Atomically decrement by 1 |
| `INCRBY` | `INCRBY key delta` | Atomically increment by delta |
| `DECRBY` | `DECRBY key delta` | Atomically decrement by delta |

### List Operations ⭐
| Command | Syntax | Description |
|---------|--------|-------------|
| `LPUSH` | `LPUSH key value` | Push to head |
| `RPUSH` | `RPUSH key value` | Push to tail |
| `LPOP` | `LPOP key` | Pop from head |
| `RPOP` | `RPOP key` | Pop from tail |
| `LRANGE` | `LRANGE key start stop` | Get range (supports negative indices) |
| `LLEN` | `LLEN key` | List length |

### Pub/Sub ⭐
| Command | Syntax | Description |
|---------|--------|-------------|
| `SUBSCRIBE` | `SUBSCRIBE channel` | Subscribe (TCP mode only) |
| `UNSUBSCRIBE` | `UNSUBSCRIBE [channel]` | Unsubscribe |
| `PUBLISH` | `PUBLISH channel message` | Publish message |

### Utility
| Command | Description |
|---------|-------------|
| `PING` | Returns `PONG` |
| `QUIT` / `EXIT` | Disconnect |

---

## 🔨 Build Instructions

### Prerequisites
- **C++17** compiler (GCC 7+, Clang 5+, MSVC 2017+)
- No external libraries required

### Quick Build (g++)
```bash
# Server
g++ -std=c++17 -O2 -o kvstore src/main.cpp src/kvstore.cpp src/command_handler.cpp src/server.cpp -lpthread

# Tests
g++ -std=c++17 -O2 -o run_tests tests/test_kvstore.cpp src/kvstore.cpp -lpthread

# Benchmark
g++ -std=c++17 -O2 -o benchmark tools/benchmark.cpp src/kvstore.cpp -lpthread
```

> **Windows**: Add `-lws2_32` to the link flags.

### CMake Build
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

---

## 🚀 Usage

### STDIN Mode (default)
```bash
./kvstore --stdin
```

### TCP Mode
```bash
./kvstore --tcp --port 7379 --max-keys 10000
```

### Command-Line Options
| Flag | Default | Description |
|------|---------|-------------|
| `--stdin` | ✅ | Interactive stdin/stdout mode |
| `--tcp` | | TCP server mode |
| `--port N` | 7379 | TCP listening port |
| `--max-keys N` | 0 (unlimited) | LRU eviction threshold |

---

## 💡 Sample Session

```
kvstore> SET user:1 '{"name":"Alice"}' EX 300
OK
kvstore> GET user:1
{"name":"Alice"}
kvstore> TTL user:1
299
kvstore> SET counter 0
OK
kvstore> KEYS user:*
user:1
kvstore> INCR counter
(integer) 1
kvstore> INCRBY counter 99
(integer) 100
kvstore> LPUSH tasks "buy_AAPL"
(integer) 1
kvstore> RPUSH tasks "sell_TSLA"
(integer) 2
kvstore> LRANGE tasks 0 -1
1) buy_AAPL
2) sell_TSLA
kvstore> EXPIRE counter 60
(integer) 1
kvstore> PERSIST counter
(integer) 1
kvstore> SAVE
OK
kvstore> STATS
---- Store Statistics ----
total_keys        : 3
expired_cleaned   : 0
max_keys_limit    : unlimited
memory_estimate   : 1284 bytes
--------------------------
```

---

## 📁 Project Structure

```
├── CMakeLists.txt              # CMake build config
├── Makefile                    # Direct g++ build
├── README.md
├── .gitignore
├── src/
│   ├── main.cpp                # Entry point, CLI parsing
│   ├── kvstore.h               # KVStore class (header)
│   ├── kvstore.cpp             # Core store implementation (~930 lines)
│   ├── command_handler.h       # Command parser/dispatcher
│   ├── command_handler.cpp     # 30+ command routing
│   ├── server.h                # TCP + STDIN server
│   └── server.cpp              # Cross-platform network I/O
├── tests/
│   └── test_kvstore.cpp        # 44-test comprehensive suite
└── tools/
    └── benchmark.cpp           # Performance measurement tool
```

---

## 🧠 Design Decisions & Trade-offs

| Decision | Rationale |
|----------|-----------|
| `std::unordered_map` for storage | O(1) amortized lookup – critical for sub-microsecond caching |
| `std::priority_queue` (min-heap) | Efficiently finds next-to-expire key in O(log n) during periodic sweep |
| `std::list` for LRU tracking | O(1) move-to-front via iterator, O(1) evict-from-back |
| `std::deque` for list values | O(1) push/pop at both ends (vs `std::vector` O(n) at front) |
| Separate pub/sub mutex | Prevents message fanout from blocking hot-path data operations |
| Lazy + periodic expiration | Lazy avoids returning stale data; periodic reclaims memory proactively |
| Zero dependencies | Pure C++17 stdlib – no nlohmann/json, no Boost, no gtest |
| Custom JSON serializer | Avoids library dependency for a well-scoped persistence format |
| `std::condition_variable` for cleanup | Allows instant graceful shutdown (vs `sleep()` blocking) |
| Thread-per-client TCP model | Simple, correct; sufficient for moderate client counts |

### Why not `std::shared_mutex` (reader-writer lock)?
`GET` operations perform lazy expiration (a write), making pure read-locking incorrect without lock upgrading. The mutex approach is simpler, correct, and the benchmark shows **3.9M GET ops/sec** single-threaded – well within trading system requirements.

---

## 📜 License

MIT License – free to use, modify, and distribute.
