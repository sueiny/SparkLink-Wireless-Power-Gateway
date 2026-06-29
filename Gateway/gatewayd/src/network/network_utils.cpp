#include "network/network_utils.h"
#include "network/netlink_utils.h"

#include <arpa/inet.h>
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <ifaddrs.h>
#include <netdb.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <set>
#include <sstream>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/ioctl.h>

namespace gateway::network {

namespace {

bool startsWith(const std::string &text, const char *prefix)
{
    const size_t prefix_len = std::strlen(prefix);
    return text.size() >= prefix_len && text.compare(0, prefix_len, prefix) == 0;
}

std::string joinRoutes(const std::vector<NetlinkRoute> &routes)
{
    if (routes.empty())
        return "none";

    std::ostringstream oss;
    for (size_t i = 0; i < routes.size(); ++i) {
        if (i > 0)
            oss << "; ";
        oss << routes[i].iface << " via " << routes[i].gateway
            << " metric=" << routes[i].metric;
    }
    return oss.str();
}

bool waitDefaultRoutePresentOn(const std::string &ifname, uint32_t route_metric)
{
    for (int i = 0; i < 3; ++i) {
        if (defaultRoutePresentOn(ifname, route_metric))
            return true;
        ::usleep(100 * 1000);
    }
    return false;
}

bool waitDefaultRouteActiveOn(const std::string &ifname, uint32_t route_metric)
{
    for (int i = 0; i < 3; ++i) {
        if (defaultRouteActiveOn(ifname, route_metric))
            return true;
        ::usleep(100 * 1000);
    }
    return false;
}

std::string trimRight(std::string text)
{
    while (!text.empty() &&
           (text.back() == '\n' || text.back() == '\r' || text.back() == ' ' || text.back() == '\t'))
        text.pop_back();
    return text;
}

std::string trimCopy(const std::string &text)
{
    size_t start = 0;
    size_t end = text.size();
    while (start < end &&
           (text[start] == ' ' || text[start] == '\t' || text[start] == '\r' || text[start] == '\n'))
        ++start;
    while (end > start &&
           (text[end - 1] == ' ' || text[end - 1] == '\t' ||
            text[end - 1] == '\r' || text[end - 1] == '\n'))
        --end;
    return text.substr(start, end - start);
}

bool isIpv4Text(const std::string &text)
{
    in_addr addr {};
    return ::inet_pton(AF_INET, text.c_str(), &addr) == 1;
}

std::string ipv4HostOrderToText(uint32_t ip)
{
    in_addr addr {};
    addr.s_addr = htonl(ip);
    char buf[INET_ADDRSTRLEN] = {};
    if (::inet_ntop(AF_INET, &addr, buf, sizeof(buf)) == nullptr)
        return {};
    return buf;
}

std::vector<std::string> splitWhitespace(const std::string &text)
{
    std::istringstream in(text);
    std::vector<std::string> out;
    std::string token;
    while (in >> token)
        out.push_back(token);
    return out;
}

bool parseLeaseOutput(const std::string &ifname,
                      const std::string &text,
                      InterfaceLeaseInfo *info)
{
    if (!info)
        return false;

    InterfaceLeaseInfo parsed;
    parsed.ifname = ifname;
    parsed.has_ipv4 = interfaceHasIpv4(ifname);

    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const std::string trimmed = trimCopy(line);
        if (trimmed.empty())
            continue;

        const size_t eq = trimmed.find('=');
        if (eq == std::string::npos)
            continue;

        const std::string key = trimCopy(trimmed.substr(0, eq));
        std::string value = trimCopy(trimmed.substr(eq + 1));
        if (!value.empty() && value.front() == '\'' && value.back() == '\'' && value.size() >= 2)
            value = value.substr(1, value.size() - 2);
        if (!value.empty() && value.front() == '"' && value.back() == '"' && value.size() >= 2)
            value = value.substr(1, value.size() - 2);

        if ((key == "new_routers" || key == "routers") && parsed.gateway.empty()) {
            for (const auto &token : splitWhitespace(value)) {
                if (isIpv4Text(token)) {
                    parsed.gateway = token;
                    break;
                }
            }
            continue;
        }

        if (key == "new_domain_name_servers" || key == "domain_name_servers") {
            std::set<std::string> unique(parsed.nameservers.begin(), parsed.nameservers.end());
            for (const auto &token : splitWhitespace(value)) {
                if (!isIpv4Text(token))
                    continue;
                if (unique.insert(token).second)
                    parsed.nameservers.push_back(token);
            }
        }
    }

