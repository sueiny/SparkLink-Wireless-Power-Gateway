#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gateway::network {

struct NetlinkRoute {
    std::string iface;
    std::string gateway;    // 点分十进制
    uint32_t metric;
    bool is_default;        // destination == 0.0.0.0
};

// Netlink Socket 路由管理（替代 fork+exec ip route）

// 获取所有默认路由
std::vector<NetlinkRoute> netlinkGetDefaultRoutes();

// 添加默认路由
bool netlinkAddDefaultRoute(const std::string &ifname, const std::string &gateway, uint32_t metric);

// 删除指定默认路由
bool netlinkDelDefaultRoute(const std::string &ifname, const std::string &gateway, uint32_t metric);

// 删除所有非指定接口的默认路由
bool netlinkDelOtherDefaultRoutes(const std::string &keep_ifname);

// 设置指定接口为默认路由（metric=100），删除其他接口的默认路由
bool netlinkSetDefaultRouteVia(const std::string &ifname);

} // namespace gateway::network
