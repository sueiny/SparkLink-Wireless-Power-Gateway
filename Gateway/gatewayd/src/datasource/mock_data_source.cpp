#include "datasource/mock_data_source.h"

#include "common/time_utils.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace gateway::datasource {
namespace {

constexpr double kInitialMainMeterEnergy = 10523.8;
constexpr double kInitialBranchMeterEnergy = 3000.0;
constexpr double kBranchEnergyStep = 121.6;
constexpr double kBranchPhaseOffset = 0.85;
constexpr double kBranchTickPhase = 0.33;
constexpr double kBaseCurrent = 2.4;
constexpr double kCurrentStep = 1.05;

double round2(double value)
{
    return std::round(value * 100.0) / 100.0;
}

int countCsvItems(const std::string &text)
{
    if (text.empty())
        return 0;

    int count = 1;
    for (char ch : text) {
        if (ch == ',')
            ++count;
    }
    return count;
}

bool isRootDtuParent(const std::string &parent_mac)
{
    return parent_mac.empty() || parent_mac == "00:11:22:33:44:55";
}

int meterRoleEnumValue(model::MeterRole role)
{
    switch (role) {
    case model::MeterRole::Main:
        return 1;
    case model::MeterRole::Branch:
    case model::MeterRole::None:
    default:
        return 0;
    }
}

} // namespace

MockDataSource::MockDataSource(std::vector<model::DeviceInfo> devices,
                               config::MockConfig mock,
                               int publish_interval_ms,
                               std::shared_ptr<state::DeviceStateStore> state_store)
    : devices_(std::move(devices)),
      mock_(mock),
      publish_interval_ms_(publish_interval_ms > 0 ? publish_interval_ms : 5000),
      state_store_(std::move(state_store))
{
}

bool MockDataSource::init()
{
    int branch_index = 0;
    for (const auto &device : devices_) {
        if (device.type != model::DeviceType::SinglePhaseMeter)
            continue;

        if (device.meter_role == model::MeterRole::Main)
            energy_kwh_[device.device_id] = kInitialMainMeterEnergy;
        else if (device.meter_role == model::MeterRole::Branch)
            energy_kwh_[device.device_id] =
                kInitialBranchMeterEnergy + (++branch_index * kBranchEnergyStep);
    }

    return true;
}

std::vector<model::TelemetryData> MockDataSource::collect()
{
    const int64_t ts = common::nowMs();
    std::vector<model::TelemetryData> result;
    std::vector<model::DeviceInfo> main_meters;
    double branch_power_sum = 0.0;
    double branch_current_sum = 0.0;
    int branch_index = 0;

    ++tick_;

    for (const auto &device : devices_) {
        if (device.type == model::DeviceType::SinglePhaseMeter &&
            device.meter_role == model::MeterRole::Main) {
            main_meters.push_back(device);
        }
    }

    std::vector<model::TelemetryData> branch_meters;
    for (const auto &device : devices_) {
        if (device.type == model::DeviceType::SinglePhaseMeter &&
            device.meter_role == model::MeterRole::Branch) {
            auto data = buildBranchMeter(device, branch_index++, ts);
            branch_power_sum += data.numeric_values["active_power"];
            branch_current_sum += data.numeric_values["current"];
            branch_meters.push_back(std::move(data));
        }
    }

    for (const auto &device : main_meters)
        result.push_back(buildMainMeter(device, ts, branch_power_sum, branch_current_sum));
    result.insert(result.end(),
                  std::make_move_iterator(branch_meters.begin()),
                  std::make_move_iterator(branch_meters.end()));

    for (const auto &device : devices_) {
        if (device.type == model::DeviceType::EnvSensor)
            result.push_back(buildEnvSensor(device, ts));
        else if (device.type == model::DeviceType::Relay)
            result.push_back(buildRelay(device, ts));
        else if (device.type == model::DeviceType::DtuNode)
            result.push_back(buildDtuNode(device, ts));
    }

    return result;
}

model::TelemetryData MockDataSource::buildBranchMeter(const model::DeviceInfo &device,
                                                       int branch_index,
                                                       int64_t ts_ms)
{
    const double phase = tick_ * kBranchTickPhase + branch_index * kBranchPhaseOffset;
    const double voltage = mock_.voltage_base + std::sin(phase) * 1.8;
    const double current = kBaseCurrent + branch_index * kCurrentStep +
                           (std::sin(phase * 0.7) + 1.0) * 0.75;
    const double power_factor = 0.94 + std::sin(phase * 0.2) * 0.02;
    const double active_power = voltage * current * power_factor;
    const double hours = publish_interval_ms_ / 3600000.0;

    energy_kwh_[device.device_id] += active_power * hours / 1000.0;

    model::TelemetryData data;
    data.device_id = device.device_id;
    data.type = device.type;
    data.ts_ms = ts_ms;
    data.integer_values["meter_role"] = meterRoleEnumValue(device.meter_role);
    data.string_values["parent_meter_id"] = device.parent_meter_id;
    data.numeric_values["voltage"] = round2(voltage);
    data.numeric_values["current"] = round2(current);
    data.numeric_values["active_power"] = round2(active_power);
    data.numeric_values["energy"] = round2(energy_kwh_[device.device_id]);
    data.numeric_values["frequency"] = round2(mock_.frequency_base + std::sin(phase * 0.15) * 0.03);
    data.numeric_values["power_factor"] = round2(power_factor);
    data.integer_values["relay_status"] = 1;
    // 这些字段对支表没有业务含义，但同一电表产品共用物模型。
    // 支表上报 0 可以避免平台页面显示为空。
    data.numeric_values["branch_power_sum"] = 0.0;
    data.numeric_values["power_loss"] = 0.0;
    data.numeric_values["loss_rate"] = 0.0;
    data.integer_values["online"] = device.online ? 1 : 0;
    applyStateOverlay(data);
    return data;
}