    *info = std::move(parsed);
    return true;
}

void mergeFallbackDefaultRoute(const std::string &ifname, InterfaceLeaseInfo *info)
{
    if (!info || !info->gateway.empty())
        return;

    for (const auto &route : netlinkGetDefaultRoutes()) {
        if (route.iface == ifname && !route.gateway.empty()) {
            info->gateway = route.gateway;
            return;
        }
    }
}

void mergeFallbackConnectedGateway(const std::string &ifname, InterfaceLeaseInfo *info)
{
    if (!info || !info->gateway.empty())
        return;

    ifaddrs *addrs = nullptr;
    if (::getifaddrs(&addrs) != 0)
        return;

    for (ifaddrs *it = addrs; it; it = it->ifa_next) {
        if (!it->ifa_addr || !it->ifa_netmask || ifname != it->ifa_name)
            continue;
        if (it->ifa_addr->sa_family != AF_INET)
            continue;

        const auto *addr = reinterpret_cast<const sockaddr_in *>(it->ifa_addr);
        const auto *mask = reinterpret_cast<const sockaddr_in *>(it->ifa_netmask);
        const uint32_t ip = ntohl(addr->sin_addr.s_addr);
        const uint32_t netmask = ntohl(mask->sin_addr.s_addr);
        if (ip == 0 || netmask == 0 || (ip & 0xFFFF0000U) == 0xA9FE0000U)
            continue;

        const uint32_t network = ip & netmask;
        const uint32_t gateway = network + 1;
        if (gateway == ip || gateway == 0)
            continue;

        info->gateway = ipv4HostOrderToText(gateway);
        if (!info->gateway.empty())
            break;
    }

    ::freeifaddrs(addrs);
}

std::string nameserverSummary(const std::vector<std::string> &nameservers)
{
    if (nameservers.empty())
        return "none";

    std::ostringstream oss;
    for (size_t i = 0; i < nameservers.size(); ++i) {
        if (i > 0)
            oss << ',';
        oss << nameservers[i];
    }
    return oss.str();
}

} // namespace

bool interfaceExists(const std::string &ifname)//检查接口是否存在
{
    struct stat st {};
    const std::string path = "/sys/class/net/" + ifname;// /sys/class/net/ 是Linux sysfs虚拟文件系统中的网络接口目录
    return ::stat(path.c_str(), &st) == 0;
}

bool interfaceIsUp(const std::string &ifname)
{
    const std::string state = interfaceOperState(ifname);
    return state == "up" || state == "unknown";
}

std::string interfaceOperState(const std::string &ifname)
{
    std::ifstream in("/sys/class/net/" + ifname + "/operstate");
    std::string state;
    in >> state;
    return state;
}

bool interfaceHasIpv4(const std::string &ifname)  //
{
    ifaddrs *addrs = nullptr;
    if (::getifaddrs(&addrs) != 0)
        return false;

    bool found = false;
    for (ifaddrs *it = addrs; it; it = it->ifa_next) {
        if (!it->ifa_addr || ifname != it->ifa_name)
            continue;
        if (it->ifa_addr->sa_family == AF_INET) {
            const auto *addr = reinterpret_cast<const sockaddr_in *>(it->ifa_addr);
            const uint32_t ip = ntohl(addr->sin_addr.s_addr);
            // 169.254/16 is link-local fallback, not a usable cloud route.
            if ((ip & 0xFFFF0000U) == 0xA9FE0000U)
                continue;
            found = true;
            break;
        }
    }

    ::freeifaddrs(addrs);
    return found;
}

