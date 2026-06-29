#include "app/network_worker.h"

#include "common/time_utils.h"

#include <algorithm>
#include <chrono>

namespace gateway::app {
namespace {

constexpr int kMinRetryMs = 1000;
constexpr int kMaxRetryMs = 300000;

} // namespace

NetworkWorker::NetworkWorker(log::Logger &logger, network::NetManager &net_manager)
    : logger_(logger), net_manager_(net_manager)
{
}

void NetworkWorker::stop()
{
    stop_.store(true);
    control_cv_.notify_all();
}

network::NetworkState NetworkWorker::state() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

void NetworkWorker::run()
{
    while (!stop_.load()) {
        const network::NetworkState state = net_manager_.ensureNetwork();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_ = state;
        }

        const int wait_ms = std::clamp(
            state.retry_delay_ms > 0 ? state.retry_delay_ms : 5000,
            kMinRetryMs,
            kMaxRetryMs);

        std::unique_lock<std::mutex> lock(control_mutex_);
        control_cv_.wait_for(lock, std::chrono::milliseconds(wait_ms), [&] {
            return stop_.load();
        });
    }

    logger_.info("APP", "network worker stopped");
}

} // namespace gateway::app
