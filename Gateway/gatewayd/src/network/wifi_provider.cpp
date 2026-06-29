#include "network/wifi_provider.h"

#include "common/time_utils.h"
#include "network/network_utils.h"

#include <algorithm>
#include <chrono>
#include <initializer_list>
#include <sstream>
#include <sys/stat.h>
#include <thread>
#include <utility>
namespace gateway::network {
namespace {

constexpr int kStage1CooldownMs = 5000;
constexpr int kStage2CooldownMs = 15000;
constexpr int kStage3CooldownMs = 60000;

constexpr int kStage1BaseDelayMs = 5000;
constexpr int kStage2BaseDelayMs = 15000;
constexpr int kStage3BaseDelayMs = 60000;

constexpr int kStage1MaxDelayMs = 30000;
constexpr int kStage2MaxDelayMs = 120000;
constexpr int kStage3MaxDelayMs = 300000;

std::string trimCopy(std::string text)
{
    while (!text.empty() &&
           (text.back() == '\n' || text.back() == '\r' || text.back() == ' ' || text.back() == '\t')) {
        text.pop_back();
    }
    return text;
}

std::string compactText(const std::string &text, size_t max_len = 400)
{
    std::string compact;
    compact.reserve(std::min(max_len, text.size()));
    for (char ch : text) {
        if (ch == '\r' || ch == '\n' || ch == '\t')
            compact.push_back(' ');
        else
            compact.push_back(ch);
        if (compact.size() >= max_len)
            break;
    }
    while (!compact.empty() && compact.back() == ' ')
        compact.pop_back();
    return compact;
}

bool containsAny(const std::string &text, std::initializer_list<const char *> tokens)
{
    for (const char *token : tokens) {
        if (text.find(token) != std::string::npos)
            return true;
    }
    return false;
}

} // namespace

WifiProvider::WifiProvider(config::WifiConfig config, int priority)
    : config_(std::move(config)), priority_(priority)
{
    last_state_.type = NetworkType::Wifi;
    last_state_.name = name();
    last_state_.ifname = config_.ifname;
}

void WifiProvider::setLogger(log::Logger *logger)
{
    std::lock_guard<std::mutex> lock(mutex_);
    logger_ = logger;
}

std::string WifiProvider::wpaConfigPath() const
{
    return "/tmp/gateway_wpa_" + config_.ifname + ".conf";
}

std::string WifiProvider::wpaLogPath() const
{
    return "/tmp/gateway_wpa_" + config_.ifname + ".log";
}

std::string WifiProvider::rfkillStatePath() const
{
    return "/sys/class/rfkill/rfkill1/state";
}

std::string WifiProvider::rfkillSoftPath() const
{
    return "/sys/class/rfkill/rfkill1/soft";
}

std::string WifiProvider::rfkillHardPath() const
{
    return "/sys/class/rfkill/rfkill1/hard";
}

std::string WifiProvider::wifiPowerPath() const
{
    return "/sys/class/leds/wifi-en/brightness";
}

std::string WifiProvider::faultModule(const std::string &code) const
{
    if (code.find("RADIO") != std::string::npos || code.find("RFKILL") != std::string::npos ||
        code.find("SCAN") != std::string::npos || code.find("DORMANT") != std::string::npos ||
        code.find("INTERFACE") != std::string::npos) {
        return "WIFI-RADIO";
    }
    if (code.find("WPA") != std::string::npos || code.find("AUTH") != std::string::npos)
        return "WIFI-WPA";
    if (code.find("DHCP") != std::string::npos || code.find("DNS") != std::string::npos ||
        code.find("CLOUD") != std::string::npos) {
        return "WIFI-NET";
    }
    return "WIFI";
}

bool WifiProvider::wifiAlreadyReadyLocked() const
{
    if (!interfaceHasIpv4(config_.ifname))
        return false;

    const auto status = runProcess({"wpa_cli", "-i", config_.ifname, "status"}, 2000);
    if (!status.timeout && status.exit_code == 0 &&
        status.stdout_output.find("wpa_state=COMPLETED") != std::string::npos) {
        if (config_.ssid.empty() || status.stdout_output.find("ssid=" + config_.ssid) != std::string::npos)
            return true;
    }

    const auto link = runProcess({"iw", "dev", config_.ifname, "link"}, 2000);
    if (link.timeout || link.exit_code != 0)
        return false;
    if (link.stdout_output.find("Connected") == std::string::npos)
        return false;
    if (!config_.ssid.empty() && link.stdout_output.find("SSID: " + config_.ssid) == std::string::npos)
        return false;
    return true;
}

WifiProvider::FaultInfo WifiProvider::diagnoseLocked(bool include_scan) const
{
    FaultInfo fault;
    fault.retry_delay_ms = kStage1BaseDelayMs;

    if (!config_.enable) {
        fault.code = "WIFI_DISABLED";
        fault.message = "wifi disabled by config";
        fault.retry_delay_ms = kStage3CooldownMs;
        fault.recovery_level = 0;
        return fault;
    }

    if (config_.ifname.empty() || config_.ssid.empty() || config_.password.empty()) {
        fault.code = "WIFI_CONFIG_INVALID";
        fault.message = "wifi config missing ssid/password/ifname";
        fault.retry_delay_ms = kStage3CooldownMs;
        fault.recovery_level = 1;
        return fault;
    }

    if (!interfaceExists(config_.ifname)) {
        fault.code = "WIFI_INTERFACE_MISSING";
        fault.message = "interface missing: " + config_.ifname;
        fault.retry_delay_ms = kStage2BaseDelayMs;
        fault.recovery_level = 1;
        return fault;
    }

    std::string rfkill_hard;
    if (readTextFile(rfkillHardPath(), &rfkill_hard) && trimCopy(rfkill_hard) == "1") {
        fault.code = "WIFI_RFKILL_HARD_BLOCKED";
        fault.message = "rfkill hard block active";
        fault.retry_delay_ms = kStage3BaseDelayMs;
        fault.recovery_level = 3;
        return fault;
    }

    std::string rfkill_soft;
    if (readTextFile(rfkillSoftPath(), &rfkill_soft) && trimCopy(rfkill_soft) == "1") {
        fault.code = "WIFI_RFKILL_SOFT_BLOCKED";
        fault.message = "rfkill soft block active";
        fault.retry_delay_ms = kStage2BaseDelayMs;
        fault.recovery_level = 2;
        return fault;
    }

    std::string rfkill_state;
    if (readTextFile(rfkillStatePath(), &rfkill_state) && trimCopy(rfkill_state) == "0") {
        fault.code = "WIFI_RFKILL_SOFT_BLOCKED";
        fault.message = "rfkill state blocked";
        fault.retry_delay_ms = kStage2BaseDelayMs;
        fault.recovery_level = 2;
        return fault;
    }

    const std::string oper_state = interfaceOperState(config_.ifname);
    if (oper_state == "dormant") {
        fault.code = "WIFI_WLAN_DORMANT";
        fault.message = "wlan dormant";
        fault.retry_delay_ms = kStage1BaseDelayMs;
        fault.recovery_level = 1;
        return fault;
    }
    if (oper_state == "down" || oper_state == "lowerlayerdown") {
        fault.code = "WIFI_INTERFACE_DOWN";
        fault.message = "interface down: " + oper_state;
        fault.retry_delay_ms = kStage1BaseDelayMs;
        fault.recovery_level = 1;
        return fault;
    }

    const auto status = runProcess({"wpa_cli", "-i", config_.ifname, "status"}, 2000);
    if (status.timeout || status.exit_code != 0) {
        fault.code = "WIFI_WPA_UNAVAILABLE";
        fault.message = "wpa_cli status failed";
        fault.retry_delay_ms = kStage1BaseDelayMs;
        fault.recovery_level = 1;
        return fault;
    }

    if (status.stdout_output.find("wpa_state=COMPLETED") != std::string::npos) {
        if (interfaceHasIpv4(config_.ifname))
            return fault;

        fault.code = "WIFI_DHCP_FAILED";
        fault.message = "connected but IPv4 missing";
        fault.retry_delay_ms = kStage2BaseDelayMs;
        fault.recovery_level = 1;
        return fault;
    }

    if (containsAny(status.stdout_output,
                    {"wpa_state=SCANNING", "wpa_state=ASSOCIATING", "wpa_state=4WAY_HANDSHAKE"})) {
        fault.code = "WIFI_SCANNING";
        fault.message = "wpa state=" + compactText(status.stdout_output, 120);
        fault.retry_delay_ms = kStage1BaseDelayMs;
        fault.recovery_level = 0;
        return fault;
    }

    std::string wpa_log;
    if (readTextFile(wpaLogPath(), &wpa_log) &&
        containsAny(wpa_log,
                    {"WRONG_KEY", "pre-shared key may be incorrect", "AUTH_FAILED",
                     "SSID-TEMP-DISABLED", "CTRL-EVENT-DISCONNECTED reason=WRONG_KEY"})) {
        fault.code = "WIFI_AUTH_FAILED";
        fault.message = "wpa log indicates authentication failure";
        fault.retry_delay_ms = kStage2BaseDelayMs;
        fault.recovery_level = 1;
        return fault;
    }

    if (include_scan) {
        const auto scan = runProcess({"iw", "dev", config_.ifname, "scan"}, 4000);
        if (scan.timeout) {
            fault.code = "WIFI_SCAN_FAILED";
            fault.message = "scan timeout";
            fault.retry_delay_ms = kStage2BaseDelayMs;
            fault.recovery_level = 2;
            return fault;
        }
        if (scan.exit_code != 0) {
            const std::string compact = compactText(scan.stdout_output, 200);
            if (compact.find("Operation not permitted") != std::string::npos ||
                compact.find("Network is down") != std::string::npos) {
                fault.code = "WIFI_RADIO_POWER_FAULT";
                fault.message = compact.empty() ? "scan denied by radio" : compact;
                fault.retry_delay_ms = kStage3BaseDelayMs;
                fault.recovery_level = 3;
            } else {
                fault.code = "WIFI_SCAN_FAILED";
                fault.message = compact.empty() ? "scan failed" : compact;
                fault.retry_delay_ms = kStage2BaseDelayMs;
                fault.recovery_level = 2;
            }
            return fault;
        }
    }

    if (!interfaceHasIpv4(config_.ifname)) {
        fault.code = "WIFI_DHCP_PENDING";
        fault.message = "waiting for IPv4 lease";
        fault.retry_delay_ms = kStage1BaseDelayMs;
        fault.recovery_level = 1;
        return fault;
    }

    fault.code = "WIFI_WPA_DISCONNECTED";
    fault.message = "wpa state not completed";
    fault.retry_delay_ms = kStage1BaseDelayMs;
    fault.recovery_level = 1;
    return fault;
}

NetworkState WifiProvider::buildStateLocked() const
{
    NetworkState state;
    state.type = NetworkType::Wifi;
    state.name = name();
    state.ifname = config_.ifname;
    return state;
}

void WifiProvider::markSuccessLocked()
{
    consecutive_failures_ = 0;
    stage_failures_.fill(0);
    last_state_ = buildStateLocked();
    last_state_.available = true;
    last_state_.cloud_reachable = false;
    last_state_.fault_code = "none";
    last_state_.fault_message.clear();
    last_state_.retry_delay_ms = kStage1BaseDelayMs;
    last_state_.failure_count = 0;
    last_state_.recovery_level = 0;
}

void WifiProvider::markFailureLocked(const FaultInfo &fault)
{
    consecutive_failures_ = std::max(1, consecutive_failures_ + 1);
    const int level = std::clamp(fault.recovery_level, 1, 3);
    stage_failures_[level] = std::max(1, stage_failures_[level] + 1);

    last_state_ = buildStateLocked();
    last_state_.available = false;
    last_state_.cloud_reachable = false;
    last_state_.fault_code = fault.code;
    last_state_.fault_message = fault.message;
    last_state_.retry_delay_ms = fault.retry_delay_ms;
    last_state_.failure_count = consecutive_failures_;
    last_state_.recovery_level = level;
}

int WifiProvider::selectRecoveryStageLocked(const FaultInfo &fault) const
{
    if (fault.code == "WIFI_RFKILL_HARD_BLOCKED" || fault.code == "WIFI_RADIO_POWER_FAULT")
        return 3;
    if (fault.code == "WIFI_RFKILL_SOFT_BLOCKED" || fault.code == "WIFI_SCAN_FAILED")
        return 2;
    if (consecutive_failures_ >= 4)
        return 3;
    if (consecutive_failures_ >= 2)
        return 2;
    return 1;
}

bool WifiProvider::stageCoolingDownLocked(int stage, int64_t now_ms, int *remaining_ms) const
{
    if (stage < 1 || stage > 3)
        return false;
    const int64_t last_ms = stage_last_attempt_ms_[stage];
    const int cooldown = stageCooldownMs(stage);
    const int64_t elapsed = now_ms - last_ms;
    if (elapsed >= cooldown)
        return false;
    if (remaining_ms)
        *remaining_ms = static_cast<int>(cooldown - elapsed);
    return true;
}

int WifiProvider::stageCooldownMs(int stage) const
{
    switch (stage) {
    case 1:
        return kStage1CooldownMs;
    case 2:
        return kStage2CooldownMs;
    case 3:
    default:
        return kStage3CooldownMs;
    }
}

int WifiProvider::stageBackoffMs(int stage, int failure_count) const
{
    const int base = (stage == 1) ? kStage1BaseDelayMs : (stage == 2 ? kStage2BaseDelayMs : kStage3BaseDelayMs);
    const int max_delay = (stage == 1) ? kStage1MaxDelayMs : (stage == 2 ? kStage2MaxDelayMs : kStage3MaxDelayMs);
    const int capped = std::clamp(failure_count, 0, 4);
    int64_t delay = base;
    for (int i = 0; i < capped; ++i)
        delay *= 2;
    return static_cast<int>(std::min<int64_t>(delay, max_delay));
}

bool WifiProvider::writeConfigLocked() const
{
    std::ostringstream ss;
    ss << "ctrl_interface=/var/run/wpa_supplicant\n"
       << "update_config=1\n"
       << "country=" << config_.country << "\n"
       << "ap_scan=1\n"
       << "network={\n"
       << "    ssid=\"" << config_.ssid << "\"\n"
       << "    scan_ssid=1\n"
       << "    psk=\"" << config_.password << "\"\n"
       << "    key_mgmt=WPA-PSK\n"
       << "}\n";
    if (!writeTextFile(wpaConfigPath(), ss.str())) {
        return false;
    }
    ::chmod(wpaConfigPath().c_str(), 0600);
    return true;
}

bool WifiProvider::startWpaSupplicantLocked(std::string *detail)
{
    if (!writeConfigLocked()) {
        if (detail)
            *detail = "write wpa config failed";
        return false;
    }

    runProcess({"ip", "addr", "flush", "dev", config_.ifname}, 2000);
    runProcess({"wpa_cli", "-i", config_.ifname, "terminate"}, 2000);
    runProcess({"rm", "-f", "/var/run/wpa_supplicant/" + config_.ifname}, 2000);
    runProcess({"rm", "-f", wpaLogPath()}, 2000);

    const auto start = runProcess({
        "wpa_supplicant",
        "-B",
        "-i",
        config_.ifname,
        "-c",
        wpaConfigPath(),
        "-f",
        wpaLogPath(),
        "-t",
    }, 8000);
    if (start.timeout || start.exit_code != 0) {
        if (detail)
            *detail = "wpa_supplicant start failed: " + compactText(start.stdout_output);
        return false;
    }
    return true;
}

bool WifiProvider::waitForWifiConnectedLocked(int timeout_ms, std::string *detail) const
{
    int waited_ms = 0;
    while (waited_ms <= timeout_ms) {
        const auto status = runProcess({"wpa_cli", "-i", config_.ifname, "status"}, 2000);
        if (!status.timeout && status.exit_code == 0 &&
            status.stdout_output.find("wpa_state=COMPLETED") != std::string::npos) {
            const auto iw = runProcess({"iw", "dev", config_.ifname, "link"}, 2000);
            if (!iw.timeout && iw.exit_code == 0 &&
                iw.stdout_output.find("Connected") != std::string::npos) {
                return true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        waited_ms += 500;
    }

    if (detail)
        *detail = "wifi connect timeout";
    return false;
}

bool WifiProvider::softRestartLocked(uint32_t route_metric, std::string *detail)
{
    if (!setInterfaceDown(config_.ifname) && detail)
        *detail = "ip link down failed";
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    if (!setInterfaceUp(config_.ifname)) {
        if (detail)
            *detail = "ip link up failed";
        return false;
    }

    if (!startWpaSupplicantLocked(detail))
        return false;
    if (!waitForWifiConnectedLocked(15000, detail))
        return false;
    if (!requestDhcp(config_.ifname, 15000,
                     route_metric == 0 ? kWifiRouteMetric : route_metric)) {
        if (detail)
            *detail = "dhcp request failed";
        return false;
    }
    return true;
}

bool WifiProvider::rfkillUnlockLocked(std::string *detail)
{
    bool ok = true;
    if (!writeTextFile(rfkillStatePath(), "1\n"))
        ok = false;
    if (!writeTextFile(rfkillSoftPath(), "0\n"))
        ok = false;
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    if (!ok && detail)
        *detail = "rfkill unlock failed";
    return ok;
}

bool WifiProvider::powerCycleLocked(std::string *detail)
{
    bool ok = true;
    if (!writeTextFile(rfkillStatePath(), "0\n"))
        ok = false;
    if (!writeTextFile(wifiPowerPath(), "0\n"))
        ok = false;
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    if (!writeTextFile(wifiPowerPath(), "1\n"))
        ok = false;
    if (!writeTextFile(rfkillStatePath(), "1\n"))
        ok = false;
    if (!writeTextFile(rfkillSoftPath(), "0\n"))
        ok = false;
    std::this_thread::sleep_for(std::chrono::milliseconds(1800));
    if (!ok && detail)
        *detail = "wifi power cycle failed";
    return ok;
}

void WifiProvider::logFaultLocked(const FaultInfo &fault, const std::string &detail) const
{
    if (!logger_)
        return;

    std::ostringstream ss;
    ss << "code=" << fault.code
       << ", recovery_level=" << fault.recovery_level
       << ", retry_ms=" << fault.retry_delay_ms
       << ", failures=" << consecutive_failures_;
    if (!fault.message.empty())
        ss << ", reason=" << fault.message;
    if (!detail.empty() && detail != fault.message)
        ss << ", detail=" << detail;
    logger_->warn(faultModule(fault.code), ss.str());
}

void WifiProvider::refreshStatus(bool include_scan)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const FaultInfo fault = diagnoseLocked(include_scan);
    if (fault.code == "none" && interfaceHasIpv4(config_.ifname) && interfaceIsUp(config_.ifname)) {
        markSuccessLocked();
        return;
    }

    last_state_ = buildStateLocked();
    last_state_.available = false;
    last_state_.cloud_reachable = false;
    last_state_.fault_code = fault.code;
    last_state_.fault_message = fault.message;
    last_state_.retry_delay_ms = fault.retry_delay_ms;
    last_state_.failure_count = consecutive_failures_;
    last_state_.recovery_level = fault.recovery_level;
}

NetworkState WifiProvider::stateSnapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return last_state_;
}

bool WifiProvider::bringUp(uint32_t route_metric)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (wifiAlreadyReadyLocked()) {
        markSuccessLocked();
        if (logger_)
            logger_->info("WIFI", "wifi already ready on " + config_.ifname);
        return true;
    }

