#pragma once
#include <thread>
#include <utility>
#include <functional>
#include <deque>
#include <mutex>
#include "shutdown_manager.h"

namespace WorkerThreads {

    using Task = std::function<void()>;
    inline std::deque<Task> tasks;
    inline std::mutex mtx;

    inline void RunOnMain(Task t) {
        if (ShutdownManager::instance().isShuttingDown()) {
            return;
        }

        std::lock_guard<std::mutex> lock(mtx);
        tasks.push_back(std::move(t));
    }

    inline void RunOnMainUpdate() {
        std::deque<Task> toRun;
        {
            std::lock_guard<std::mutex> lock(mtx);
            toRun.swap(tasks);
        }

        for (auto &t: toRun) {
            if (ShutdownManager::instance().isShuttingDown()) {
                break;
            }
            t();
        }
    }

    template<typename Func, typename... Args> void runBackground(Func &&f, Args &&...args) {
        if (ShutdownManager::instance().isShuttingDown()) {
            return;
        }

        std::thread t([fn = std::forward<Func>(f), tup = std::make_tuple(std::forward<Args>(args)...)]() mutable {
            if (ShutdownManager::instance().isShuttingDown()) {
                return;
            }
            std::apply(fn, tup);
        });

        ShutdownManager::instance().registerThread(std::move(t));
    }
} // namespace WorkerThreads