model::TelemetryData MockDataSource::buildMainMeter(const model::DeviceInfo &device,
                                                     int64_t ts_ms,
                                                     double branch_power_sum,
                                                     double branch_current_sum)
{
    const double phase = tick_ * 0.24;
    const double power_loss = std::max(35.0, branch_power_sum * (0.035 + std::sin(phase) * 0.006));
    const double active_power = branch_power_sum + power_loss;
    const double hours = publish_interval_ms_ / 3600000.0;

    energy_kwh_[device.device_id] += active_power * hours / 1000.0;

    model::TelemetryData data;
    data.device_id = device.device_id;
    data.type = device.type;
    data.ts_ms = ts_ms;
    data.integer_values["meter_role"] = meterRoleEnumValue(device.meter_role);
    data.numeric_values["voltage"] = round2(mock_.voltage_base + std::sin(phase) * 1.2);
    data.numeric_values["current"] = round2(branch_current_sum + power_loss / mock_.voltage_base);
    data.numeric_values["active_power"] = round2(active_power);
    data.numeric_values["energy"] = round2(energy_kwh_[device.device_id]);
    data.numeric_values["frequency"] = round2(mock_.frequency_base + std::sin(phase * 0.12) * 0.02);
    data.numeric_values["power_factor"] = 0.96;
    data.integer_values["relay_status"] = 1;
    data.numeric_values["branch_power_sum"] = round2(branch_power_sum);
    data.numeric_values["power_loss"] = round2(power_loss);
    data.numeric_values["loss_rate"] = branch_power_sum > 0 ? round2(power_loss * 100.0 / branch_power_sum) : 0.0;
    data.integer_values["online"] = device.online ? 1 : 0;
    applyStateOverlay(data);
    return data;
}

model::TelemetryData MockDataSource::buildEnvSensor(const model::DeviceInfo &device, int64_t ts_ms) const
{
    const double phase = tick_ * 0.18;

    model::TelemetryData data;
    data.device_id = device.device_id;
    data.type = device.type;
    data.ts_ms = ts_ms;
    data.numeric_values["temperature"] = round2(mock_.temperature_base + std::sin(phase) * 1.6);
    data.numeric_values["humidity"] = round2(mock_.humidity_base + std::cos(phase * 0.8) * 3.2);
    data.integer_values["online"] = device.online ? 1 : 0;
    applyStateOverlay(data);
    return data;
}

model::TelemetryData MockDataSource::buildRelay(const model::DeviceInfo &device, int64_t ts_ms) const
{
    model::TelemetryData data;
    data.device_id = device.device_id;
    data.type = device.type;
    data.ts_ms = ts_ms;
    data.integer_values["relay_state"] = (tick_ / 12) % 2;
    data.integer_values["control_mode"] = 0;
    data.integer_values["online"] = device.online ? 1 : 0;
    applyStateOverlay(data);
    return data;
}

model::TelemetryData MockDataSource::buildDtuNode(const model::DeviceInfo &device, int64_t ts_ms) const
{
    const int child_count = countCsvItems(device.child_macs);
    const int collect_cycle = publish_interval_ms_;

    model::TelemetryData data;
    data.device_id = device.device_id;
    data.type = device.type;
    data.ts_ms = ts_ms;
    data.integer_values["role"] = isRootDtuParent(device.parent_mac) ? 0 : 1;
    data.integer_values["uptime"] = tick_ * publish_interval_ms_ / 1000;
    data.string_values["mac"] = device.mac;
    data.string_values["name"] = device.name.empty() ? device.device_id : device.name;
    data.integer_values["online"] = device.online ? 1 : 0;
    data.object_values["topology"] = {
        {"parent_mac", device.parent_mac},
        {"child_count", child_count},
        {"child_macs", device.child_macs},
    };
    data.object_values["collect_config"] = {
        {"modbus_count", device.modbus_type > 0 ? 1 : 0},
        {"collect_cycle", collect_cycle},
        {"addr_1", device.modbus_addr},
        {"type_1", device.modbus_type},
        {"addr_2", 0},
        {"type_2", 0},
        {"addr_3", 0},
        {"type_3", 0},
        {"addr_4", 0},
        {"type_4", 0},
        {"addr_5", 0},
        {"type_5", 0},
        {"addr_6", 0},
        {"type_6", 0},
        {"addr_7", 0},
        {"type_7", 0},
        {"addr_8", 0},
        {"type_8", 0},
    };
    applyStateOverlay(data);
    return data;
}

void MockDataSource::applyStateOverlay(model::TelemetryData &data) const
{
    if (state_store_)
        state_store_->overlay(data);
}

} // namespace gateway::datasource
