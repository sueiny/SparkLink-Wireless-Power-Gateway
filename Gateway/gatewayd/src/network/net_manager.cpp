#include "network/net_manager.h"

#include "network/cellular_provider.h"
#include "network/ethernet_provider.h"
#include "network/network_utils.h"
#include "network/wifi_provider.h"

#include <algorithm>
#include <chrono>
#include <sstream>
#include <thread>

namespace gateway::network {

bool NetManager::init(const config::NetworkConfig &config, log::Logger *logger)
{
    config_ = config;
    logger_ = logger;
    providers_.clear();

    providers_.push_back(std::make_unique<EthernetProvider>(
        config_.ethernet, priorityFor("ethernet")));
    providers_.push_back(std::make_unique<WifiProvider>(
        config_.wifi, priorityFor("wifi")));
    providers_.push_back(std::make_unique<CellularProvider>(
        config_.cellular, priorityFor("cellular")));

    current_ = {};
    current_check_failures_ = 0;
    if (logger_)
        logger_->info("NET", "NetManager initialized, mode=" + config_.mode);
    return true;
}

NetworkState NetManager::ensureNetwork()
{
    // 已有选中接口：先检查是否仍然可用
    if (current_.available) {
        auto *current_provider = findProvider(current_);
        bool cloud_reachable = false;
        std::string reason;
        if (current_provider &&
            checkProvider(*current_provider, false, &cloud_reachable, &reason)) {
            current_.cloud_reachable = cloud_reachable;
            current_check_failures_ = 0;
            // 定期刷新默认路由，防止 dhcpcd 续租后路由漂移
            refreshDefaultRoute(current_.ifname, "keep");
            if (logger_)
                logger_->info("NET", "keep selected " + current_.name + " " + current_.ifname +
                                         ", cloud=" + (cloud_reachable ? "reachable" : "unreachable"));
            return current_;
        }

        ++current_check_failures_;
        current_.cloud_reachable = false;
        if (current_check_failures_ < 3) {
            if (logger_) {
                logger_->warn("NET", current_.name + " " + current_.ifname +
                                         " transient failure #" +
                                         std::to_string(current_check_failures_) +
                                         ", reason=" + (reason.empty() ? "unknown" : reason) +
                                         ", routes=" + defaultRouteSummary() +
                                         ", dns=" + firstNameserverLine());
            }
            return current_;
        }

        // 当前接口连续失败，释放后重新选择
        if (logger_)
            logger_->warn("NET", current_.name + " " + current_.ifname +
                                     " lost after repeated failures, re-selecting");
        releaseCurrentProvider();
    }

    // 按优先级逐个拉起，成功就停，不拉起低优先级接口。
    // 避免所有接口同时 UP 导致 dhcpcd 路由冲突。
    for (auto *provider : candidateProviders()) {
        if (!provider->enabled()) {
            if (logger_)
                logger_->info("NET", std::string(provider->name()) + " disabled");
            continue;
        }

        bool cloud_reachable = false;
        std::string reason;
        if (checkProvider(*provider, true, &cloud_reachable, &reason)) {
            current_.type = provider->type();
            current_.name = provider->name();
            current_.ifname = provider->ifname();
            current_.cloud_reachable = cloud_reachable;
            current_.available = true;
            current_check_failures_ = 0;

            // 确保默认路由走选中的接口
            if (refreshDefaultRoute(current_.ifname, "select")) {
                if (logger_)
                    logger_->info("NET", "default route set via " + current_.ifname);
            }

            if (logger_)
                logger_->info("NET", "selected " + current_.name + " " + current_.ifname +
                                         ", cloud=" + (cloud_reachable ? "reachable" : "unreachable"));
            return current_;
        }

        // 该接口不可用，继续尝试下一个
        if (logger_)
            logger_->info("NET", std::string(provider->name()) + " " + provider->ifname() +
                                     " not available, trying next, reason=" +
                                     (reason.empty() ? "unknown" : reason));
    }

    current_ = {};
    current_check_failures_ = 0;
    if (logger_)
        logger_->warn("NET", "no available network selected");
    return current_;
}

std::vector<INetworkProvider *> NetManager::candidateProviders()
{
    std::vector<INetworkProvider *> selected;

    for (auto &provider : providers_) {
        if (config_.mode == "auto" || config_.mode == provider->name())
            selected.push_back(provider.get());
    }

    std::sort(selected.begin(), selected.end(), [](const auto *lhs, const auto *rhs) {
        return lhs->priority() < rhs->priority();
    });

    return selected;
}

INetworkProvider *NetManager::findProvider(const NetworkState &state)
{
    for (auto &provider : providers_) {
        if (provider->type() == state.type && provider->ifname() == state.ifname)
            return provider.get();
    }
    return nullptr;
}

bool NetManager::checkProvider(INetworkProvider &provider,
                               bool allow_bring_up,
                               bool *cloud_reachable,
                               std::string *reason)
{
    if (cloud_reachable)
        *cloud_reachable = false;
    if (reason)
        reason->clear();

    if (logger_)
        logger_->info("NET", std::string("checking ") + provider.name() + " " + provider.ifname());

    if (allow_bring_up) {
        // 首次选择网络或网络掉线后才执行拉起流程，避免每轮重启 DHCP 或 wpa_supplicant。
        const bool bring_up_ok = provider.bringUp();
        if (logger_) {
            logger_->info("NET", std::string(provider.name()) +
                                     std::string(" bringUp ") +
                                     (bring_up_ok ? "ok" : "failed"));
        }
        // bringUp 会触发 dhcpcd 更新 resolv.conf（异步写入 nameserver）。
        // 设置路由后等待 DNS 就绪，tcpConnect 内部也会重试等待。
        if (bring_up_ok)
            refreshDefaultRoute(provider.ifname(), "bring_up");
        if (!bring_up_ok) {
            if (reason)
                *reason = "bring_up_failed";
        }
    }

    if (!provider.isInterfaceUp()) {
        if (logger_)
            logger_->warn("NET", std::string(provider.name()) +
                                      " interface is not up: " + provider.ifname());
        if (reason)
            *reason = "link_not_ready";
        return false;
    }

    if (!provider.hasIp()) {
        if (logger_)
            logger_->warn("NET", std::string(provider.name()) +
                                      " has no IPv4: " + provider.ifname());
        if (reason)
            *reason = "ip_not_ready";
        return false;
    }

    // bringUp 后 dhcpcd 异步更新 resolv.conf，DNS 可能暂时不可用。
    // 首次选择时跳过 canReachCloud 测试，先选中接口、设好路由，
    // 下一轮 ensureNetwork 会重新验证（此时 DNS 已就绪）。
    if (allow_bring_up) {
        if (cloud_reachable)
            *cloud_reachable = false;
        if (reason)
            *reason = "cloud_test_deferred";
        if (logger_)
            logger_->info("NET", std::string(provider.name()) + " " + provider.ifname() +
                                      " ready, deferring cloud test");
        return true;
    }

    std::string cloud_reason;
    const bool reachable =
        provider.canReachCloud(config_.cloud_test_host, config_.cloud_test_port, &cloud_reason);
    if (cloud_reachable)
        *cloud_reachable = reachable;

    if (!reachable) {
        if (reason)
            *reason = "cloud_unreachable: " + (cloud_reason.empty() ? "unknown" : cloud_reason);
        if (logger_) {
            std::ostringstream ss;
            ss << provider.name() << " cannot reach cloud "
               << config_.cloud_test_host << ":" << config_.cloud_test_port
               << ", reason=" << (cloud_reason.empty() ? "unknown" : cloud_reason)
               << ", routes=" << defaultRouteSummary()
               << ", dns=" << firstNameserverLine();
            logger_->warn("NET", ss.str());
        }
    } else if (reason) {
        *reason = "cloud_reachable";
    }

    return true;
}

int NetManager::priorityFor(const std::string &name) const
{
    for (size_t i = 0; i < config_.priority.size(); ++i) {
        if (config_.priority[i] == name)
            return static_cast<int>(i);
    }
    return 100;
}

void NetManager::releaseCurrentProvider()
{
    // 重置内存中的接口状态。注意：不执行 ip link set down，
    // 因为设计上不允许同时拉低所有接口（会导致 dhcpcd 路由冲突）。
    // 接口实际由 dhcpcd 管理，新接口选中后通过 setDefaultRouteVia 切换路由。
    if (logger_)
        logger_->info("NET", "releasing " + current_.name + " " + current_.ifname);
    current_ = {};
    current_check_failures_ = 0;
}

bool NetManager::refreshDefaultRoute(const std::string &ifname, const std::string &context)
{
    if (ifname.empty())
        return false;

    const bool ok = setDefaultRouteVia(ifname);
    if (logger_ && (context != "keep" || !ok)) {
        const std::string message = context + " route via " + ifname +
                                    ", ok=" + (ok ? "1" : "0") +
                                    ", routes=" + defaultRouteSummary();
        if (ok)
            logger_->info("NET", message);
        else
            logger_->warn("NET", message);
    }
    return ok;
}

} // namespace gateway::network