std::string interfaceIpv4Address(const std::string &ifname)
{
    ifaddrs *addrs = nullptr;
    if (::getifaddrs(&addrs) != 0)
        return {};

    std::string result;
    for (ifaddrs *it = addrs; it; it = it->ifa_next) {
        if (!it->ifa_addr || ifname != it->ifa_name)
            continue;
        if (it->ifa_addr->sa_family != AF_INET)
            continue;

        const auto *addr = reinterpret_cast<const sockaddr_in *>(it->ifa_addr);
        const uint32_t ip = ntohl(addr->sin_addr.s_addr);
        if ((ip & 0xFFFF0000U) == 0xA9FE0000U)
            continue;

        char buffer[INET_ADDRSTRLEN] = {};
        if (::inet_ntop(AF_INET, &addr->sin_addr, buffer, sizeof(buffer)) != nullptr) {
            result = buffer;
            break;
        }
    }

    ::freeifaddrs(addrs);
    return result;
}

uint32_t defaultRouteMetricForIfname(const std::string &ifname)
{
    if (startsWith(ifname, "eth") || startsWith(ifname, "en"))
        return kEthernetRouteMetric;
    if (startsWith(ifname, "wlan") || startsWith(ifname, "wl"))
        return kWifiRouteMetric;
    if (startsWith(ifname, "ppp") || startsWith(ifname, "usb") || startsWith(ifname, "wwan"))
        return kCellularRouteMetric;
    return kCellularRouteMetric;
}

TcpConnectResult tcpConnectDetailedImpl(const std::string &host,
                                        int port,
                                        int timeout_ms,
                                        const std::string &bind_ifname)
{
    TcpConnectResult result;
    if (host.empty() || port <= 0) {
        result.reason = "invalid endpoint";
        return result;
    }

    addrinfo hints {};
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_INET;

    addrinfo *res = nullptr;
    const std::string port_text = std::to_string(port);

    // DNS 解析：dhcpcd 重写 resolv.conf 时 nameserver 会暂时消失。
    // 等待最多 15 秒（75×200ms），之后返回 false，由 ensureNetwork 下一轮重试。
    int gai_ret = EAI_AGAIN;
    bool observed_nameserver = false;
    for (int retry = 0; retry < 75 && gai_ret != 0; ++retry) {
        bool has_ns = false;
        {
            std::ifstream check("/etc/resolv.conf");
            std::string line;
            while (std::getline(check, line)) {
                if (line.find("nameserver") == 0) {
                    has_ns = true;
                    break;
                }
            }
        }
        if (has_ns)
            observed_nameserver = true;
        if (!has_ns) {
            ::usleep(200000);
            continue;
        }
        gai_ret = ::getaddrinfo(host.c_str(), port_text.c_str(), &hints, &res);
        if (gai_ret != 0)
            ::usleep(200000);
    }
    if (gai_ret != 0) {
        result.reason = observed_nameserver
                            ? std::string("getaddrinfo failed: ") + ::gai_strerror(gai_ret)
                            : "no nameserver in /etc/resolv.conf";
        return result;
    }

    std::string last_reason;
    for (addrinfo *it = res; it && !result.ok; it = it->ai_next) {
        const int fd = ::socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd < 0) {
            last_reason = std::string("socket failed: ") + std::strerror(errno);
            continue;
        }

        if (!bind_ifname.empty()) {
            const int bind_ret = ::setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE,
                                              bind_ifname.c_str(),
                                              static_cast<socklen_t>(bind_ifname.size() + 1));
            if (bind_ret != 0) {
                last_reason = std::string("SO_BINDTODEVICE failed: ") + std::strerror(errno);
                ::close(fd);
                continue;
            }
        }

        const int flags = ::fcntl(fd, F_GETFL, 0);
        if (flags >= 0)
            ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);

        const int rc = ::connect(fd, it->ai_addr, it->ai_addrlen);
        if (rc == 0) {
            result.ok = true;
        } else if (errno == EINPROGRESS) {
            pollfd pfd {};
            pfd.fd = fd;
            pfd.events = POLLOUT;
            const int pr = ::poll(&pfd, 1, timeout_ms);
            if (pr > 0) {
                int err = 0;
                socklen_t len = sizeof(err);
                if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) == 0 && err == 0) {
                    result.ok = true;
                } else if (err != 0) {
                    last_reason = std::string("connect failed: ") + std::strerror(err);
                }
            } else if (pr == 0) {
                last_reason = "connect timeout";
            } else {
                last_reason = std::string("poll failed: ") + std::strerror(errno);
            }
        } else {
            last_reason = std::string("connect failed: ") + std::strerror(errno);
        }

        ::close(fd);
    }

    ::freeaddrinfo(res);
    if (!result.ok)
        result.reason = last_reason.empty() ? "connect failed" : last_reason;
    return result;
}

