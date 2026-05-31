#include "common/device_model.h"

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

MeterRole meterRoleFromString(const std::string &text)
{
    if (text == "main")
        return MeterRole::Main;
    if (text == "branch")
        return MeterRole::Branch;
    return MeterRole::None;
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

std::string toString(MeterRole role)
{
    switch (role) {
    case MeterRole::Main:
        return "main";
    case MeterRole::Branch:
        return "branch";
    case MeterRole::None:
    default:
        return "none";
    }
}

} // namespace gateway::model