    const FaultInfo observed = diagnoseLocked(true);
    const int stage = selectRecoveryStageLocked(observed);
    const int64_t now_ms = common::nowMs();
    int remaining_ms = 0;
    if (stageCoolingDownLocked(stage, now_ms, &remaining_ms)) {
        FaultInfo cooldown = observed;
        cooldown.code = "WIFI_RECOVERY_COOLDOWN";
        cooldown.message = "recovery stage cooldown active";
        cooldown.retry_delay_ms = std::max(cooldown.retry_delay_ms, remaining_ms);
        cooldown.recovery_level = stage;
        markFailureLocked(cooldown);
        logFaultLocked(cooldown, "cooldown active");
        return false;
    }

    stage_last_attempt_ms_[stage] = now_ms;

    std::string detail;
    bool ok = false;
    if (stage == 1) {
        ok = softRestartLocked(route_metric, &detail);
    } else if (stage == 2) {
        ok = rfkillUnlockLocked(&detail) && softRestartLocked(route_metric, &detail);
    } else {
        ok = powerCycleLocked(&detail) &&
             rfkillUnlockLocked(&detail) &&
             softRestartLocked(route_metric, &detail);
    }

    if (ok) {
        markSuccessLocked();
        if (logger_)
            logger_->info("WIFI", "wifi recovery stage " + std::to_string(stage) +
                                     " succeeded on " + config_.ifname);
        return true;
    }

    FaultInfo fault = diagnoseLocked(true);
    if (fault.code == "none") {
        fault.code = "WIFI_BRINGUP_FAILED";
        fault.message = detail.empty() ? "wifi staged recovery failed" : detail;
        fault.retry_delay_ms = stageBackoffMs(stage, stage_failures_[stage]);
        fault.recovery_level = stage;
    }
    fault.recovery_level = std::max(fault.recovery_level, stage);
    fault.retry_delay_ms = std::max(fault.retry_delay_ms, stageBackoffMs(stage, stage_failures_[stage]));
    markFailureLocked(fault);
    logFaultLocked(fault, detail);
    return false;
}

bool WifiProvider::isInterfaceUp() const
{
    return defaultIsInterfaceUp(config_.ifname);
}

bool WifiProvider::hasIp() const
{
    return defaultHasIp(config_.ifname);
}

bool WifiProvider::canReachCloud(const std::string &host,
                                 int port,
                                 std::string *reason) const
{
    return defaultCanReachCloud(host, port, config_.ifname, reason);
}

} // namespace gateway::network