bool tcpConnectVia(const std::string &host, int port, int timeout_ms, const std::string &ifname)
{
    return tcpConnectViaDetailed(host, port, timeout_ms, ifname).ok;
}

TcpConnectResult tcpConnectDetailed(const std::string &host, int port, int timeout_ms)
{
    return tcpConnectDetailedImpl(host, port, timeout_ms, "");
}

TcpConnectResult tcpConnectViaDetailed(const std::string &host,
                                       int port,
                                       int timeout_ms,
                                       const std::string &ifname)
{
    if (ifname.empty())
        return tcpConnectDetailed(host, port, timeout_ms);

    return tcpConnectDetailedImpl(host, port, timeout_ms, ifname);
}

bool tcpConnect(const std::string &host, int port, int timeout_ms)
{
    return tcpConnectDetailed(host, port, timeout_ms).ok;
}

bool setDefaultRouteVia(const std::string &ifname, uint32_t route_metric)
{
    if (route_metric == 0)
        route_metric = defaultRouteMetricForIfname(ifname);

    // 使用 Netlink Socket 直接管理路由（不 fork 进程）
    return netlinkSetDefaultRouteVia(ifname, route_metric);
}

ProcessResult runProcess(const std::vector<std::string> &args, int timeout_ms)
{
    ProcessResult result;
    if (args.empty())
        return result;

    int pipe_fd[2] = {-1, -1};
    // 使用 O_CLOEXEC 确保 pipe fd 不会被子进程继承
    if (::pipe2(pipe_fd, O_CLOEXEC) != 0)
        return result;

    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(pipe_fd[0]);
        ::close(pipe_fd[1]);
        return result;
    }

    if (pid == 0) {
        // 子进程：关闭所有非标准 fd，避免继承到 exec 的进程中
        // 这样 wpa_supplicant 等子进程不会持有 IPC socket 等 fd
        int max_fd = sysconf(_SC_OPEN_MAX);
        if (max_fd < 0) max_fd = 1024;  // 保守上限
        for (int fd = 3; fd < max_fd; fd++) {
            if (fd != pipe_fd[1]) {
                ::close(fd);
            }
        }

        ::dup2(pipe_fd[1], STDOUT_FILENO);
        ::dup2(pipe_fd[1], STDERR_FILENO);
        ::close(pipe_fd[0]);
        ::close(pipe_fd[1]);

        std::vector<char *> argv;
        argv.reserve(args.size() + 1);
        for (const auto &arg : args)
            argv.push_back(const_cast<char *>(arg.c_str()));
        argv.push_back(nullptr);
        ::execvp(argv[0], argv.data());
        _exit(127);
    }

    ::close(pipe_fd[1]);
    const int flags = ::fcntl(pipe_fd[0], F_GETFL, 0);
    ::fcntl(pipe_fd[0], F_SETFL, flags | O_NONBLOCK);

    const int step_ms = 50;
    int waited_ms = 0;
    int status = 0;
    bool exited = false;

    while (waited_ms <= timeout_ms) {
        char buffer[256];
        while (true) {
            const ssize_t n = ::read(pipe_fd[0], buffer, sizeof(buffer));
            if (n > 0)
                result.stdout_output.append(buffer, static_cast<size_t>(n));
            else if (n < 0 && errno == EINTR)
                continue;
            else
                break;
        }

        const pid_t rc = ::waitpid(pid, &status, WNOHANG);
        if (rc == pid) {
            exited = true;
            break;
        }

        ::usleep(step_ms * 1000);
        waited_ms += step_ms;
    }

    if (!exited) {
        result.timeout = true;
        ::kill(pid, SIGKILL);
        ::waitpid(pid, &status, 0);
    }

    char buffer[256];
    while (true) {
        const ssize_t n = ::read(pipe_fd[0], buffer, sizeof(buffer));
        if (n > 0)
            result.stdout_output.append(buffer, static_cast<size_t>(n));
        else if (n < 0 && errno == EINTR)
            continue;
        else
            break;
    }
    ::close(pipe_fd[0]);

    if (exited && WIFEXITED(status))
        result.exit_code = WEXITSTATUS(status);
    return result;
}

