#include "config/config_manager.h"

#include "common/constants.h"
#include "common/file_utils.h"

#include <algorithm>
#include <set>

namespace gateway::config {
namespace {

model::DeviceInfo parseDevice(const nlohmann::json &item)
{
    model::DeviceInfo device;
    device.device_id = item.value("device_id", "");
    device.product_id = item.value("product_id", "");
    device.name = item.value("name", "");
    device.type = model::deviceTypeFromString(item.value("type", "gateway"));
    device.meter_role = model::meterRoleFromString(item.value("meter_role", ""));
    device.parent_meter_id = item.value("parent_meter_id", "");
    device.mac = item.value("mac", "");
    device.parent_mac = item.value("parent_mac", "");
    device.child_macs = item.value("child_macs", "");
    device.modbus_addr = item.value("modbus_addr", 0);
    device.modbus_type = item.value("modbus_type", 0);
    device.online = item.value("online", true);
    return device;
}

} // namespace

bool ConfigManager::load(const std::string &path, std::string *error)
{
    std::string text;
    if (!common::readText(path, &text) || text.empty()) {
        if (error)
            *error = "failed to open config: " + path;
        return false;
    }

    nlohmann::json root;
    try {
        root = nlohmann::json::parse(text);
    } catch (const std::exception &e) {
        if (error)
            *error = std::string("failed to parse config json: ") + e.what();
        return false;
    }

    AppConfig cfg;

    const auto gateway = root.value("gateway", nlohmann::json::object());
    cfg.gateway.gateway_id = gateway.value("gateway_id", "RK3506_GW_001");
    cfg.gateway.name = gateway.value("name", "RK3506 Gateway");
    cfg.gateway.version = gateway.value("version", "0.1.0");

    const auto thingskit = root.value("thingskit", nlohmann::json::object());
    cfg.thingskit.protocol = thingskit.value("protocol", "mqtt");
    cfg.thingskit.host = thingskit.value("host", "");
    cfg.thingskit.port = thingskit.value("port", 1883);
    cfg.thingskit.client_id = thingskit.value("client_id", cfg.gateway.gateway_id);
    cfg.thingskit.credential_mode = thingskit.value("credential_mode", "access_token");
    cfg.thingskit.access_token = thingskit.value("access_token", "");
    const auto mqtt_basic = thingskit.value("mqtt_basic", nlohmann::json::object());
    cfg.thingskit.basic_client_id = mqtt_basic.value("client_id", cfg.thingskit.client_id);
    cfg.thingskit.basic_username = mqtt_basic.value("username", thingskit.value("username", ""));
    cfg.thingskit.basic_password = mqtt_basic.value("password", thingskit.value("password", ""));
    if (cfg.thingskit.credential_mode == "mqtt_basic") {
        cfg.thingskit.client_id = cfg.thingskit.basic_client_id;
        cfg.thingskit.username = cfg.thingskit.basic_username;
        cfg.thingskit.password = cfg.thingskit.basic_password;
    } else {
        cfg.thingskit.username = cfg.thingskit.access_token.empty()
                                     ? thingskit.value("username", cfg.gateway.gateway_id)
                                     : cfg.thingskit.access_token;
        cfg.thingskit.password = "";
    }
    cfg.thingskit.keepalive = thingskit.value("keepalive", 60);
    cfg.thingskit.topic_prefix = thingskit.value("topic_prefix", "devices");

    const auto network = root.value("network", nlohmann::json::object());
    cfg.network.mode = network.value("mode", "auto");
    cfg.network.cloud_test_host = network.value("cloud_test_host", cfg.thingskit.host);
    cfg.network.cloud_test_port = network.value("cloud_test_port", cfg.thingskit.port);
    cfg.network.priority.clear();
    for (const auto &item : network.value("priority", nlohmann::json::array({"ethernet", "wifi", "cellular"})))
        cfg.network.priority.push_back(item.get<std::string>());

    const auto ethernet = network.value("ethernet", nlohmann::json::object());
    cfg.network.ethernet.enable = ethernet.value("enable", true);
    cfg.network.ethernet.ifname = ethernet.value("ifname", "eth0");

    const auto wifi = network.value("wifi", nlohmann::json::object());
    cfg.network.wifi.enable = wifi.value("enable", true);
    cfg.network.wifi.ifname = wifi.value("ifname", "wlan0");
    cfg.network.wifi.ssid = wifi.value("ssid", "");
    cfg.network.wifi.password = wifi.value("password", "");
    cfg.network.wifi.country = wifi.value("country", "CN");

    const auto cellular = network.value("cellular", nlohmann::json::object());
    cfg.network.cellular.enable = cellular.value("enable", true);
    cfg.network.cellular.ifname = cellular.value("ifname", "ppp0");
    cfg.network.cellular.module = cellular.value("module", "L610");
    cfg.network.cellular.serial_device = cellular.value("serial_device", "/dev/ttyS1");
    cfg.network.cellular.baudrate = cellular.value("baudrate", 115200);
    cfg.network.cellular.apn = cellular.value("apn", "cmnet");

    const auto publish = root.value("publish", nlohmann::json::object());
    cfg.publish.interval_ms = publish.value("interval_ms", 5000);
    cfg.publish.gateway_status_interval_ms =
        publish.value("gateway_status_interval_ms", 10000);
    cfg.publish.cache_ttl_ms =
        publish.value("cache_ttl_ms", common::kDefaultCacheTtlMs);
    cfg.publish.enable_cache = publish.value("enable_cache", true);

    const auto log = root.value("log", nlohmann::json::object());
    cfg.log.dir = log.value("dir", common::kDefaultLogDir);
    cfg.log.level = log.value("level", "info");

    const auto mock = root.value("mock", nlohmann::json::object());
    cfg.mock.voltage_base = mock.value("voltage_base", 220.0);
    cfg.mock.frequency_base = mock.value("frequency_base", 50.0);
    cfg.mock.temperature_base = mock.value("temperature_base", 28.0);
    cfg.mock.humidity_base = mock.value("humidity_base", 60.0);

    cfg.devices.clear();
    for (const auto &item : root.value("devices", nlohmann::json::array()))
        cfg.devices.push_back(parseDevice(item));

    if (!validate(cfg, error))
        return false;

    config_ = std::move(cfg);
    return true;
}

bool ConfigManager::validate(const AppConfig &config, std::string *error) const
{
    auto fail = [&](const std::string &message) {
        if (error)
            *error = message;
        return false;
    };

    if (config.gateway.gateway_id.empty())
        return fail("gateway.gateway_id must not be empty");
    if (config.thingskit.host.empty())
        return fail("thingskit.host must not be empty");
    if (config.thingskit.port < 1 || config.thingskit.port > 65535)
        return fail("thingskit.port must be 1-65535");
    if (config.thingskit.credential_mode != "access_token" &&
        config.thingskit.credential_mode != "mqtt_basic")
        return fail("thingskit.credential_mode must be access_token/mqtt_basic");
    if (config.thingskit.credential_mode == "access_token" &&
        config.thingskit.access_token.empty())
        return fail("thingskit.access_token must not be empty in access_token mode");
    if (config.thingskit.credential_mode == "mqtt_basic" &&
        (config.thingskit.basic_client_id.empty() ||
         config.thingskit.basic_username.empty()))
        return fail("thingskit.mqtt_basic client_id/username must not be empty in mqtt_basic mode");
    if (config.devices.empty())
        return fail("devices must not be empty");
    if (config.publish.interval_ms <= 0)
        return fail("publish.interval_ms must be positive");
    if (config.publish.gateway_status_interval_ms <= 0)
        return fail("publish.gateway_status_interval_ms must be positive");
    if (config.publish.cache_ttl_ms <= 0)
        return fail("publish.cache_ttl_ms must be positive");

    const std::set<std::string> allowed_modes = {"auto", "ethernet", "wifi", "cellular"};
    if (!allowed_modes.count(config.network.mode))
        return fail("network.mode must be auto/ethernet/wifi/cellular");

    const std::set<std::string> allowed_priority = {"ethernet", "wifi", "cellular"};
    for (const auto &item : config.network.priority) {
        if (!allowed_priority.count(item))
            return fail("network.priority contains unsupported item: " + item);
    }

    if (config.network.cloud_test_host.empty())
        return fail("network.cloud_test_host must not be empty");
    if (config.network.cloud_test_port < 1 || config.network.cloud_test_port > 65535)
        return fail("network.cloud_test_port must be 1-65535");

    std::set<std::string> device_ids;
    for (const auto &device : config.devices) {
        if (device.device_id.empty())
            return fail("devices contains empty device_id");
        if (!device_ids.insert(device.device_id).second)
            return fail("devices contains duplicate device_id: " + device.device_id);
    }

    return true;
}

} // namespace gateway::config
