#include "network/ethernet_provider.h"

#include "network/network_utils.h"

namespace gateway::network {

EthernetProvider::EthernetProvider(config::EthernetConfig config, int priority)
    : config_(std::move(config)), priority_(priority)
{
}

bool EthernetProvider::bringUp()
{
    if (!setInterfaceUp(config_.ifname))
        return false;
    return requestDhcp(config_.ifname, 10000);
}

bool EthernetProvider::isInterfaceUp() const
{
    return defaultIsInterfaceUp(config_.ifname);
}

bool EthernetProvider::hasIp() const
{
    return defaultHasIp(config_.ifname);
}

bool EthernetProvider::canReachCloud(const std::string &host, int port) const
{
    return defaultCanReachCloud(host, port, config_.ifname);
}

} // namespace gateway::network