bool setInterfaceUp(const std::string &ifname)
{
    if (ifname.empty())
        return false;

    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return false;

    ifreq req {};
    std::snprintf(req.ifr_name, sizeof(req.ifr_name), "%s", ifname.c_str());
    bool ok = false;
    if (::ioctl(fd, SIOCGIFFLAGS, &req) == 0) {
        req.ifr_flags |= IFF_UP;
        ok = ::ioctl(fd, SIOCSIFFLAGS, &req) == 0;
    }
    ::close(fd);
    return ok;
}

bool setInterfaceDown(const std::string &ifname)
{
    if (ifname.empty())
        return false;

    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0)
        return false;

    ifreq req {};
    std::snprintf(req.ifr_name, sizeof(req.ifr_name), "%s", ifname.c_str());
    bool ok = false;
    if (::ioctl(fd, SIOCGIFFLAGS, &req) == 0) {
        req.ifr_flags &= static_cast<short>(~IFF_UP);
        ok = ::ioctl(fd, SIOCSIFFLAGS, &req) == 0;
    }
    ::close(fd);
    return ok;
}

bool requestDhcp(const std::string &ifname, int timeout_ms, uint32_t route_metric)
{
    if (ifname.empty())
        return false;

    if (route_metric == 0)
        route_metric = defaultRouteMetricForIfname(ifname);

    if (interfaceHasIpv4(ifname))
        return true;

    const auto dhcpcd_pid = runProcess({"pidof", "dhcpcd"}, 1000);
    if (!dhcpcd_pid.timeout && dhcpcd_pid.exit_code == 0) {
        const auto metric = runProcess({"dhcpcd", "-m", std::to_string(route_metric), ifname},
                                       timeout_ms);
        if (metric.timeout || metric.exit_code != 0)
            return false;
        const auto renew = runProcess({"dhcpcd", "-n", ifname}, timeout_ms);
        if (renew.timeout || renew.exit_code != 0)
            return false;
    } else {
        const auto result = runProcess({"udhcpc", "-i", ifname, "-n", "-q"}, timeout_ms);
        if (result.timeout || result.exit_code != 0)
            return false;
    }

    const int step_ms = 500;
    int waited_ms = 0;
    while (waited_ms <= timeout_ms) {
        if (interfaceHasIpv4(ifname))
            return true;
        ::usleep(step_ms * 1000);
        waited_ms += step_ms;
    }
    return false;
}

bool defaultRouteActiveOn(const std::string &ifname, uint32_t route_metric)
{
    if (ifname.empty())
        return false;

    if (route_metric == 0)
        route_metric = defaultRouteMetricForIfname(ifname);

    const auto routes = netlinkGetDefaultRoutes();
    bool found_target = false;
    uint32_t min_metric = UINT32_MAX;
    for (const auto &route : routes) {
        if (!route.is_default)
            continue;
        min_metric = std::min(min_metric, route.metric);
        if (route.iface == ifname && route.metric == route_metric)
            found_target = true;
    }
    if (!found_target)
        return false;
    return min_metric == route_metric;
}

bool defaultRoutePresentOn(const std::string &ifname, uint32_t route_metric)
{
    if (ifname.empty())
        return false;

    if (route_metric == 0)
        route_metric = defaultRouteMetricForIfname(ifname);

    for (const auto &route : netlinkGetDefaultRoutes()) {
        if (!route.is_default)
            continue;
        if (route.iface == ifname && route.metric == route_metric)
            return true;
    }
    return false;
}

