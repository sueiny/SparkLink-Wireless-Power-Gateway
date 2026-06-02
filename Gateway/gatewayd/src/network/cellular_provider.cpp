#include "network/cellular_provider.h"

#include "network/network_utils.h"

namespace gateway::network {

CellularProvider::CellularProvider(config::CellularConfig config, int priority)
    : config_(std::move(config)), priority_(priority)
{
}

std::string CellularProvider::ifname() const
{
    return firstUsableInterface(candidateIfnames());
}

bool CellularProvider::bringUp()
{
    return false;
}

bool CellularProvider::isInterfaceUp() const
{
    for (const auto &candidate : candidateIfnames()) {
        if (interfaceExists(candidate) && interfaceIsUp(candidate))
            return true;
    }
    return false;
}

bool CellularProvider::hasIp() const
{
    for (const auto &candidate : candidateIfnames()) {
        if (interfaceHasIpv4(candidate))
            return true;
    }
    return false;
}

bool CellularProvider::canReachCloud(const std::string &host, int port) const
{
    // 使用 ifname() 检测实际存在的接口（ppp0/usb0/wwan0），而非硬编码 ppp0
    std::string name = ifname();
    if (name.empty()) name = "ppp0";
    return defaultCanReachCloud(host, port, name);
}

std::vector<std::string> CellularProvider::candidateIfnames() const
{
    std::vector<std::string> names;
    names.push_back(config_.ifname.empty() ? "ppp0" : config_.ifname);

    for (const auto &fallback : {"ppp0", "usb0", "wwan0"}) {
        bool exists = false;
        for (const auto &name : names) {
            if (name == fallback) {
                exists = true;
                break;
            }
        }
        if (!exists)
            names.push_back(fallback);
    }

    return names;
}

} // namespace gateway::network
