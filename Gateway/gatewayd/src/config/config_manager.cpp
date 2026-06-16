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
    device.station_id = item.value("station_id", 0);
    device.dtu_id = item.value("dtu_id", 0);
    device.modbus_addr = item.value("modbus_addr", 0);
    device.modbus_type = item.value("modbus_type", 0);
    device.online = item.value("online", true);
    // V1 兼容
    device.dtu_node_id = item.value("dtu_node_id", 0);
    return device;
}

model::DtuDeviceInfo parseDtuDevice(const nlohmann::json &item)
{
    model::DtuDeviceInfo dtu;
    dtu.device_id = item.value("device_id", "");
    dtu.node_id = item.value("node_id", 0);
    dtu.parent_id = item.value("parent_id", 0);
    dtu.child_ids = item.value("child_ids", "");
    return dtu;
}

MeterRuleConfig parseMeterRuleConfig(const nlohmann::json &item)
{
    MeterRuleConfig config;
    config.nominal_voltage_v = item.value("nominal_voltage_v", config.nominal_voltage_v);
    config.over_voltage_v = item.value("over_voltage_v", config.over_voltage_v);
    config.under_voltage_v = item.value("under_voltage_v", config.under_voltage_v);
    config.frequency_low_hz = item.value("frequency_low_hz", config.frequency_low_hz);
    config.frequency_high_hz = item.value("frequency_high_hz", config.frequency_high_hz);
    config.rated_current_a = item.value("rated_current_a", config.rated_current_a);
    config.over_current_ratio = item.value("over_current_ratio", config.over_current_ratio);
    config.hold_ms = item.value("hold_ms", config.hold_ms);
    return config;
}

EnvRuleConfig parseEnvRuleConfig(const nlohmann::json &item)
{
    EnvRuleConfig config;
    config.high_temperature_c = item.value("high_temperature_c", config.high_temperature_c);
    config.high_humidity_rh = item.value("high_humidity_rh", config.high_humidity_rh);
    config.hold_ms = item.value("hold_ms", config.hold_ms);
    return config;
}

DtuRuleConfig parseDtuRuleConfig(const nlohmann::json &item)
{
    DtuRuleConfig config;
    config.offline_timeout_ms = item.value("offline_timeout_ms", config.offline_timeout_ms);
    return config;
}

RuleDeviceOverride parseRuleDeviceOverride(const nlohmann::json &item)
{
    auto readDoubleOverride = [&](const char *key) {
        if (!item.contains(key))
            return 0.0;
        const double value = item.value(key, -1.0);
        return value > 0.0 ? value : -1.0;
    };
    auto readIntOverride = [&](const char *key) {
        if (!item.contains(key))
            return 0;
        const int value = item.value(key, -1);
        return value > 0 ? value : -1;
    };

    RuleDeviceOverride config;
    config.over_voltage_v = readDoubleOverride("over_voltage_v");
    config.under_voltage_v = readDoubleOverride("under_voltage_v");
    config.frequency_low_hz = readDoubleOverride("frequency_low_hz");
    config.frequency_high_hz = readDoubleOverride("frequency_high_hz");
    config.rated_current_a = readDoubleOverride("rated_current_a");
    config.over_current_ratio = readDoubleOverride("over_current_ratio");
    config.high_temperature_c = readDoubleOverride("high_temperature_c");
    config.high_humidity_rh = readDoubleOverride("high_humidity_rh");
    config.hold_ms = readIntOverride("hold_ms");
    config.offline_timeout_ms = readIntOverride("offline_timeout_ms");
    return config;
}

MeterRuleConfig applyMeterOverride(MeterRuleConfig config, const RuleDeviceOverride &override)
{
    if (override.over_voltage_v != 0.0)
        config.over_voltage_v = override.over_voltage_v;
    if (override.under_voltage_v != 0.0)
        config.under_voltage_v = override.under_voltage_v;
    if (override.frequency_low_hz != 0.0)
        config.frequency_low_hz = override.frequency_low_hz;
    if (override.frequency_high_hz != 0.0)
        config.frequency_high_hz = override.frequency_high_hz;
    if (override.rated_current_a != 0.0)
        config.rated_current_a = override.rated_current_a;
    if (override.over_current_ratio != 0.0)
        config.over_current_ratio = override.over_current_ratio;
    if (override.hold_ms != 0)
        config.hold_ms = override.hold_ms;
    return config;
}

