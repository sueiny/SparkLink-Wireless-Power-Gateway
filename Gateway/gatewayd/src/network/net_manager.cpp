#include "network/net_manager.h"

#include "common/time_utils.h"
#include "network/cellular_provider.h"
#include "network/ethernet_provider.h"
#include "network/netlink_utils.h"
#include "network/wifi_provider.h"
#include "network/network_utils.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <sstream>
#include <thread>

namespace gateway::network {
namespace {

bool looksLikeDnsFailure(const std::string &reason)
{
    return reason.find("nameserver") != std::string::npos ||
           reason.find("getaddrinfo") != std::string::npos ||
           reason.find("DNS") != std::string::npos;
}

bool isCriticalWifiFault(const NetworkState &state)
{
    if (state.type != NetworkType::Wifi)
        return false;
    if (state.fault_code.empty() || state.fault_code == "none")
        return false;

    return state.fault_code != "WIFI_DNS_NOT_READY" &&
           state.fault_code != "WIFI_CLOUD_UNREACHABLE" &&
           state.fault_code != "WIFI_SCANNING";
}

std::string joinNameservers(const std::vector<std::string> &nameservers)
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

constexpr uint32_t kProbeRouteMetricOffset = 4000;
constexpr int kPreemptConfirmCount = 2;
constexpr int64_t kPreemptStableMs = 30000;
constexpr int kCurrentSoftFailureConfirmCount = 3;

bool isCurrentSoftFailure(const NetworkState &state)
{
    return state.available &&
           (state.fault_code == "CLOUD_UNREACHABLE" ||
            state.fault_code == "WIFI_CLOUD_UNREACHABLE");
}

} // namespace

bool NetManager::init(const config::NetworkConfig &config, log::Logger *logger)
{
    config_ = config;
    logger_ = logger;
    providers_.clear();
    wifi_provider_ = nullptr;

    providers_.push_back(std::make_unique<EthernetProvider>(
        config_.ethernet, priorityFor("ethernet")));

    auto wifi_provider = std::make_unique<WifiProvider>(config_.wifi, priorityFor("wifi"));
    wifi_provider->setLogger(logger_);
    wifi_provider_ = wifi_provider.get();
    providers_.push_back(std::move(wifi_provider));

    providers_.push_back(std::make_unique<CellularProvider>(
        config_.cellular, priorityFor("cellular")));

    current_ = {};
    current_check_failures_ = 0;
    resetPreemptCandidate();
    if (logger_)
        logger_->info("NET", "NetManager initialized, mode=" + config_.mode);
    return true;
}

NetworkState NetManager::snapshotProviderState(INetworkProvider &provider)
{
    NetworkState state;
    state.type = provider.type();
    state.name = provider.name();
    state.ifname = provider.ifname();
    state.retry_delay_ms = 5000;

    if (auto *wifi = dynamic_cast<WifiProvider *>(&provider)) {
        wifi->refreshStatus(false);
        state = wifi->stateSnapshot();
        return state;
    }

    state.available = provider.isInterfaceUp() && provider.hasIp();
    state.fault_code = state.available ? "none" : "LINK_NOT_READY";
    state.fault_message = state.available ? "" : "link or ip not ready";
    state.retry_delay_ms = state.available ? 5000 : 10000;
    return state;
}

void NetManager::applyCloudObservation(const INetworkProvider &provider,
                                       bool cloud_reachable,
                                       const std::string &cloud_reason,
                                       NetworkState *state) const
{
    if (!state)
        return;

    state->cloud_reachable = cloud_reachable;
    if (cloud_reachable) {
        state->fault_code = "none";
        state->fault_message.clear();
        if (state->retry_delay_ms <= 0)
            state->retry_delay_ms = 5000;
        return;
    }

    const bool dns_issue = looksLikeDnsFailure(cloud_reason);
    if (provider.type() == NetworkType::Wifi) {
        state->fault_code = dns_issue ? "WIFI_DNS_NOT_READY" : "WIFI_CLOUD_UNREACHABLE";
    } else {
        state->fault_code = dns_issue ? "DNS_NOT_READY" : "CLOUD_UNREACHABLE";
    }
    state->fault_message = cloud_reason.empty() ? "cloud unreachable" : cloud_reason;
    state->retry_delay_ms = dns_issue ? std::max(state->retry_delay_ms, 5000)
                                      : std::max(state->retry_delay_ms, 10000);
}

bool NetManager::recordPreemptSuccess(const INetworkProvider &provider)
{
    const std::string name = provider.name();
    const std::string ifname = provider.ifname();
    const int64_t now_ms = common::nowMs();
    if (preempt_candidate_name_ != name || preempt_candidate_ifname_ != ifname) {
        preempt_candidate_name_ = name;
        preempt_candidate_ifname_ = ifname;
        preempt_success_count_ = 1;
        preempt_candidate_since_ms_ = now_ms;
    } else {
        ++preempt_success_count_;
    }

    return preempt_success_count_ >= kPreemptConfirmCount &&
           now_ms - preempt_candidate_since_ms_ >= kPreemptStableMs;
}

void NetManager::resetPreemptCandidate()
{
    preempt_candidate_name_.clear();
    preempt_candidate_ifname_.clear();
    preempt_success_count_ = 0;
    preempt_candidate_since_ms_ = 0;
}

NetworkState NetManager::ensureNetwork()
{
    if (config_.mode == "auto")
        return ensureAutoModeNetwork();
    return ensureFixedModeNetwork();
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

INetworkProvider *NetManager::findFixedModeProvider()
{
    for (auto &provider : providers_) {
        if (provider->name() == config_.mode)
            return provider.get();
    }
    return nullptr;
}

bool NetManager::checkProvider(INetworkProvider &provider,
                               bool allow_bring_up,
                               const std::string &context,
                               uint32_t route_metric,
                               NetworkState *observed_state,
                               bool *cloud_reachable,
                               std::string *reason)
{
    if (cloud_reachable)
        *cloud_reachable = false;
    if (reason)
        reason->clear();

    if (logger_)
        logger_->info("NET", std::string("checking ") + provider.name() + " " + provider.ifname());

    NetworkState snapshot = snapshotProviderState(provider);

    if (allow_bring_up) {
        const bool bring_up_ok = provider.bringUp(route_metric);
        if (logger_) {
            logger_->info("NET", std::string(provider.name()) +
                                     std::string(" bringUp ") +
                                     (bring_up_ok ? "ok" : "failed"));
        }
        if (!bring_up_ok) {
            snapshot = snapshotProviderState(provider);
            if (observed_state)
                *observed_state = snapshot;
            if (reason)
                *reason = snapshot.fault_code.empty() ? "bring_up_failed" : snapshot.fault_code;
            return false;
        }

        snapshot = snapshotProviderState(provider);
    }

    if (!allow_bring_up && isCriticalWifiFault(snapshot)) {
        snapshot.available = false;
        if (observed_state)
            *observed_state = snapshot;
        if (reason)
            *reason = snapshot.fault_code;
        if (logger_) {
            logger_->warn("NET", std::string(provider.name()) + " " + provider.ifname() +
                                      " requires recovery, code=" + snapshot.fault_code +
                                      ", detail=" + snapshot.fault_message);
        }
        return false;
    }

    if (!provider.isInterfaceUp()) {
        snapshot.available = false;
        snapshot.fault_code = snapshot.fault_code == "none" ? "LINK_NOT_READY" : snapshot.fault_code;
        snapshot.fault_message = snapshot.fault_message.empty() ? "interface is not up" : snapshot.fault_message;
        snapshot.retry_delay_ms = std::max(snapshot.retry_delay_ms, 5000);
        if (observed_state)
            *observed_state = snapshot;
        if (reason)
            *reason = snapshot.fault_code;
        if (logger_)
            logger_->warn("NET", std::string(provider.name()) +
                                      " interface is not up: " + provider.ifname());
        return false;
    }

    if (!provider.hasIp()) {
        snapshot.available = false;
        if (snapshot.fault_code == "none")
            snapshot.fault_code = "IP_NOT_READY";
        if (snapshot.fault_message.empty())
            snapshot.fault_message = "no IPv4 address";
        snapshot.retry_delay_ms = std::max(snapshot.retry_delay_ms, 10000);
        if (observed_state)
            *observed_state = snapshot;
        if (reason)
            *reason = snapshot.fault_code;
        if (logger_)
            logger_->warn("NET", std::string(provider.name()) +
                                      " has no IPv4: " + provider.ifname());
        return false;
    }

    std::string align_reason;
    const bool preempt_probe = context == "preempt";
    const bool aligned = preempt_probe
                             ? prepareProviderProbeNetwork(provider, context, route_metric,
                                                           &snapshot, &align_reason)
                             : reconcileProviderNetwork(provider, context, &snapshot, &align_reason);
    if (!aligned) {
        snapshot.available = false;
        snapshot.cloud_reachable = false;
        if (snapshot.fault_code == "none") {
            if (looksLikeDnsFailure(align_reason))
                snapshot.fault_code = "DNS_NOT_READY";
            else if (align_reason.find("default route") != std::string::npos ||
                     align_reason.find("gateway") != std::string::npos) {
                snapshot.fault_code = "ROUTE_DRIFT";
            } else {
                snapshot.fault_code = "DNS_MISMATCH";
            }
        }
        if (snapshot.fault_message.empty())
            snapshot.fault_message = align_reason;
        snapshot.retry_delay_ms = std::max(snapshot.retry_delay_ms, 5000);
        if (observed_state)
            *observed_state = snapshot;
        if (reason)
            *reason = snapshot.fault_code;
        return false;
    }

    std::string cloud_reason;
    const bool reachable =
        provider.canReachCloud(config_.cloud_test_host, config_.cloud_test_port, &cloud_reason);
    snapshot.available = true;
    applyCloudObservation(provider, reachable, cloud_reason, &snapshot);
    if (cloud_reachable)
        *cloud_reachable = reachable;
    if (observed_state)
        *observed_state = snapshot;

    if (!reachable) {
        if (reason)
            *reason = snapshot.fault_code;
        if (logger_) {
            std::ostringstream ss;
            ss << provider.name() << " cannot reach cloud "
               << config_.cloud_test_host << ":" << config_.cloud_test_port
               << ", reason=" << (cloud_reason.empty() ? "unknown" : cloud_reason)
               << ", routes=" << defaultRouteSummary()
               << ", dns=" << firstNameserverLine();
            logger_->warn("NET", ss.str());
        }
    } else {
        if (logger_ && !cloud_reason.empty()) {
            logger_->info("NET", std::string(provider.name()) + " " + provider.ifname() +
                                     " cloud reachable, note=" + cloud_reason);
        }
        if (reason)
            *reason = cloud_reason.empty() ? "cloud_reachable" : cloud_reason;
    }

    return reachable;
}

NetworkState NetManager::ensureFixedModeNetwork()
{
    INetworkProvider *provider = findFixedModeProvider();
    if (!provider || !provider->enabled()) {
        current_ = {};
        current_check_failures_ = 0;
        if (logger_)
            logger_->warn("NET", "fixed mode provider unavailable mode=" + config_.mode);
        return current_;
    }

    bool cloud_reachable = false;
    std::string reason;
    NetworkState observed;
    const bool current_matches =
        current_.available && current_.name == provider->name() && current_.ifname == provider->ifname();

    checkProvider(*provider, current_matches ? false : true,
                  current_matches ? "keep" : "select",
                  routeMetricFor(*provider),
                  &observed, &cloud_reachable, &reason);

    if (observed.available && observed.fault_code == "none" && cloud_reachable) {
        current_ = observed;
        current_.cloud_reachable = true;
        current_.available = true;
        current_check_failures_ = 0;
        resetPreemptCandidate();
        if (logger_)
            logger_->info("NET", "selected " + current_.name + " " + current_.ifname +
                                     ", cloud=reachable");
        return current_;
    }

    current_ = observed;
    current_.cloud_reachable = false;
    ++current_check_failures_;
    current_.failure_count = std::max(current_.failure_count, current_check_failures_);
    if (logger_)
        logger_->warn("NET", std::string("fixed mode ") + provider->name() + " " + provider->ifname() +
                                 " unavailable, code=" + current_.fault_code +
                                 ", reason=" + (reason.empty() ? current_.fault_message : reason) +
                                 ", routes=" + defaultRouteSummary() +
                                 ", dns=" + firstNameserverLine());
    return current_;
}

NetworkState NetManager::ensureAutoModeNetwork()
{
    INetworkProvider *current_provider = current_.available ? findProvider(current_) : nullptr;
    NetworkState current_observed;
    bool current_ok = false;

    if (current_provider) {
        bool cloud_reachable = false;
        std::string reason;
        if (checkProvider(*current_provider, false, "keep",
                          routeMetricFor(*current_provider),
                          &current_observed, &cloud_reachable, &reason)) {
            current_observed.available = true;
            current_observed.cloud_reachable = cloud_reachable;
            current_ok = cloud_reachable;
            current_check_failures_ = 0;
        } else {
            current_observed.cloud_reachable = false;
            ++current_check_failures_;
            current_observed.failure_count = std::max(current_observed.failure_count,
                                                      current_check_failures_);
            if (logger_) {
                logger_->warn("NET", current_.name + " " + current_.ifname +
                                         " transient failure #" +
                                         std::to_string(current_check_failures_) +
                                         ", code=" + (current_observed.fault_code.empty() ? "unknown" : current_observed.fault_code) +
                                         ", reason=" + (reason.empty() ? "unknown" : reason) +
                                         ", routes=" + defaultRouteSummary() +
                                         ", dns=" + firstNameserverLine());
            }
            if (isCurrentSoftFailure(current_observed) &&
                current_check_failures_ < kCurrentSoftFailureConfirmCount) {
                current_.available = true;
                current_.cloud_reachable = true;
                current_.fault_code = "none";
                current_.fault_message.clear();
                current_.failure_count = current_check_failures_;
                current_.retry_delay_ms = 5000;
                resetPreemptCandidate();
                if (logger_) {
                    logger_->warn("NET", current_.name + " " + current_.ifname +
                                             " soft failure not confirmed, keep current link, failures=" +
                                             std::to_string(current_check_failures_) + "/" +
                                             std::to_string(kCurrentSoftFailureConfirmCount));
                }
                return current_;
            }
        }
    }

    auto providers = candidateProviders();
    const int current_priority = current_provider ? current_provider->priority() : 1000;

    if (current_ok && current_provider) {
        for (auto *provider : providers) {
            if (!provider->enabled() || provider == current_provider)
                continue;
            if (provider->priority() >= current_priority)
                continue;

            bool cloud_reachable = false;
            std::string reason;
            NetworkState observed;
            const uint32_t probe_metric = probeRouteMetricFor(*provider);
            const bool ok = checkProvider(*provider, true, "preempt",
                                          probe_metric,
                                          &observed, &cloud_reachable, &reason);
            if (ok && cloud_reachable) {
                if (!recordPreemptSuccess(*provider)) {
                    removeProviderProbeRoute(*provider, probe_metric, "remove_after_preempt_probe");
                    restoreProviderNetwork(current_provider, "restore_after_preempt_probe");
                    if (logger_) {
                        logger_->info("NET", "higher priority " + std::string(provider->name()) + " " +
                                                 provider->ifname() + " probe confirmed once, waiting for stable preempt");
                    }
                    current_ = current_observed;
                    return current_;
                }

                std::string select_reason;
                if (reconcileProviderNetwork(*provider, "preempt_select",
                                             &observed, &select_reason)) {
                    removeProviderProbeRoute(*provider, probe_metric, "remove_after_preempt_select");
                    current_ = observed;
                    current_.available = true;
                    current_.cloud_reachable = true;
                    current_check_failures_ = 0;
                    resetPreemptCandidate();
                    if (logger_)
                        logger_->info("NET", "preempt selected " + current_.name + " " + current_.ifname +
                                                 " from " + current_provider->name() + " " + current_provider->ifname());
                    return current_;
                }
                reason = select_reason.empty() ? "preempt alignment failed" : select_reason;
            }

            removeProviderProbeRoute(*provider, probe_metric, "remove_after_preempt_probe");
            restoreProviderNetwork(current_provider, "restore_after_preempt_probe");
            resetPreemptCandidate();
            if (logger_) {
                logger_->info("NET", "higher priority " + std::string(provider->name()) + " " +
                                         provider->ifname() + " not ready, reason=" +
                                         (reason.empty() ? observed.fault_code : reason) +
                                         ", detail=" + observed.fault_message);
            }
        }

        current_ = current_observed;
        if (logger_)
            logger_->info("NET", "keep selected " + current_.name + " " + current_.ifname +
                                     ", cloud=reachable");
        return current_;
    }

    NetworkState failed_state = current_provider ? current_observed : NetworkState{};
    bool have_failed_state = current_provider != nullptr;

    for (auto *provider : providers) {
        if (!provider->enabled())
            continue;
        if (current_provider == provider && current_observed.available)
            continue;

        bool cloud_reachable = false;
        std::string reason;
        NetworkState observed;
        if (checkProvider(*provider, true, "select",
                          routeMetricFor(*provider),
                          &observed, &cloud_reachable, &reason) &&
            cloud_reachable) {
            current_ = observed;
            current_.available = true;
            current_.cloud_reachable = true;
            current_check_failures_ = 0;
            resetPreemptCandidate();
            if (logger_)
                logger_->info("NET", "selected " + current_.name + " " + current_.ifname +
                                         ", cloud=reachable");
            return current_;
        }

        if (have_failed_state && current_provider && provider != current_provider)
            restoreProviderNetwork(current_provider, "restore_after_select_probe");

        if (!have_failed_state) {
            failed_state = observed;
            have_failed_state = true;
        }

        if (logger_)
            logger_->info("NET", std::string(provider->name()) + " " + provider->ifname() +
                                     " not available, trying next, reason=" +
                                     (reason.empty() ? "unknown" : reason));
    }

    if (have_failed_state) {
        current_ = failed_state;
        if (logger_)
            logger_->warn("NET", "keeping failed network state " + current_.name + " " + current_.ifname +
                                     ", code=" + current_.fault_code +
                                     ", retry_ms=" + std::to_string(current_.retry_delay_ms));
        return current_;
    }

    current_ = {};
    current_check_failures_ = 0;
    resetPreemptCandidate();
    if (logger_)
        logger_->warn("NET", "no available network selected");
    return current_;
}

int NetManager::priorityFor(const std::string &name) const
{
    for (size_t i = 0; i < config_.priority.size(); ++i) {
        if (config_.priority[i] == name)
            return static_cast<int>(i);
    }
    return 100;
}

uint32_t NetManager::routeMetricFor(const INetworkProvider &provider) const
{
    switch (provider.type()) {
    case NetworkType::Ethernet:
        return kEthernetRouteMetric;
    case NetworkType::Wifi:
        return kWifiRouteMetric;
    case NetworkType::Cellular:
        return kCellularRouteMetric;
    case NetworkType::None:
    default:
        return defaultRouteMetricForIfname(provider.ifname());
    }
}

uint32_t NetManager::probeRouteMetricFor(const INetworkProvider &provider) const
{
    return routeMetricFor(provider) + kProbeRouteMetricOffset;
}

void NetManager::releaseCurrentProvider()
{
    if (logger_)
        logger_->info("NET", "releasing " + current_.name + " " + current_.ifname);
    current_ = {};
    current_check_failures_ = 0;
}

bool NetManager::refreshDefaultRoute(const INetworkProvider &provider, const std::string &context)
{
    const std::string ifname = provider.ifname();
    if (ifname.empty())
        return false;

    const uint32_t metric = routeMetricFor(provider);
    const bool ok = setDefaultRouteVia(ifname, metric);
    const bool active = defaultRouteActiveOn(ifname, metric);
    if (logger_ && (context != "keep" || !ok)) {
        const std::string message = context + " route via " + ifname +
                                    " metric=" + std::to_string(metric) +
                                    ", ok=" + (ok ? "1" : "0") +
                                    ", active=" + (active ? "1" : "0") +
                                    ", routes=" + defaultRouteSummary();
        if (ok && active)
            logger_->info("NET", message);
        else
            logger_->warn("NET", message);
    }
    return ok && active;
}

bool NetManager::reconcileProviderNetwork(const INetworkProvider &provider,
                                          const std::string &context,
                                          NetworkState *observed_state,
                                          std::string *reason) const
{
    NetworkState local_state = observed_state ? *observed_state : NetworkState{};
    InterfaceLeaseInfo lease_info;
    std::string align_reason;
    const bool ok = reconcileInterfaceNetwork(provider.ifname(), routeMetricFor(provider),
                                              &lease_info, &align_reason);

    if (!ok) {
        local_state.available = false;
        local_state.cloud_reachable = false;
        local_state.fault_code =
            looksLikeDnsFailure(align_reason) ? "DNS_NOT_READY" :
            (align_reason.find("default route") != std::string::npos ? "ROUTE_DRIFT" : "DNS_MISMATCH");
        local_state.fault_message = align_reason;
        local_state.retry_delay_ms = std::max(local_state.retry_delay_ms, 5000);
        if (reason)
            *reason = align_reason;
        if (logger_) {
            logger_->warn("NET", context + " network alignment failed on " + provider.ifname() +
                                     ", code=" + local_state.fault_code +
                                     ", reason=" + align_reason +
                                     ", routes=" + defaultRouteSummary() +
                                     ", dns=" + firstNameserverLine());
        }
    } else if (logger_) {
        std::string dns_reason;
        if (!dnsAlignedToInterface(provider.ifname(), lease_info, &dns_reason)) {
            logger_->warn("NET", "dns mismatch for " + provider.ifname() +
                                     ", reason=" + dns_reason);
        } else {
            logger_->info("NET", "dns realigned to " + provider.ifname() +
                                     " nameservers=" + joinNameservers(lease_info.nameservers));
        }
    }

    if (observed_state)
        *observed_state = local_state;
    return ok;
}

bool NetManager::prepareProviderProbeNetwork(const INetworkProvider &provider,
                                             const std::string &context,
                                             uint32_t route_metric,
                                             NetworkState *observed_state,
                                             std::string *reason) const
{
    NetworkState local_state = observed_state ? *observed_state : NetworkState{};
    InterfaceLeaseInfo lease_info;
    std::string align_reason;
    const bool ok = prepareInterfaceNetworkProbe(provider.ifname(), route_metric,
                                                 &lease_info, &align_reason);

    if (!ok) {
        local_state.available = false;
        local_state.cloud_reachable = false;
        local_state.fault_code =
            looksLikeDnsFailure(align_reason) ? "DNS_NOT_READY" :
            (align_reason.find("default route") != std::string::npos ? "ROUTE_DRIFT" : "DNS_MISMATCH");
        local_state.fault_message = align_reason;
        local_state.retry_delay_ms = std::max(local_state.retry_delay_ms, 5000);
        if (reason)
            *reason = align_reason;
        if (logger_) {
            logger_->warn("NET", context + " probe alignment failed on " + provider.ifname() +
                                     ", code=" + local_state.fault_code +
                                     ", reason=" + align_reason +
                                     ", routes=" + defaultRouteSummary() +
                                     ", dns=" + firstNameserverLine());
        }
    } else if (logger_) {
        logger_->info("NET", context + " probe prepared on " + provider.ifname() +
                                 " nameservers=" + joinNameservers(lease_info.nameservers) +
                                 ", metric=" + std::to_string(route_metric) +
                                 ", routes=" + defaultRouteSummary());
    }

    if (observed_state)
        *observed_state = local_state;
    return ok;
}

void NetManager::removeProviderProbeRoute(const INetworkProvider &provider,
                                          uint32_t route_metric,
                                          const std::string &context) const
{
    bool removed = false;
    for (const auto &route : netlinkGetDefaultRoutes()) {
        if (route.iface != provider.ifname() || route.metric != route_metric)
            continue;
        if (netlinkDelDefaultRoute(route.iface, route.gateway, route.metric))
            removed = true;
    }

    if (removed && logger_) {
        logger_->info("NET", context + " removed probe route on " + provider.ifname() +
                             " metric=" + std::to_string(route_metric) +
                             ", routes=" + defaultRouteSummary());
    }
}

void NetManager::restoreProviderNetwork(const INetworkProvider *provider, const std::string &context) const
{
    if (!provider)
        return;

    InterfaceLeaseInfo lease_info;
    std::string reason;
    if (reconcileInterfaceNetwork(provider->ifname(), routeMetricFor(*provider), &lease_info, &reason)) {
        if (logger_)
            logger_->info("NET", context + " restored " + provider->ifname() +
                                     ", dns=" + joinNameservers(lease_info.nameservers));
    } else if (logger_) {
        logger_->warn("NET", context + " failed to restore " + provider->ifname() +
                                 ", reason=" + reason);
    }
}

} // namespace gateway::network
