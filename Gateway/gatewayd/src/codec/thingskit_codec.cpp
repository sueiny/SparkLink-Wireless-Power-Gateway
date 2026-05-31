#include "codec/thingskit_codec.h"

#include "json.hpp"

namespace gateway::codec {
namespace {

nlohmann::json buildGatewayAttributeValues(const model::GatewayStatus &status)
{
    return {
        {"network_type", status.network_type},
        {"network_ifname", status.network_ifname},
        {"cloud_connected", status.cloud_connected ? 1 : 0},
        {"device_count", status.device_count},
        {"cache_count", status.cache_count},
        {"gateway_version", status.version},
    };
}

} // namespace

std::string ThingsKitCodec::buildTelemetryPayload(const model::TelemetryData &data)
{
    nlohmann::json payload;
    payload["deviceId"] = data.device_id;
    payload["ts"] = data.ts_ms;
    payload["values"] = data.toFlatJson();
    return payload.dump();
}

std::string ThingsKitCodec::buildTelemetryValuesPayload(const model::TelemetryData &data)
{
    return data.toFlatJson().dump();
}

std::string ThingsKitCodec::buildGatewaySubDeviceTelemetryPayload(
    const std::vector<model::TelemetryData> &batch)
{
    nlohmann::json payload = nlohmann::json::object();

    for (const auto &item : batch) {
        payload[item.device_id] = nlohmann::json::array({
            {
                {"ts", item.ts_ms},
                {"values", item.toFlatJson()},
            },
        });
    }

    return payload.dump();
}

std::string ThingsKitCodec::buildGatewayAttributesPayload(const model::GatewayStatus &status)
{
    nlohmann::json payload;
    payload["deviceId"] = status.gateway_id;
    payload["ts"] = status.ts_ms;
    payload["values"] = buildGatewayAttributeValues(status);
    return payload.dump();
}

std::string ThingsKitCodec::buildGatewayAttributesValuesPayload(
    const model::GatewayStatus &status)
{
    return buildGatewayAttributeValues(status).dump();
}

std::string ThingsKitCodec::buildEventPayload(const std::string &device_id,
                                              const std::string &event,
                                              const std::string &severity,
                                              const std::string &message)
{
    nlohmann::json payload;
    payload["deviceId"] = device_id;
    payload["event"] = event;
    payload["severity"] = severity;
    payload["message"] = message;
    return payload.dump();
}

} // namespace gateway::codec
