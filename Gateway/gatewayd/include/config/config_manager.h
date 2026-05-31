#pragma once

#include "common/device_model.h"
#include "common/constants.h"

#include <cstdint>
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
    std::string ifname = "ppp0";
    std::string module = "L610";
    std::string serial_device = "/dev/ttyS1";
    int baudrate = 115200;
    std::string apn = "cmnet";
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

// 运行期总配置对象。GatewayApp 初始化后把它按引用传给各个 worker/manager。
struct AppConfig {
    GatewayConfig gateway;
    ThingsKitConfig thingskit;
    NetworkConfig network;
    PublishConfig publish;
    LogConfig log;
    MockConfig mock;
    std::vector<model::DeviceInfo> devices;
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
