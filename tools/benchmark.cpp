/// @file benchmark.cpp
/// @brief Performance benchmark for KVStore – measures ops/sec for SET, GET,
///        INCR under single-threaded and multi-threaded workloads.

#include "../src/kvstore.h"

#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <string>
#include <iomanip>
#include <atomic>

using Clock = std::chrono::high_resolution_clock;

static double opsPerSec(int ops, double elapsed_ms) {
    return (ops / elapsed_ms) * 1000.0;
}

static void benchSingleThreadSET(KVStore& s, int n) {
    auto start = Clock::now();
    for (int i = 0; i < n; ++i)
        s.set("bench_key_" + std::to_string(i), "value_" + std::to_string(i));
    auto end = Clock::now();
    double ms = std::chrono::duration<double, std::milli>(end - start).count();
    std::cout << "  SET  (1 thread, " << n << " ops):  "
              << std::fixed << std::setprecision(0)
              << opsPerSec(n, ms) << " ops/sec  ("
              << std::setprecision(1) << ms << " ms)\n";
}

static void benchSingleThreadGET(KVStore& s, int n) {
    auto start = Clock::now();
    for (int i = 0; i < n; ++i)
        s.get("bench_key_" + std::to_string(i));
    auto end = Clock::now();
    double ms = std::chrono::duration<double, std::milli>(end - start).count();
    std::cout << "  GET  (1 thread, " << n << " ops):  "
              << std::fixed << std::setprecision(0)
              << opsPerSec(n, ms) << " ops/sec  ("
              << std::setprecision(1) << ms << " ms)\n";
}

static void benchMultiThread(KVStore& s, int nThreads, int opsPerThread,
                             const std::string& label, bool write) {
    std::vector<std::thread> threads;
    auto start = Clock::now();

    for (int t = 0; t < nThreads; ++t) {
        threads.emplace_back([&s, t, opsPerThread, write]() {
            for (int i = 0; i < opsPerThread; ++i) {
                std::string key = "mt_" + std::to_string(t) + "_" + std::to_string(i);
                if (write)
                    s.set(key, "val");
                else
                    s.get(key);
            }
        });
    }
    for (auto& t : threads) t.join();

    auto end = Clock::now();
    int totalOps = nThreads * opsPerThread;
    double ms = std::chrono::duration<double, std::milli>(end - start).count();
    std::cout << "  " << label << " (" << nThreads << " threads, "
              << totalOps << " ops):  "
              << std::fixed << std::setprecision(0)
              << opsPerSec(totalOps, ms) << " ops/sec  ("
              << std::setprecision(1) << ms << " ms)\n";
}

static void benchINCR(KVStore& s, int nThreads, int opsPerThread) {
    s.set("bench_counter", "0");
    std::vector<std::thread> threads;
    auto start = Clock::now();
    for (int t = 0; t < nThreads; ++t) {
        threads.emplace_back([&s, opsPerThread]() {
            for (int i = 0; i < opsPerThread; ++i)
                s.incr("bench_counter");
        });
    }
    for (auto& t : threads) t.join();
    auto end = Clock::now();
    int totalOps = nThreads * opsPerThread;
    double ms = std::chrono::duration<double, std::milli>(end - start).count();
    std::cout << "  INCR (" << nThreads << " threads, "
              << totalOps << " ops):  "
              << std::fixed << std::setprecision(0)
              << opsPerSec(totalOps, ms) << " ops/sec  ("
              << std::setprecision(1) << ms << " ms)\n";
}

int main() {
    const int N = 100000;
    const int THREADS = 8;
    const int OPS_PT = 25000;

    std::cout << "\n╔══════════════════════════════════════════════╗\n"
              <<   "║       KVStore Performance Benchmark          ║\n"
              <<   "╚══════════════════════════════════════════════╝\n\n";

    std::cout << "── Single-Threaded ──\n";
    {
        KVStore s;
        benchSingleThreadSET(s, N);
        benchSingleThreadGET(s, N);
    }

    std::cout << "\n── Multi-Threaded ──\n";
    {
        KVStore s;
        benchMultiThread(s, THREADS, OPS_PT, "SET ", true);
        benchMultiThread(s, THREADS, OPS_PT, "GET ", false);
        benchINCR(s, THREADS, OPS_PT);
    }

    std::cout << "\n── Mixed Read/Write (80% GET / 20% SET) ──\n";
    {
        KVStore s;
        // Pre-populate
        for (int i = 0; i < 10000; ++i)
            s.set("pre_" + std::to_string(i), "v");

        std::vector<std::thread> threads;
        int totalOps = THREADS * OPS_PT;
        auto start = Clock::now();

        for (int t = 0; t < THREADS; ++t) {
            threads.emplace_back([&s, t, OPS_PT]() {
                for (int i = 0; i < OPS_PT; ++i) {
                    if (i % 5 == 0)
                        s.set("pre_" + std::to_string(i % 10000), "new");
                    else
                        s.get("pre_" + std::to_string(i % 10000));
                }
            });
        }
        for (auto& t : threads) t.join();

        auto end = Clock::now();
        double ms = std::chrono::duration<double, std::milli>(end - start).count();
        std::cout << "  MIX  (" << THREADS << " threads, "
                  << totalOps << " ops):  "
                  << std::fixed << std::setprecision(0)
                  << opsPerSec(totalOps, ms) << " ops/sec  ("
                  << std::setprecision(1) << ms << " ms)\n";
    }

    std::cout << "\n══════════════════════════════════════════════\n\n";
    return 0;
}
