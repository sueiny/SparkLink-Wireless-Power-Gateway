#pragma once

#include <string>

namespace gateway::network {

enum class NetworkType {
    None,
    Ethernet,
    Wifi,
    Cellular,
};

struct NetworkState {
    NetworkType type = NetworkType::None;
    std::string name = "none";
    std::string ifname = "none";
    bool cloud_reachable = false;
    bool available = false;
};

// 单一网络类型的适配器接口。
// Ethernet/Wi-Fi/Cellular 各自负责“如何拉起”，NetManager 只负责选择和排序。
class INetworkProvider {
public:
    virtual ~INetworkProvider() = default;

    virtual NetworkType type() const = 0;
    virtual const char *name() const = 0;
    virtual std::string ifname() const = 0;
    virtual int priority() const = 0;
    virtual bool enabled() const = 0;

    virtual bool bringUp() = 0;
    virtual bool isInterfaceUp() const = 0;
    virtual bool hasIp() const = 0;
    virtual bool canReachCloud(const std::string &host, int port) const = 0;

protected:
    // 默认工具函数放在接口基类里，避免各 provider 重复写 Linux 网络检测逻辑。
    bool defaultIsInterfaceUp(const std::string &ifname) const;
    bool defaultHasIp(const std::string &ifname) const;
    bool defaultCanReachCloud(const std::string &host, int port) const;
    bool defaultCanReachCloud(const std::string &host, int port, const std::string &ifname) const;
};

std::string toString(NetworkType type);

} // namespace gateway::network
