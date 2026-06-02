#include "common/device_model.h"

#include <cstdio>

namespace gateway::model {
namespace {

void eraseTelemetryKey(TelemetryData &data, const std::string &key)
{
    data.integer_values.erase(key);
    data.numeric_values.erase(key);
    data.string_values.erase(key);
    data.bool_values.erase(key);
    data.object_values.erase(key);
}

} // namespace

nlohmann::json TelemetryData::toFlatJson() const
{
    nlohmann::json values = nlohmann::json::object();

    for (const auto &[key, value] : integer_values)
        values[key] = value;
    for (const auto &[key, value] : numeric_values)
        values[key] = value;
    for (const auto &[key, value] : string_values)
        values[key] = value;
    for (const auto &[key, value] : bool_values)
        values[key] = value;
    for (const auto &[key, value] : object_values)
        values[key] = value;

    return values;
}

void TelemetryData::mergeFromFlatJson(const nlohmann::json &values)
{
    if (!values.is_object())
        return;

    for (const auto &[key, value] : values.items()) {
        eraseTelemetryKey(*this, key);
        if (value.is_boolean()) {
            bool_values[key] = value.get<bool>();
        } else if (value.is_number_integer() || value.is_number_unsigned()) {
            integer_values[key] = value.get<int64_t>();
        } else if (value.is_number()) {
            numeric_values[key] = value.get<double>();
        } else if (value.is_string()) {
            string_values[key] = value.get<std::string>();
        } else if (value.is_object() || value.is_array()) {
            object_values[key] = value;
        }
    }
}

DeviceType deviceTypeFromString(const std::string &text)
{
    if (text == "single_phase_meter")
        return DeviceType::SinglePhaseMeter;
    if (text == "env_sensor")
        return DeviceType::EnvSensor;
    if (text == "relay_device")
        return DeviceType::Relay;
    if (text == "dtu_node")
        return DeviceType::DtuNode;
    return DeviceType::Gateway;
}

std::string toString(DeviceType type)
{
    switch (type) {
    case DeviceType::SinglePhaseMeter:
        return "single_phase_meter";
    case DeviceType::EnvSensor:
        return "env_sensor";
    case DeviceType::Relay:
        return "relay_device";
    case DeviceType::DtuNode:
        return "dtu_node";
    case DeviceType::Gateway:
    default:
        return "gateway";
    }
}

std::string generateDeviceName(DeviceType type, int station_id)
{
    const char *prefix = "DEV_";
    switch (type) {
    case DeviceType::SinglePhaseMeter:
        prefix = "METER_";
        break;
    case DeviceType::Relay:
        prefix = "RELAY_";
        break;
    case DeviceType::EnvSensor:
        prefix = "ENV_";
        break;
    default:
        break;
    }
    char buf[16];
    snprintf(buf, sizeof(buf), "%s%03d", prefix, station_id);
    return buf;
}

std::string generateDtuName(int node_id)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "DTU_%03d", node_id);
    return buf;
}

uint8_t sleRoleToCloudRole(uint8_t sle_role)
{
    // SLE: Root(1) → 云端 0 (root), Relay(2)/Leaf(3) → 云端 1 (node)
    return (sle_role == 1) ? 0 : 1;
}

} // namespace gateway::model
