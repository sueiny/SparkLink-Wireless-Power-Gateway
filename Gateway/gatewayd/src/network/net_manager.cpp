#include "network/net_manager.h"

#include "network/cellular_provider.h"
#include "network/ethernet_provider.h"
#include "network/wifi_provider.h"

#include <algorithm>
#include <sstream>

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
    if (logger_)
        logger_->info("NET", "NetManager initialized, mode=" + config_.mode);
    return true;
}

NetworkState NetManager::ensureNetwork()
{
    if (current_.available) {
        auto *current_provider = findProvider(current_);
        bool cloud_reachable = false;
        if (current_provider && checkProvider(*current_provider, false, &cloud_reachable)) {
            current_.cloud_reachable = cloud_reachable;
            if (logger_)
                logger_->info("NET", "keep selected " + current_.name + " " + current_.ifname +
                                         ", cloud=" + (cloud_reachable ? "reachable" : "unreachable"));
            return current_;
        }
    }

    for (auto *provider : candidateProviders()) {
        if (!provider->enabled()) {
            if (logger_)
                logger_->info("NET", std::string(provider->name()) + " disabled");
            continue;
        }

        bool cloud_reachable = false;
        if (checkProvider(*provider, true, &cloud_reachable)) {
            current_.type = provider->type();
            current_.name = provider->name();
            current_.ifname = provider->ifname();
            current_.cloud_reachable = cloud_reachable;
            current_.available = true;

            if (logger_)
                logger_->info("NET", "selected " + current_.name + " " + current_.ifname +
                                         ", cloud=" + (cloud_reachable ? "reachable" : "unreachable"));
            return current_;
        }
    }

    current_ = {};
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
                               bool *cloud_reachable)
{
    if (cloud_reachable)
        *cloud_reachable = false;

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
    }

    if (!provider.isInterfaceUp()) {
        if (logger_)
            logger_->warn("NET", std::string(provider.name()) +
                                      " interface is not up: " + provider.ifname());
        return false;
    }

    if (!provider.hasIp()) {
        if (logger_)
            logger_->warn("NET", std::string(provider.name()) +
                                      " has no IPv4: " + provider.ifname());
        return false;
    }

    const bool reachable = provider.canReachCloud(config_.cloud_test_host, config_.cloud_test_port);
    if (cloud_reachable)
        *cloud_reachable = reachable;

    if (!reachable) {
        if (logger_) {
            std::ostringstream ss;
            ss << provider.name() << " cannot reach cloud "
               << config_.cloud_test_host << ":" << config_.cloud_test_port;
            logger_->warn("NET", ss.str());
        }
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

} // namespace gateway::network
