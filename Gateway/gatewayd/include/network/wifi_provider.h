#pragma once

#include "common/logger.h"
#include "config/config_manager.h"
#include "network/i_network_provider.h"

#include <array>
#include <chrono>
#include <mutex>

namespace gateway::network {

// Wi-Fi provider：生成临时 wpa_supplicant 配置，启动连接并申请 DHCP。
// 额外封装了 RK3506 板级射频恢复链路和故障分层状态。
class WifiProvider : public INetworkProvider {
public:
    WifiProvider(config::WifiConfig config, int priority);

    NetworkType type() const override { return NetworkType::Wifi; }
    const char *name() const override { return "wifi"; }
    std::string ifname() const override { return config_.ifname; }
    int priority() const override { return priority_; }
    bool enabled() const override { return config_.enable; }

    void setLogger(log::Logger *logger);
    NetworkState stateSnapshot() const;
    void refreshStatus(bool include_scan = false);

    bool bringUp(uint32_t route_metric = 0) override;
    bool isInterfaceUp() const override;
    bool hasIp() const override;
    bool canReachCloud(const std::string &host,
                       int port,
                       std::string *reason = nullptr) const override;

private:
    struct FaultInfo {
        std::string code = "none";
        std::string message;
        int retry_delay_ms = 5000;
        int recovery_level = 0;
    };

    std::string wpaConfigPath() const;
    std::string wpaLogPath() const;
    std::string rfkillStatePath() const;
    std::string rfkillSoftPath() const;
    std::string rfkillHardPath() const;
    std::string wifiPowerPath() const;
    std::string faultModule(const std::string &code) const;

    bool wifiAlreadyReadyLocked() const;
    FaultInfo diagnoseLocked(bool include_scan) const;
    NetworkState buildStateLocked() const;
    void markSuccessLocked();
    void markFailureLocked(const FaultInfo &fault);
    int selectRecoveryStageLocked(const FaultInfo &fault) const;
    bool stageCoolingDownLocked(int stage, int64_t now_ms, int *remaining_ms) const;
    int stageCooldownMs(int stage) const;
    int stageBackoffMs(int stage, int failure_count) const;
    bool writeConfigLocked() const;
    bool startWpaSupplicantLocked(std::string *detail);
    bool waitForWifiConnectedLocked(int timeout_ms, std::string *detail) const;
    bool softRestartLocked(uint32_t route_metric, std::string *detail);
    bool rfkillUnlockLocked(std::string *detail);
    bool powerCycleLocked(std::string *detail);
    void logFaultLocked(const FaultInfo &fault, const std::string &detail) const;

    config::WifiConfig config_;
    int priority_ = 0;
    mutable std::mutex mutex_;
    log::Logger *logger_ = nullptr;
    NetworkState last_state_;
    int consecutive_failures_ = 0;
    std::array<int, 4> stage_failures_{{0, 0, 0, 0}};
    std::array<int64_t, 4> stage_last_attempt_ms_{{0, 0, 0, 0}};
};

} // namespace gateway::network
