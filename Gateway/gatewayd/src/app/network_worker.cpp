#include "app/network_worker.h"

#include "common/time_utils.h"

namespace gateway::app {
namespace {

constexpr int64_t kNetworkPollIntervalMs = 5000;

} // namespace

NetworkWorker::NetworkWorker(log::Logger &logger, network::NetManager &net_manager)
    : logger_(logger), net_manager_(net_manager)
{
}

network::NetworkState NetworkWorker::state() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

void NetworkWorker::run()
{
    while (!stop_.load()) {
        const auto state = net_manager_.ensureNetwork();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            state_ = state;
        }

        common::interruptibleSleep(stop_, kNetworkPollIntervalMs);
    }

    logger_.info("APP", "network worker stopped");
}

} // namespace gateway::app
