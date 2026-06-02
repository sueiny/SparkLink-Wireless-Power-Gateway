#include "network/netlink_utils.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <sys/socket.h>

namespace gateway::network {
namespace {

// Netlink 请求缓冲区
struct NetlinkRequest {
    struct nlmsghdr nlh;
    struct rtmsg rtm;
    char buf[256];
};

// 添加 rtattr 到 nlmsghdr
void addRtattr(struct nlmsghdr *nlh, int maxlen, int type, const void *data, int datalen)
{
    int len = RTA_LENGTH(datalen);
    struct rtattr *rta = reinterpret_cast<struct rtattr *>(
        reinterpret_cast<char *>(nlh) + NLMSG_ALIGN(nlh->nlmsg_len));
    if (NLMSG_ALIGN(nlh->nlmsg_len) + RTA_ALIGN(len) > static_cast<size_t>(maxlen))
        return;
    rta->rta_type = type;
    rta->rta_len = len;
    if (datalen > 0)
        memcpy(RTA_DATA(rta), data, datalen);
    nlh->nlmsg_len = NLMSG_ALIGN(nlh->nlmsg_len) + RTA_ALIGN(len);
}

// 发送 Netlink 请求并等待 ACK
bool sendNetlinkRequest(struct nlmsghdr *nlh)
{
    int fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (fd < 0)
        return false;

    // 设置 5 秒超时，防止 recv 阻塞
    struct timeval tv;
    tv.tv_sec = 5;
    tv.tv_usec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_nl sa;
    memset(&sa, 0, sizeof(sa));
    sa.nl_family = AF_NETLINK;

    struct iovec iov = {nlh, nlh->nlmsg_len};
    struct msghdr msg = {&sa, sizeof(sa), &iov, 1, nullptr, 0, 0};

    ssize_t sent = sendmsg(fd, &msg, 0);
    if (sent < 0) {
        close(fd);
        return false;
    }

    // 读取 ACK
    char reply[4096];
    ssize_t n = recv(fd, reply, sizeof(reply), 0);
    close(fd);

    if (n < 0)
        return false;

    struct nlmsghdr *reply_hdr = reinterpret_cast<struct nlmsghdr *>(reply);
    if (reply_hdr->nlmsg_type == NLMSG_ERROR) {
        struct nlmsgerr *err = static_cast<struct nlmsgerr *>(NLMSG_DATA(reply_hdr));
        return err->error == 0;
    }
    return true;
}

// IP 地址转字符串
std::string ipToString(uint32_t ip)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u",
             ip & 0xFF, (ip >> 8) & 0xFF, (ip >> 16) & 0xFF, (ip >> 24) & 0xFF);
    return buf;
}

// 字符串转 IP 地址
uint32_t stringToIp(const std::string &str)
{
    int a = 0, b = 0, c = 0, d = 0;
    char dot;
    std::istringstream iss(str);
    iss >> a >> dot >> b >> dot >> c >> dot >> d;
    if (iss.fail())
        return 0;
    return static_cast<uint32_t>((d << 24) | (c << 16) | (b << 8) | a);
}

} // namespace

std::vector<NetlinkRoute> netlinkGetDefaultRoutes()
{
    std::vector<NetlinkRoute> routes;

    // 从 /proc/net/route 读取（比 Netlink 查询更简单可靠）
    std::ifstream file("/proc/net/route");
    if (!file.is_open())
        return routes;

    std::string line;
    std::getline(file, line); // 跳过表头
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string iface, dest, gateway, flags, refcnt, use, met;
        iss >> iface >> dest >> gateway >> flags >> refcnt >> use >> met;
        if (dest != "00000000")
            continue;

        unsigned int gw_raw;
        std::istringstream(gateway) >> std::hex >> gw_raw;

        NetlinkRoute r;
        r.iface = iface;
        r.gateway = ipToString(gw_raw);
        r.metric = static_cast<uint32_t>(std::stoul(met));
        r.is_default = true;
        routes.push_back(r);
    }
    return routes;
}

