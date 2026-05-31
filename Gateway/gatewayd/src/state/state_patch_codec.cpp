#include "state/state_patch_codec.h"

#include "common/time_utils.h"

#include <algorithm>
#include <utility>

namespace gateway::codec {
namespace {

const model::DeviceInfo *findDevice(const config::AppConfig &config,
                                    const std::string &device_id)
{
    const auto it = std::find_if(config.devices.begin(), config.devices.end(),
                                 [&](const model::DeviceInfo &device) {
                                     return device.device_id == device_id;
                                 });
    return it == config.devices.end() ? nullptr : &*it;
}

nlohmann::json buildDtuCollectConfigPatch(const model::DeviceInfo *device, int collect_cycle)
{
    nlohmann::json collect_config = {
        {"modbus_count", device && device->modbus_type > 0 ? 1 : 0},
        {"collect_cycle", collect_cycle},
        {"addr_1", device ? device->modbus_addr : 0},
        {"type_1", device ? device->modbus_type : 0},
    };

    for (int index = 2; index <= 8; ++index) {
        collect_config["addr_" + std::to_string(index)] = 0;
        collect_config["type_" + std::to_string(index)] = 0;
    }

    return collect_config;
}

} // namespace

bool StatePatchCodec::buildPatch(const command::CommandRequest &request,
                                 const command::CommandResult &result,
                                 const config::AppConfig &config,
                                 model::TelemetryData *out) const
{
    if (!out || !result.success)
        return false;

    model::TelemetryData telemetry;
    telemetry.device_id = request.target_device_id;
    telemetry.type = request.target_type;
    telemetry.ts_ms = common::nowMs();

    if (request.method == "set_relay") {
        const int state = request.params.value("state", 0);
        if (request.target_type == model::DeviceType::Relay) {
            telemetry.integer_values["relay_state"] = state;
        } else if (request.target_type == model::DeviceType::SinglePhaseMeter) {
            telemetry.integer_values["relay_status"] = state;
        } else {
            return false;
        }
    } else if (request.method == "set_mode" &&
               request.target_type == model::DeviceType::Relay) {
        telemetry.integer_values["control_mode"] = request.params.value("mode", 0);
    } else if (request.method == "set_collect_cycle" &&
               request.target_type == model::DeviceType::DtuNode) {
        const auto *device = findDevice(config, request.target_device_id);
        telemetry.object_values["collect_config"] =
            buildDtuCollectConfigPatch(device, request.params.value("cycle_ms", 0));
    } else {
        return false;
    }

    *out = std::move(telemetry);
    return true;
}

} // namespace gateway::codec
