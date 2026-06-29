#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace gateway::network {

struct ProcessResult {
    int exit_code = -1;
    std::string stdout_output;
    bool timeout = false;
};

struct TcpConnectResult {
    bool ok = false;
    std::string reason;
};

struct InterfaceLeaseInfo {
    std::string ifname;
    std::string gateway;
    std::vector<std::string> nameservers;
    bool has_ipv4 = false;
};

constexpr uint32_t kEthernetRouteMetric = 100;
constexpr uint32_t kWifiRouteMetric = 200;
constexpr uint32_t kCellularRouteMetric = 300;

bool interfaceExists(const std::string &ifname);
bool interfaceIsUp(const std::string &ifname);
std::string interfaceOperState(const std::string &ifname);
bool interfaceHasIpv4(const std::string &ifname);
std::string interfaceIpv4Address(const std::string &ifname);
uint32_t defaultRouteMetricForIfname(const std::string &ifname);
bool tcpConnect(const std::string &host, int port, int timeout_ms);
TcpConnectResult tcpConnectDetailed(const std::string &host, int port, int timeout_ms);
// 绑定到指定接口的版本，测试时走该接口的路由
bool tcpConnectVia(const std::string &host, int port, int timeout_ms, const std::string &ifname);
TcpConnectResult tcpConnectViaDetailed(const std::string &host,
                                       int port,
                                       int timeout_ms,
                                       const std::string &ifname);
ProcessResult runProcess(const std::vector<std::string> &args, int timeout_ms = 10000);
bool setInterfaceUp(const std::string &ifname);
bool setInterfaceDown(const std::string &ifname);
bool requestDhcp(const std::string &ifname,
                 int timeout_ms = 10000,
                 uint32_t route_metric = 0);
bool readTextFile(const std::string &path, std::string *text);
bool writeTextFile(const std::string &path, const std::string &text);
std::string firstUsableInterface(const std::vector<std::string> &ifnames);
std::string firstNameserverLine();
std::string defaultRouteSummary();
bool defaultRouteActiveOn(const std::string &ifname, uint32_t route_metric = 0);
bool defaultRoutePresentOn(const std::string &ifname, uint32_t route_metric = 0);
// 设置默认路由走指定接口（通过该接口的现有网关）
bool setDefaultRouteVia(const std::string &ifname, uint32_t route_metric = 0);
bool readInterfaceLeaseInfo(const std::string &ifname,
                            InterfaceLeaseInfo *info,
                            std::string *reason = nullptr);
bool writeResolvConfForInterface(const std::string &ifname,
                                 const InterfaceLeaseInfo &lease_info,
                                 std::string *reason = nullptr);
bool dnsAlignedToInterface(const std::string &ifname,
                           const InterfaceLeaseInfo &lease_info,
                           std::string *reason = nullptr);
bool reconcileInterfaceNetwork(const std::string &ifname,
                               uint32_t route_metric,
                               InterfaceLeaseInfo *lease_info = nullptr,
                               std::string *reason = nullptr);
bool prepareInterfaceNetworkProbe(const std::string &ifname,
                                  uint32_t route_metric,
                                  InterfaceLeaseInfo *lease_info = nullptr,
                                  std::string *reason = nullptr);

} // namespace gateway::network