bool netlinkAddDefaultRoute(const std::string &ifname, const std::string &gateway, uint32_t metric)
{
    NetlinkRequest req;
    memset(&req, 0, sizeof(req));

    req.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
    req.nlh.nlmsg_type = RTM_NEWROUTE;
    req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_REPLACE;
    req.nlh.nlmsg_seq = 1;

    req.rtm.rtm_family = AF_INET;
    req.rtm.rtm_table = RT_TABLE_MAIN;
    req.rtm.rtm_protocol = RTPROT_BOOT;
    req.rtm.rtm_scope = RT_SCOPE_UNIVERSE;
    req.rtm.rtm_type = RTN_UNICAST;
    req.rtm.rtm_dst_len = 0; // 默认路由

    // RTA_GATEWAY
    uint32_t gw_ip = stringToIp(gateway);
    addRtattr(&req.nlh, sizeof(req), RTA_GATEWAY, &gw_ip, 4);

    // RTA_OIF
    unsigned int ifidx = if_nametoindex(ifname.c_str());
    if (ifidx == 0)
        return false;
    addRtattr(&req.nlh, sizeof(req), RTA_OIF, &ifidx, 4);

    // RTA_PRIORITY (metric)
    addRtattr(&req.nlh, sizeof(req), RTA_PRIORITY, &metric, 4);

    return sendNetlinkRequest(&req.nlh);
}

bool netlinkDelDefaultRoute(const std::string &ifname, const std::string &gateway, uint32_t metric)
{
    NetlinkRequest req;
    memset(&req, 0, sizeof(req));

    req.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
    req.nlh.nlmsg_type = RTM_DELROUTE;
    req.nlh.nlmsg_flags = NLM_F_REQUEST;
    req.nlh.nlmsg_seq = 2;

    req.rtm.rtm_family = AF_INET;
    req.rtm.rtm_table = RT_TABLE_MAIN;
    req.rtm.rtm_protocol = RTPROT_BOOT;
    req.rtm.rtm_scope = RT_SCOPE_UNIVERSE;
    req.rtm.rtm_type = RTN_UNICAST;
    req.rtm.rtm_dst_len = 0;

    uint32_t gw_ip = stringToIp(gateway);
    addRtattr(&req.nlh, sizeof(req), RTA_GATEWAY, &gw_ip, 4);

    unsigned int ifidx = if_nametoindex(ifname.c_str());
    if (ifidx == 0)
        return false;
    addRtattr(&req.nlh, sizeof(req), RTA_OIF, &ifidx, 4);

    addRtattr(&req.nlh, sizeof(req), RTA_PRIORITY, &metric, 4);

    return sendNetlinkRequest(&req.nlh);
}

bool netlinkDelOtherDefaultRoutes(const std::string &keep_ifname)
{
    auto routes = netlinkGetDefaultRoutes();
    bool ok = true;
    for (const auto &r : routes) {
        if (r.iface == keep_ifname && r.metric == 100)
            continue; // 保留我们设置的路由
        // Netlink 删除需要精确匹配属性，dhcpcd 路由有 proto/src 等额外属性
        // 直接用 Netlink 可能删不掉，改用 ip route del（更可靠）
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "ip route del default via %s dev %s metric %u 2>/dev/null",
                 r.gateway.c_str(), r.iface.c_str(), r.metric);
        if (system(cmd) != 0)
            ok = false;
    }
    return ok;
}

bool netlinkSetDefaultRouteVia(const std::string &ifname)
{
    // 1. 获取目标接口的网关
    std::ifstream file("/proc/net/route");
    if (!file.is_open())
        return false;

    std::string gw;
    std::string line;
    std::getline(file, line); // 跳过表头
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string iface, dest, gateway, flags, refcnt, use, met;
        iss >> iface >> dest >> gateway >> flags >> refcnt >> use >> met;
        if (iface == ifname && dest == "00000000") {
            unsigned int gw_raw;
            std::istringstream(gateway) >> std::hex >> gw_raw;
            gw = ipToString(gw_raw);
            break;
        }
    }

    if (gw.empty())
        return false;

    // 2. 删除其他接口的默认路由
    netlinkDelOtherDefaultRoutes(ifname);

    // 3. 添加选中接口的默认路由（metric=100）
    return netlinkAddDefaultRoute(ifname, gw, 100);
}

} // namespace gateway::network
