#pragma once

#include "app/worker.h"

#include <atomic>
#include <thread>

namespace gateway::app {

// WorkerBase 只封装线程生命周期：start/stop/join。
// 具体工作逻辑由 Derived::run() 实现，避免每个 worker 重复写相同代码。
template <typename Derived>
class WorkerBase : public Worker {
public:
    void start() override
    {
        stop_.store(false);
        thread_ = std::thread(&Derived::run, static_cast<Derived *>(this));
    }

    void stop() override
    {
        stop_.store(true);
    }

    void join() override
    {
        if (thread_.joinable())
            thread_.join();
    }

protected:
    std::atomic_bool stop_{false};
    std::thread thread_;
};

} // namespace gateway::app
