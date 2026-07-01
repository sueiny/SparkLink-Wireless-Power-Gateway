#pragma once

#include "config/config_manager.h"
#include "network/i_network_provider.h"

#include <string>

namespace gateway::network {

// ML307 ECM/RNDIS provider：ttyUSB2 只做 AT 控制面，数据面稳定命名为 cell0。
class CellularProvider : public INetworkProvider {
public:
    CellularProvider(config::CellularConfig config, int priority);

    NetworkType type() const override { return NetworkType::Cellular; }
    const char *name() const override { return "cellular"; }
    std::string ifname() const override;
    int priority() const override { return priority_; }
    bool enabled() const override { return config_.enable; }

    bool bringUp(uint32_t route_metric = 0) override;
    bool isInterfaceUp() const override;
    bool hasIp() const override;
    bool canReachCloud(const std::string &host,
                       int port,
                       std::string *reason = nullptr) const override;

private:
    std::string ensureStableIfname() const;
    std::string findMl307Netdev() const;
    bool atControlReady() const;
    bool ensureHostDialup() const;
    bool isAllowedCellularIfname(const std::string &ifname) const;
    bool isMl307Netdev(const std::string &ifname) const;

    config::CellularConfig config_;
    int priority_ = 0;
};

} // namespace gateway::network
