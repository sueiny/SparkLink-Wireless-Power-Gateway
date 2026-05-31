#pragma once

#include "config/config_manager.h"
#include "network/i_network_provider.h"

namespace gateway::network {

// 以太网 provider：拉起接口并申请 DHCP。
class EthernetProvider : public INetworkProvider {
public:
    EthernetProvider(config::EthernetConfig config, int priority);

    NetworkType type() const override { return NetworkType::Ethernet; }
    const char *name() const override { return "ethernet"; }
    std::string ifname() const override { return config_.ifname; }
    int priority() const override { return priority_; }
    bool enabled() const override { return config_.enable; }

    bool bringUp() override;
    bool isInterfaceUp() const override;
    bool hasIp() const override;
    bool canReachCloud(const std::string &host, int port) const override;

private:
    config::EthernetConfig config_;
    int priority_ = 0;
};

} // namespace gateway::network
