# KVStore – In-Memory Key-Value Store with TTL

A thread-safe, Redis-like in-memory key-value store built in **C++17** with TTL-based expiration, JSON persistence snapshots, LRU eviction, list operations, and pub/sub messaging — all accessible via a **TCP socket** or **stdin** text protocol.

> **Domain:** Caching / Session Management  
> **Use Case:** Low-latency lookup tables, session stores, and caching layers in trading systems.

---

## Features

### Core (Must-Have)
| Command | Syntax | Description |
|---------|--------|-------------|
| `SET`   | `SET key value [EX seconds]` | Set a key with optional TTL |
| `GET`   | `GET key` | Retrieve a value (`(nil)` if missing/expired) |
| `DEL`   | `DEL key` | Delete a key |
| `KEYS`  | `KEYS pattern` | Glob-style pattern matching (`*`, `?`) |
| `TTL`   | `TTL key` | Remaining seconds (`-1` = no expiry, `-2` = not found) |
| `SAVE`  | `SAVE [filename]` | Snapshot non-expired keys to JSON (default: `dump.json`) |
| `LOAD`  | `LOAD [filename]` | Restore from JSON snapshot |
| `STATS` | `STATS` | Print memory stats, key count, expired keys cleaned |
| `PING`  | `PING` | Returns `PONG` (health check) |

### Bonus – Integer Operations ⭐
| Command | Syntax | Description |
|---------|--------|-------------|
| `INCR`  | `INCR key` | Atomically increment integer value |
| `DECR`  | `DECR key` | Atomically decrement integer value |

### Bonus – List Operations ⭐
| Command | Syntax | Description |
|---------|--------|-------------|
| `LPUSH` | `LPUSH key value` | Push to the left (head) of a list |
| `RPUSH` | `RPUSH key value` | Push to the right (tail) of a list |
| `LPOP`  | `LPOP key` | Pop from the left (head) |
| `RPOP`  | `RPOP key` | Pop from the right (tail) |
| `LRANGE`| `LRANGE key start stop` | Get elements in range |

### Bonus – Pub/Sub ⭐
| Command | Syntax | Description |
|---------|--------|-------------|
| `SUBSCRIBE` | `SUBSCRIBE channel` | Subscribe to a channel (TCP mode) |
| `UNSUBSCRIBE` | `UNSUBSCRIBE [channel]` | Unsubscribe from channel(s) |
| `PUBLISH` | `PUBLISH channel message` | Publish a message to a channel |

### Bonus – LRU Eviction ⭐
When `--max-keys N` is set, the store automatically evicts the least-recently-used key when capacity is exceeded.

---

## Architecture

```
┌───────────────────────────────────────────────┐
│                   main.cpp                    │
│          CLI argument parsing                 │
└──────────────────┬────────────────────────────┘
                   │
       ┌───────────┴───────────┐
       ▼                       ▼
┌─────────────┐        ┌─────────────┐
│   Server    │        │   Server    │
│ (TCP Mode)  │        │(STDIN Mode) │
└──────┬──────┘        └──────┬──────┘
       │                      │
       ▼                      ▼
┌──────────────────────────────────┐
│        CommandHandler            │
│  Tokenizer + Command Dispatch    │
└──────────────┬───────────────────┘
               │
               ▼
┌──────────────────────────────────┐
│            KVStore               │
│  ┌─────────────────────────┐     │
│  │  std::unordered_map     │     │
│  │  (key → Entry)          │     │
│  └─────────────────────────┘     │
│  ┌─────────────────────────┐     │
│  │  std::list (LRU order)  │     │
│  └─────────────────────────┘     │
│  ┌─────────────────────────┐     │
│  │  priority_queue (TTL)   │     │
│  └─────────────────────────┘     │
│  ┌─────────────────────────┐     │
│  │  Background cleanup     │     │
│  │  thread (every 1s)      │     │
│  └─────────────────────────┘     │
└──────────────────────────────────┘
```

### Thread Safety
- All store operations are protected by `std::mutex`.
- Pub/Sub uses a separate `std::mutex` to avoid blocking data operations.
- Background cleanup uses `std::condition_variable` for graceful shutdown.

### TTL Expiration Strategy
1. **Lazy expiration** – Checked on every `GET`, `KEYS`, `TTL` access. Expired keys are removed inline.
2. **Periodic cleanup** – A background thread wakes every **1 second** and drains the min-heap of expired entries.

### LRU Eviction
- A doubly-linked list (`std::list`) tracks access order.
- Every access moves the key to the front (most recently used).
- On capacity overflow, the tail (least recently used) is evicted.

---

## Build Instructions

### Prerequisites
- **C++17** compiler (GCC 7+, Clang 5+, MSVC 2017+)
- **CMake 3.14+**

### Build (Linux / macOS)
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### Build (Windows – MSVC)
```powershell
mkdir build; cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --config Release
```

### Build (Windows – MinGW)
```powershell
mkdir build; cd build
cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
mingw32-make
```

---

## Usage

### STDIN Mode (default)
```bash
./kvstore --stdin
```

### TCP Mode
```bash
./kvstore --tcp --port 7379 --max-keys 10000
```

Connect with any TCP client:
```bash
# Linux / macOS
nc localhost 7379

# Windows (PowerShell)
# Use telnet or a TCP client
telnet localhost 7379
```

### Command-Line Options
```
  --port PORT       TCP port (default: 7379)
  --stdin           stdin/stdout mode (default)
  --tcp             TCP server mode
  --max-keys N      LRU eviction threshold (0 = unlimited)
  --help            Show help
```

---

## Sample Session

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
kvstore> DEL user:1
(integer) 1
kvstore> GET user:1
(nil)
kvstore> INCR counter
(integer) 1
kvstore> INCR counter
(integer) 2
kvstore> DECR counter
(integer) 1
kvstore> LPUSH mylist hello
(integer) 1
kvstore> RPUSH mylist world
(integer) 2
kvstore> LRANGE mylist 0 -1
1) hello
2) world
kvstore> LPOP mylist
hello
kvstore> SAVE
OK
kvstore> STATS
---- Store Statistics ----
total_keys        : 2
expired_cleaned   : 0
max_keys_limit    : unlimited
memory_estimate   : 1284 bytes
--------------------------
kvstore> QUIT
Bye!
```

---

## Project Structure

```
├── CMakeLists.txt            # Build configuration
├── README.md                 # This file
├── .gitignore
└── src/
    ├── main.cpp              # Entry point, CLI parsing
    ├── kvstore.h             # KVStore class definition
    ├── kvstore.cpp           # Core store implementation
    ├── command_handler.h     # Command parser/dispatcher
    ├── command_handler.cpp   # Command routing logic
    ├── server.h              # TCP + STDIN server
    └── server.cpp            # Network I/O, client handling
```

---

## Design Decisions

| Decision | Rationale |
|----------|-----------|
| `std::unordered_map` for storage | O(1) average lookup — critical for low-latency caching |
| `std::priority_queue` (min-heap) for TTL | Efficiently finds the next key to expire in O(log n) |
| `std::list` for LRU tracking | O(1) move-to-front and evict-from-back operations |
| `std::deque` for list values | O(1) push/pop at both ends (LPUSH/RPUSH/LPOP/RPOP) |
| Separate mutex for pub/sub | Prevents pub/sub traffic from blocking data operations |
| No external dependencies | Zero third-party libraries — pure C++17 standard library |

---

## License

MIT License – free to use, modify, and distribute.
