#pragma once

#include "config/config_manager.h"
#include "common/logger.h"
#include "network/i_network_provider.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gateway::network {

class WifiProvider;

class NetManager {
public:
    // 初始化网络提供者。这里只保存配置并创建 provider，不执行任何联网动作。
    bool init(const config::NetworkConfig &config, log::Logger *logger);

    // 根据配置选择可用网络。已有网络仍可用时只做轻量检测，避免每轮主循环
    // 都重新执行 DHCP / wpa_supplicant 等可能阻塞的脚本。
    NetworkState ensureNetwork();

private:
    NetworkState ensureFixedModeNetwork();
    NetworkState ensureAutoModeNetwork();

    // 根据 network.mode 和 priority 生成本轮候选 provider 列表。
    std::vector<INetworkProvider *> candidateProviders();

    INetworkProvider *findProvider(const NetworkState &state);
    INetworkProvider *findFixedModeProvider();

    // allow_bring_up 为 true 时允许执行拉起流程；保持现有网络时只做检测。
    bool checkProvider(INetworkProvider &provider,
                       bool allow_bring_up,
                       const std::string &context,
                       uint32_t route_metric,
                       NetworkState *observed_state,
                       bool *cloud_reachable,
                       std::string *reason);
    bool reconcileProviderNetwork(const INetworkProvider &provider,
                                  const std::string &context,
                                  NetworkState *observed_state,
                                  std::string *reason) const;
    bool prepareProviderProbeNetwork(const INetworkProvider &provider,
                                     const std::string &context,
                                     uint32_t route_metric,
                                     NetworkState *observed_state,
                                     std::string *reason) const;
    void removeProviderProbeRoute(const INetworkProvider &provider,
                                  uint32_t route_metric,
                                  const std::string &context) const;
    void restoreProviderNetwork(const INetworkProvider *provider, const std::string &context) const;
    NetworkState snapshotProviderState(INetworkProvider &provider);
    void applyCloudObservation(const INetworkProvider &provider,
                               bool cloud_reachable,
                               const std::string &cloud_reason,
                               NetworkState *state) const;
    bool recordPreemptSuccess(const INetworkProvider &provider);
    void resetPreemptCandidate();

    int priorityFor(const std::string &name) const;
    uint32_t routeMetricFor(const INetworkProvider &provider) const;
    uint32_t probeRouteMetricFor(const INetworkProvider &provider) const;
    bool refreshDefaultRoute(const INetworkProvider &provider, const std::string &context);
    void releaseCurrentProvider();

    config::NetworkConfig config_;
    log::Logger *logger_ = nullptr;
    NetworkState current_;
    std::vector<std::unique_ptr<INetworkProvider>> providers_;
    WifiProvider *wifi_provider_ = nullptr;
    int current_check_failures_ = 0;
    std::string preempt_candidate_name_;
    std::string preempt_candidate_ifname_;
    int preempt_success_count_ = 0;
    int64_t preempt_candidate_since_ms_ = 0;
};

} // namespace gateway::network
