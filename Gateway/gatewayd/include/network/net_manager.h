#pragma once

#include "config/config_manager.h"
#include "common/logger.h"
#include "network/i_network_provider.h"

#include <memory>
#include <vector>

namespace gateway::network {

class NetManager {
public:
    // 初始化网络提供者。这里只保存配置并创建 provider，不执行任何联网动作。
    bool init(const config::NetworkConfig &config, log::Logger *logger);

    // 根据配置选择可用网络。已有网络仍可用时只做轻量检测，避免每轮主循环
    // 都重新执行 DHCP / wpa_supplicant 等可能阻塞的脚本。
    NetworkState ensureNetwork();

private:
    // 根据 network.mode 和 priority 生成本轮候选 provider 列表。
    std::vector<INetworkProvider *> candidateProviders();

    INetworkProvider *findProvider(const NetworkState &state);

    // allow_bring_up 为 true 时允许执行拉起流程；保持现有网络时只做检测。
    bool checkProvider(INetworkProvider &provider, bool allow_bring_up, bool *cloud_reachable);

    int priorityFor(const std::string &name) const;
    void releaseCurrentProvider();

    config::NetworkConfig config_;
    log::Logger *logger_ = nullptr;
    NetworkState current_;
    std::vector<std::unique_ptr<INetworkProvider>> providers_;
};

} // namespace gateway::network