bool readInterfaceLeaseInfo(const std::string &ifname,
                            InterfaceLeaseInfo *info,
                            std::string *reason)
{
    if (info)
        *info = {};
    if (reason)
        reason->clear();

    if (ifname.empty()) {
        if (reason)
            *reason = "empty interface";
        return false;
    }

    InterfaceLeaseInfo parsed;
    parsed.ifname = ifname;
    parsed.has_ipv4 = interfaceHasIpv4(ifname);

    const auto lease = runProcess({"dhcpcd", "-U", ifname}, 3000);
    if (!lease.timeout && lease.exit_code == 0) {
        parseLeaseOutput(ifname, lease.stdout_output, &parsed);
    }

    mergeFallbackDefaultRoute(ifname, &parsed);
    mergeFallbackConnectedGateway(ifname, &parsed);
    if (parsed.nameservers.empty() && !parsed.gateway.empty())
        parsed.nameservers.push_back(parsed.gateway);

    if (!parsed.has_ipv4) {
        if (reason)
            *reason = "no IPv4 on " + ifname;
        if (info)
            *info = parsed;
        return false;
    }

    if (parsed.gateway.empty()) {
        if (reason)
            *reason = "no gateway for " + ifname;
        if (info)
            *info = parsed;
        return false;
    }

    if (parsed.nameservers.empty()) {
        if (reason)
            *reason = "no nameservers for " + ifname;
        if (info)
            *info = parsed;
        return false;
    }

    if (info)
        *info = parsed;
    return true;
}

bool writeResolvConfForInterface(const std::string &ifname,
                                 const InterfaceLeaseInfo &lease_info,
                                 std::string *reason)
{
    if (reason)
        reason->clear();

    if (ifname.empty()) {
        if (reason)
            *reason = "empty interface";
        return false;
    }

    if (lease_info.nameservers.empty()) {
        if (reason)
            *reason = "no nameservers for " + ifname;
        return false;
    }

    std::ostringstream out;
    out << "# Generated by gatewayd for " << ifname << "\n";
    for (const auto &ns : lease_info.nameservers)
        out << "nameserver " << ns << "\n";

    if (!writeTextFile("/etc/resolv.conf", out.str())) {
        if (reason)
            *reason = "failed to write /etc/resolv.conf";
        return false;
    }
    return true;
}

bool dnsAlignedToInterface(const std::string &ifname,
                           const InterfaceLeaseInfo &lease_info,
                           std::string *reason)
{
    if (reason)
        reason->clear();

    if (ifname.empty()) {
        if (reason)
            *reason = "empty interface";
        return false;
    }

    if (lease_info.nameservers.empty()) {
        if (reason)
            *reason = "no nameservers for " + ifname;
        return false;
    }

    std::string text;
    if (!readTextFile("/etc/resolv.conf", &text)) {
        if (reason)
            *reason = "cannot read /etc/resolv.conf";
        return false;
    }

    std::vector<std::string> current;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const std::string trimmed = trimCopy(line);
        if (trimmed.rfind("nameserver ", 0) != 0)
            continue;
        const std::string addr = trimCopy(trimmed.substr(std::strlen("nameserver ")));
        if (isIpv4Text(addr))
            current.push_back(addr);
    }

    if (current == lease_info.nameservers)
        return true;

    if (reason) {
        *reason = "expected=" + nameserverSummary(lease_info.nameservers) +
                  ", current=" + nameserverSummary(current);
    }
    return false;
}

