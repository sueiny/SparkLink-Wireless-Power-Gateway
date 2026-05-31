#pragma once

#include "config/config_manager.h"
#include "network/i_network_provider.h"

namespace gateway::network {

// Wi-Fi provider：生成临时 wpa_supplicant 配置，启动连接并申请 DHCP。
class WifiProvider : public INetworkProvider {
public:
    WifiProvider(config::WifiConfig config, int priority);

    NetworkType type() const override { return NetworkType::Wifi; }
    const char *name() const override { return "wifi"; }
    std::string ifname() const override { return config_.ifname; }
    int priority() const override { return priority_; }
    bool enabled() const override { return config_.enable; }

    bool bringUp() override;
    bool isInterfaceUp() const override;
    bool hasIp() const override;
    bool canReachCloud(const std::string &host, int port) const override;

private:
    config::WifiConfig config_;
    int priority_ = 0;
};

} // namespace gateway::network
