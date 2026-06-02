#pragma once

#include <string>
#include <vector>

namespace gateway::network {

struct ProcessResult {
    int exit_code = -1;
    std::string stdout_output;
    bool timeout = false;
};

bool interfaceExists(const std::string &ifname);
bool interfaceIsUp(const std::string &ifname);
bool interfaceHasIpv4(const std::string &ifname);
bool tcpConnect(const std::string &host, int port, int timeout_ms);
// 绑定到指定接口的版本，测试时走该接口的路由
bool tcpConnectVia(const std::string &host, int port, int timeout_ms, const std::string &ifname);
ProcessResult runProcess(const std::vector<std::string> &args, int timeout_ms = 10000);
bool setInterfaceUp(const std::string &ifname);
bool requestDhcp(const std::string &ifname, int timeout_ms = 10000);
std::string firstUsableInterface(const std::vector<std::string> &ifnames);
// 设置默认路由走指定接口（通过该接口的现有网关）
bool setDefaultRouteVia(const std::string &ifname);

} // namespace gateway::network
