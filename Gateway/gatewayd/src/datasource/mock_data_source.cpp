#include "datasource/mock_data_source.h"

#include "common/time_utils.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace gateway::datasource {
namespace {

constexpr double kBaseVoltage = 220.0;
constexpr double kBaseCurrent = 2.4;
constexpr double kCurrentStep = 1.05;
constexpr double kBranchPhaseOffset = 0.85;
constexpr double kBranchTickPhase = 0.33;

double round2(double value)
{
    return std::round(value * 100.0) / 100.0;
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
    return true;
}

std::vector<model::TelemetryData> MockDataSource::collect()
{
    const int64_t ts = common::nowMs();
    std::vector<model::TelemetryData> result;
    int branch_index = 0;

    ++tick_;

    for (const auto &device : devices_) {
        if (device.type == model::DeviceType::SinglePhaseMeter) {
            result.push_back(buildMeter(device, branch_index++, ts));
        } else if (device.type == model::DeviceType::EnvSensor) {
            result.push_back(buildEnvSensor(device, ts));
        } else if (device.type == model::DeviceType::Relay) {
            result.push_back(buildRelay(device, ts));
        }
    }

    return result;
}

model::TelemetryData MockDataSource::buildMeter(const model::DeviceInfo &device,
                                                 int branch_index, int64_t ts_ms)
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
    data.integer_values["dtu_id"] = device.dtu_id;
    data.numeric_values["voltage"] = round2(voltage);
    data.numeric_values["current"] = round2(current);
    data.numeric_values["active_power"] = round2(active_power);
    data.numeric_values["energy"] = round2(energy_kwh_[device.device_id]);
    data.numeric_values["frequency"] = round2(mock_.frequency_base + std::sin(phase * 0.15) * 0.03);
    data.numeric_values["power_factor"] = round2(power_factor);
    data.integer_values["relay_status"] = 1;
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
    data.integer_values["dtu_id"] = device.dtu_id;
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
    data.integer_values["dtu_id"] = device.dtu_id;
    data.integer_values["relay_state"] = (tick_ / 12) % 2;
    data.integer_values["online"] = device.online ? 1 : 0;
    applyStateOverlay(data);
    return data;
}

void MockDataSource::applyStateOverlay(model::TelemetryData &data) const
{
    if (state_store_)
        state_store_->overlay(data);
}

} // namespace gateway::datasource