bool reconcileInterfaceNetwork(const std::string &ifname,
                               uint32_t route_metric,
                               InterfaceLeaseInfo *lease_info,
                               std::string *reason)
{
    if (lease_info)
        *lease_info = {};
    if (reason)
        reason->clear();

    if (ifname.empty()) {
        if (reason)
            *reason = "empty interface";
        return false;
    }

    if (route_metric == 0)
        route_metric = defaultRouteMetricForIfname(ifname);

    InterfaceLeaseInfo parsed;
    std::string lease_reason;
    if (!readInterfaceLeaseInfo(ifname, &parsed, &lease_reason)) {
        if (reason)
            *reason = lease_reason;
        if (lease_info)
            *lease_info = parsed;
        return false;
    }

    bool route_active = setDefaultRouteVia(ifname, route_metric) &&
                        waitDefaultRouteActiveOn(ifname, route_metric);
    if (!route_active && !parsed.gateway.empty()) {
        (void)netlinkAddDefaultRoute(ifname, parsed.gateway, route_metric);
        if (waitDefaultRoutePresentOn(ifname, route_metric)) {
            netlinkDelOtherDefaultRoutes(ifname, route_metric);
            route_active = waitDefaultRouteActiveOn(ifname, route_metric);
        }
    }

    if (!route_active) {
        if (reason)
            *reason = "default route is not active on " + ifname +
                      ", routes=" + defaultRouteSummary();
        if (lease_info)
            *lease_info = parsed;
        return false;
    }

    std::string dns_reason;
    if (!dnsAlignedToInterface(ifname, parsed, &dns_reason)) {
        if (!writeResolvConfForInterface(ifname, parsed, &dns_reason) ||
            !dnsAlignedToInterface(ifname, parsed, &dns_reason)) {
            if (reason)
                *reason = dns_reason.empty() ? "DNS_NOT_READY" : dns_reason;
            if (lease_info)
                *lease_info = parsed;
            return false;
        }
    }

    if (lease_info)
        *lease_info = parsed;
    return true;
}

bool prepareInterfaceNetworkProbe(const std::string &ifname,
                                  uint32_t route_metric,
                                  InterfaceLeaseInfo *lease_info,
                                  std::string *reason)
{
    if (lease_info)
        *lease_info = {};
    if (reason)
        reason->clear();

    if (ifname.empty()) {
        if (reason)
            *reason = "empty interface";
        return false;
    }

    if (route_metric == 0)
        route_metric = defaultRouteMetricForIfname(ifname);

    InterfaceLeaseInfo parsed;
    std::string lease_reason;
    if (!readInterfaceLeaseInfo(ifname, &parsed, &lease_reason)) {
        if (reason)
            *reason = lease_reason;
        if (lease_info)
            *lease_info = parsed;
        return false;
    }

    if (!waitDefaultRoutePresentOn(ifname, route_metric)) {
        if (parsed.gateway.empty()) {
            if (reason)
                *reason = "no gateway for probe default route on " + ifname;
            if (lease_info)
                *lease_info = parsed;
            return false;
        }

        (void)netlinkEnsureDefaultRoute(ifname, parsed.gateway, route_metric);
        if (!waitDefaultRoutePresentOn(ifname, route_metric)) {
            if (reason)
                *reason = "failed to add probe default route on " + ifname +
                          ", routes=" + defaultRouteSummary();
            if (lease_info)
                *lease_info = parsed;
            return false;
        }
    }

    if (lease_info)
        *lease_info = parsed;
    return true;
}

bool readTextFile(const std::string &path, std::string *text)
{
    if (!text)
        return false;

    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in.is_open())
        return false;

    std::ostringstream oss;
    oss << in.rdbuf();
    *text = oss.str();
    return true;
}

bool writeTextFile(const std::string &path, const std::string &text)
{
    std::ofstream out(path, std::ios::out | std::ios::trunc | std::ios::binary);
    if (!out.is_open())
        return false;
    out << text;
    out.close();
    return static_cast<bool>(out);
}

std::string firstUsableInterface(const std::vector<std::string> &ifnames)
{
    for (const auto &ifname : ifnames) {
        if (interfaceExists(ifname))
            return ifname;
    }
    return "";
}

std::string firstNameserverLine()
{
    std::string text;
    if (!readTextFile("/etc/resolv.conf", &text))
        return "";

    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (line.find("nameserver") == 0)
            return trimRight(line);
    }
    return "";
}

std::string defaultRouteSummary()
{
    return joinRoutes(netlinkGetDefaultRoutes());
}

} // namespace gateway::network