EnvRuleConfig applyEnvOverride(EnvRuleConfig config, const RuleDeviceOverride &override)
{
    if (override.high_temperature_c != 0.0)
        config.high_temperature_c = override.high_temperature_c;
    if (override.high_humidity_rh != 0.0)
        config.high_humidity_rh = override.high_humidity_rh;
    if (override.hold_ms != 0)
        config.hold_ms = override.hold_ms;
    return config;
}

DtuRuleConfig applyDtuOverride(DtuRuleConfig config, const RuleDeviceOverride &override)
{
    if (override.offline_timeout_ms != 0)
        config.offline_timeout_ms = override.offline_timeout_ms;
    return config;
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

    const auto sle = root.value("sle", nlohmann::json::object());
    cfg.sle.enable = sle.value("enable", false);
    cfg.sle.data_socket = sle.value("data_socket", "/var/run/gateway/sle_data.sock");
    cfg.sle.cmd_socket = sle.value("cmd_socket", "/var/run/gateway/sle_cmd.sock");
    cfg.sle.roots.clear();
    for (const auto &root_item : sle.value("roots", nlohmann::json::array())) {
        config::SleRootConfig rc;
        rc.node_id = root_item.value("node_id", 0);
        cfg.sle.roots.push_back(rc);
    }

    const auto offline = root.value("offline_analysis", nlohmann::json::object());
    cfg.offline_analysis.enable = offline.value("enable", true);
    cfg.offline_analysis.offline_only = offline.value("offline_only", true);
    cfg.offline_analysis.enter_hold_ms =
        offline.value("enter_hold_ms", cfg.offline_analysis.enter_hold_ms);
    cfg.offline_analysis.exit_hold_ms =
        offline.value("exit_hold_ms", cfg.offline_analysis.exit_hold_ms);

    const auto rule_engine = offline.value("rule_engine", nlohmann::json::object());
    cfg.offline_analysis.rule_engine.enable = rule_engine.value("enable", true);
    cfg.offline_analysis.rule_engine.cooldown_ms =
        rule_engine.value("cooldown_ms", cfg.offline_analysis.rule_engine.cooldown_ms);
    const auto defaults = rule_engine.value("defaults", nlohmann::json::object());
    cfg.offline_analysis.rule_engine.meter = parseMeterRuleConfig(
        defaults.value("single_phase_meter", nlohmann::json::object()));
    cfg.offline_analysis.rule_engine.env = parseEnvRuleConfig(
        defaults.value("env_sensor", nlohmann::json::object()));
    cfg.offline_analysis.rule_engine.dtu = parseDtuRuleConfig(
        defaults.value("dtu_node", nlohmann::json::object()));
    cfg.offline_analysis.rule_engine.device_overrides.clear();
    const auto device_overrides = rule_engine.value("device_overrides", nlohmann::json::object());
    for (const auto &item : device_overrides.items())
        cfg.offline_analysis.rule_engine.device_overrides[item.key()] =
            parseRuleDeviceOverride(item.value());

    const auto offline_control = offline.value("offline_control", nlohmann::json::object());
    cfg.offline_analysis.offline_control.enable = offline_control.value("enable", true);
    cfg.offline_analysis.offline_control.offline_only = offline_control.value("offline_only", true);
    cfg.offline_analysis.offline_control.relay_close_on_recovery =
        offline_control.value("relay_close_on_recovery", true);
    cfg.offline_analysis.offline_control.relay_devices.clear();
    for (const auto &item : offline_control.value("relay_devices", nlohmann::json::array()))
        cfg.offline_analysis.offline_control.relay_devices.push_back(item.get<std::string>());

    const auto ai = offline.value("ai", nlohmann::json::object());
    cfg.offline_analysis.ai.enable = ai.value("enable", false);

    cfg.devices.clear();
    cfg.dtu_devices.clear();
    for (const auto &item : root.value("devices", nlohmann::json::array())) {
        std::string type = item.value("type", "gateway");
        if (type == "dtu_node")
            cfg.dtu_devices.push_back(parseDtuDevice(item));
        else
            cfg.devices.push_back(parseDevice(item));
    }

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

    const auto &offline = config.offline_analysis;
    if (offline.enable) {
        if (!offline.offline_only)
            return fail("offline_analysis.offline_only must be true");
        if (offline.enter_hold_ms <= 0)
            return fail("offline_analysis.enter_hold_ms must be positive");
        if (offline.exit_hold_ms <= 0)
            return fail("offline_analysis.exit_hold_ms must be positive");
        if (offline.ai.enable)
            return fail("offline_analysis.ai.enable is reserved and must be false");
        if (offline.offline_control.enable && !offline.offline_control.offline_only)
            return fail("offline_analysis.offline_control.offline_only must be true");

        const auto &rules = offline.rule_engine;
        if (rules.enable) {
            if (rules.cooldown_ms <= 0)
                return fail("offline_analysis.rule_engine.cooldown_ms must be positive");
            if (rules.meter.nominal_voltage_v <= 0.0 ||
                rules.meter.over_voltage_v <= 0.0 ||
                rules.meter.under_voltage_v <= 0.0 ||
                rules.meter.frequency_low_hz <= 0.0 ||
                rules.meter.frequency_high_hz <= 0.0 ||
                rules.meter.rated_current_a <= 0.0 ||
                rules.meter.over_current_ratio <= 0.0)
                return fail("offline_analysis.rule_engine meter thresholds must be positive");
            if (rules.meter.under_voltage_v >= rules.meter.over_voltage_v)
                return fail("offline_analysis.rule_engine meter voltage low must be below high");
            if (rules.meter.frequency_low_hz >= rules.meter.frequency_high_hz)
                return fail("offline_analysis.rule_engine meter frequency low must be below high");
            if (rules.meter.hold_ms <= 0)
                return fail("offline_analysis.rule_engine meter hold_ms must be positive");
            if (rules.env.high_temperature_c <= 0.0 || rules.env.high_humidity_rh <= 0.0)
                return fail("offline_analysis.rule_engine env thresholds must be positive");
            if (rules.env.hold_ms <= 0)
                return fail("offline_analysis.rule_engine env hold_ms must be positive");
            if (rules.dtu.offline_timeout_ms <= 0)
                return fail("offline_analysis.rule_engine dtu offline_timeout_ms must be positive");
        }
    }

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

    std::map<std::string, model::DeviceType> device_types;
    for (const auto &device : config.devices)
        device_types[device.device_id] = device.type;
    for (const auto &device : config.dtu_devices)
        device_types[device.device_id] = model::DeviceType::DtuNode;

    if (config.offline_analysis.enable &&
        config.offline_analysis.offline_control.enable) {
        for (const auto &relay_device_id : config.offline_analysis.offline_control.relay_devices) {
            const auto type_it = device_types.find(relay_device_id);
            if (type_it == device_types.end())
                return fail("offline_analysis.offline_control.relay_devices unknown device: " +
                            relay_device_id);
            if (type_it->second != model::DeviceType::Relay)
                return fail("offline_analysis.offline_control.relay_devices must be relay device: " +
                            relay_device_id);
        }
    }

    for (const auto &item : config.offline_analysis.rule_engine.device_overrides) {
        const auto type_it = device_types.find(item.first);
        if (type_it == device_types.end())
            return fail("offline_analysis.rule_engine.device_overrides unknown device: " +
                        item.first);

        const auto &override = item.second;
        if (override.over_voltage_v < 0.0 || override.under_voltage_v < 0.0 ||
            override.frequency_low_hz < 0.0 || override.frequency_high_hz < 0.0 ||
            override.rated_current_a < 0.0 || override.over_current_ratio < 0.0 ||
            override.high_temperature_c < 0.0 || override.high_humidity_rh < 0.0 ||
            override.hold_ms < 0 || override.offline_timeout_ms < 0)
            return fail("offline_analysis.rule_engine.device_overrides values must be positive: " +
                        item.first);

        if (type_it->second == model::DeviceType::SinglePhaseMeter) {
            const auto meter = applyMeterOverride(
                config.offline_analysis.rule_engine.meter, override);
            if (meter.under_voltage_v >= meter.over_voltage_v)
                return fail("offline_analysis.rule_engine.device_overrides voltage low must be below high: " +
                            item.first);
            if (meter.frequency_low_hz >= meter.frequency_high_hz)
                return fail("offline_analysis.rule_engine.device_overrides frequency low must be below high: " +
                            item.first);
        } else if (type_it->second == model::DeviceType::EnvSensor) {
            const auto env = applyEnvOverride(config.offline_analysis.rule_engine.env, override);
            if (env.high_temperature_c <= 0.0 || env.high_humidity_rh <= 0.0 ||
                env.hold_ms <= 0)
                return fail("offline_analysis.rule_engine.device_overrides env values must be positive: " +
                            item.first);
        } else if (type_it->second == model::DeviceType::DtuNode) {
            const auto dtu = applyDtuOverride(config.offline_analysis.rule_engine.dtu, override);
            if (dtu.offline_timeout_ms <= 0)
                return fail("offline_analysis.rule_engine.device_overrides dtu offline_timeout_ms must be positive: " +
                            item.first);
        }
    }

    return true;
}

} // namespace gateway::config
