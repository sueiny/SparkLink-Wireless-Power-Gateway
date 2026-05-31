#include "network/i_network_provider.h"

#include "network/network_utils.h"

namespace gateway::network {

std::string toString(NetworkType type)
{
    switch (type) {
    case NetworkType::Ethernet:
        return "ethernet";
    case NetworkType::Wifi:
        return "wifi";
    case NetworkType::Cellular:
        return "cellular";
    case NetworkType::None:
    default:
        return "none";
    }
}

bool INetworkProvider::defaultIsInterfaceUp(const std::string &ifname) const
{
    return interfaceExists(ifname) && interfaceIsUp(ifname);
}

bool INetworkProvider::defaultHasIp(const std::string &ifname) const
{
    return interfaceHasIpv4(ifname);
}

bool INetworkProvider::defaultCanReachCloud(const std::string &host, int port) const
{
    return tcpConnect(host, port, 3000);
}

} // namespace gateway::network
