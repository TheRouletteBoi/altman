#pragma once
#include <atomic>
#include <mutex>
#include <vector>
#include <thread>
#include <condition_variable>
#include <memory>

class ShutdownManager {
    public:
        static ShutdownManager &instance() {
            static ShutdownManager mgr;
            return mgr;
        }

        void registerThread(std::thread &&t) {
            std::lock_guard lock(threadsMutex);
            threads.push_back(std::move(t));
        }

        void requestShutdown() {
            if (shuttingDown.exchange(true)) {
                return;
            }

            shutdownCV.notify_all();
        }

        bool isShuttingDown() const {
            return shuttingDown.load(std::memory_order_acquire);
        }

        void waitForShutdown() {
            std::vector<std::thread> threadsToJoin;

            {
                std::lock_guard lock(threadsMutex);
                threadsToJoin = std::move(threads);
                threads.clear();
            }

            for (auto &t: threadsToJoin) {
                if (t.joinable()) {
                    t.join();
                }
            }
        }
        std::condition_variable &shutdownCondition() {
            return shutdownCV;
        }

        template<typename Rep, typename Period> bool sleepFor(const std::chrono::duration<Rep, Period> &duration) {
            std::unique_lock lock(sleepMutex);

            return shutdownCV.wait_for(lock, duration, [this] {
                return isShuttingDown();
            });
        }

        void debugPrintThreadCount() {
            std::lock_guard lock(threadsMutex);
        }

        ShutdownManager(const ShutdownManager &) = delete;
        ShutdownManager &operator=(const ShutdownManager &) = delete;

    private:
        ShutdownManager() = default;

        std::atomic<bool> shuttingDown {false};
        std::mutex threadsMutex;
        std::vector<std::thread> threads;
        std::condition_variable shutdownCV;
        std::mutex sleepMutex;
};
