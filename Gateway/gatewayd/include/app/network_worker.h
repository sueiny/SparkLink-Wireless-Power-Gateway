#pragma once

#include "app/worker_base.h"
#include "common/logger.h"
#include "network/net_manager.h"

#include <mutex>

namespace gateway::app {

// 网络线程入口。
//
// NetManager::ensureNetwork() 可能触发 DHCP、wpa_supplicant、连通性检测等操作，
// 所以独立放到网络线程，发布线程只读取最近一次 NetworkState。
class NetworkWorker final : public WorkerBase<NetworkWorker> {
public:
    NetworkWorker(log::Logger &logger, network::NetManager &net_manager);

    const char *name() const override { return "network"; }

    // 返回最近一次检测到的网络状态。内部加锁，允许发布线程安全读取。
    network::NetworkState state() const;

private:
    friend class WorkerBase<NetworkWorker>;
    void run();

    log::Logger &logger_;
    network::NetManager &net_manager_;
    mutable std::mutex mutex_;
    network::NetworkState state_;
};

} // namespace gateway::app
