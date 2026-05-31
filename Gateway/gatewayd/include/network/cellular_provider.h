#pragma once

#include "config/config_manager.h"
#include "network/i_network_provider.h"

#include <string>
#include <vector>

namespace gateway::network {

// 4G provider 当前只保留空框架。
// 后续接 AT/PPP 时在 bringUp() 中实现真实拨号流程。
class CellularProvider : public INetworkProvider {
public:
    CellularProvider(config::CellularConfig config, int priority);

    NetworkType type() const override { return NetworkType::Cellular; }
    const char *name() const override { return "cellular"; }
    std::string ifname() const override;
    int priority() const override { return priority_; }
    bool enabled() const override { return config_.enable; }

    bool bringUp() override;
    bool isInterfaceUp() const override;
    bool hasIp() const override;
    bool canReachCloud(const std::string &host, int port) const override;

private:
    std::vector<std::string> candidateIfnames() const;

    config::CellularConfig config_;
    int priority_ = 0;
};

} // namespace gateway::network
