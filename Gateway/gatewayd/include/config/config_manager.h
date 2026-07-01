#pragma once

#include "common/device_model.h"
#include "common/constants.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace gateway::config {

// gatewayd 的配置结构基本对应 gateway_config.json。
// ConfigManager 负责解析、填默认值和做启动前校验。
struct GatewayConfig {
    std::string gateway_id;
    std::string name;
    std::string version;
};

struct ThingsKitConfig {
    std::string protocol;
    std::string host;
    int port = 1883;
    std::string client_id;
    std::string username;
    std::string password;
    std::string credential_mode = "access_token";
    std::string access_token;
    std::string basic_client_id;
    std::string basic_username;
    std::string basic_password;
    int keepalive = 60;
    std::string topic_prefix = "devices";
};

// 发布周期和本地缓存策略。
// SQLite-only 后，缓存路径固定使用 common::kDefaultDbPath，不再暴露 cache_path。
struct PublishConfig {
    int interval_ms = 5000;
    int gateway_status_interval_ms = 10000;
    int64_t cache_ttl_ms = common::kDefaultCacheTtlMs;
    bool enable_cache = true;
};

struct LogConfig {
    std::string dir = common::kDefaultLogDir;
    std::string level = "info";
};

struct MockConfig {
    double voltage_base = 220.0;
    double frequency_base = 50.0;
    double temperature_base = 28.0;
    double humidity_base = 60.0;
};

// SLE 数据源配置。enable=true 时使用 SleDataSource 替代 MockDataSource。
struct SleConfig {
    bool enable = false;
    std::string data_socket = "/var/run/gateway/sle_data.sock";
    std::string cmd_socket = "/var/run/gateway/sle_cmd.sock";
};

struct TopologyOnlinePolicyConfig {
    bool dtu_from_topology_snapshot = true;
    bool external_from_device_map = true;
    bool external_inherits_dtu_online = true;
    bool emit_online_change = true;
    bool missing_dtu_online = false;
    bool missing_external_online = false;
};

struct StaticTopologyConfig {
    bool enable_for_test = false;
};

struct DynamicTopologyConfig {
    std::vector<std::string> required_frames = {"dtu_topology", "external_map"};
    int startup_timeout_ms = 30000;
    std::string persist_path = "/userdata/gateway/data/dynamic_topology.json";
    bool allow_fallback_to_static = false;
};

struct TopologyConfig {
    std::string source = "root_report";
    int expected_dtu_count = 0;
    int expected_external_device_count = 0;
    TopologyOnlinePolicyConfig online_policy;
    StaticTopologyConfig static_json;
    DynamicTopologyConfig dynamic;
};

struct EthernetConfig {
    bool enable = true;
    std::string ifname = "eth0";
};

struct WifiConfig {
    bool enable = true;
    std::string ifname = "wlan0";
    std::string ssid;
    std::string password;
    std::string country = "CN";
};

struct CellularConfig {
    bool enable = true;
    std::string ifname = "cell0";
    std::string module = "ML307";
    std::string serial_device = "/dev/ttyUSB2";
    int baudrate = 115200;
    std::string apn = "cmnet";
    std::string mode = "ecm_rndis";
};

struct NetworkConfig {
    std::string mode = "auto";
    std::string cloud_test_host;
    int cloud_test_port = 1883;
    std::vector<std::string> priority = {"ethernet", "wifi", "cellular"};
    EthernetConfig ethernet;
    WifiConfig wifi;
    CellularConfig cellular;
};

struct MeterRuleConfig {
    double nominal_voltage_v = 220.0;
    double over_voltage_v = 235.4;
    double under_voltage_v = 198.0;
    double frequency_low_hz = 49.8;
    double frequency_high_hz = 50.2;
    double rated_current_a = 60.0;
    double over_current_ratio = 1.1;
    int hold_ms = 10000;
};

struct EnvRuleConfig {
    double high_temperature_c = 55.0;
    double high_humidity_rh = 90.0;
    int hold_ms = 30000;
};

struct DtuRuleConfig {
    int offline_timeout_ms = 60000;
};

// 设备级覆盖项使用 0 作为“未覆盖”哨兵值。
struct RuleDeviceOverride {
    double over_voltage_v = 0.0;
    double under_voltage_v = 0.0;
    double frequency_low_hz = 0.0;
    double frequency_high_hz = 0.0;
    double rated_current_a = 0.0;
    double over_current_ratio = 0.0;
    double high_temperature_c = 0.0;
    double high_humidity_rh = 0.0;
    int hold_ms = 0;
    int offline_timeout_ms = 0;
};

struct RuleEngineConfig {
    bool enable = true;
    int cooldown_ms = 60000;
    MeterRuleConfig meter;
    EnvRuleConfig env;
    DtuRuleConfig dtu;
    std::map<std::string, RuleDeviceOverride> device_overrides;
};

struct AiConfig {
    bool enable = false;
    bool offline_only = true;
    std::string mode = "linear_score";
    std::string model_path = "/userdata/gateway/models/offline_ai_model.json";
    int window_ms = 300000;
    int min_samples = 12;
    int cooldown_ms = 300000;
    double risk_threshold_medium = 0.55;
    double risk_threshold_high = 0.8;
};

struct OfflineControlConfig {
    bool enable = true;
    bool offline_only = true;
    bool relay_close_on_recovery = true;
    std::vector<std::string> relay_devices;
};

struct OfflineAnalysisConfig {
    bool enable = true;
    bool offline_only = true;
    int enter_hold_ms = 10000;
    int exit_hold_ms = 30000;
    RuleEngineConfig rule_engine;
    OfflineControlConfig offline_control;
    AiConfig ai;
};

// 运行期总配置对象。GatewayApp 初始化后把它按引用传给各个 worker/manager。
struct AppConfig {
    GatewayConfig gateway;
    ThingsKitConfig thingskit;
    NetworkConfig network;
    PublishConfig publish;
    LogConfig log;
    MockConfig mock;
    SleConfig sle;
    TopologyConfig topology;
    OfflineAnalysisConfig offline_analysis;
    std::vector<model::DeviceInfo> devices;         // 外接设备（电表/继电器/温湿度）
    std::vector<model::DtuDeviceInfo> dtu_devices;  // DTU 节点
};

class ConfigManager {
public:
    // 读取并校验 JSON 配置；失败时 error 返回可直接打印的原因。
    bool load(const std::string &path, std::string *error);
    const AppConfig &config() const { return config_; }

private:
    // 做跨字段校验，例如 MQTT 认证模式、网络优先级、设备 ID 唯一性。
    bool validate(const AppConfig &config, std::string *error) const;

    AppConfig config_;
};

} // namespace gateway::config
