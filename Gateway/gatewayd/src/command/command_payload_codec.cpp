#include "command/command_payload_codec.h"

namespace gateway::codec {

ParsedCommandPayload CommandPayloadCodec::parsePayload(const std::string &raw_payload) const
{
    ParsedCommandPayload result;
    try {
        result.payload = nlohmann::json::parse(raw_payload);
    } catch (const std::exception &e) {
        result.error = std::string("invalid command json: ") + e.what();
        return result;
    }

    if (!result.payload.is_string())
        return result;

    try {
        result.payload = nlohmann::json::parse(result.payload.get<std::string>());
    } catch (const std::exception &e) {
        result.error = std::string("invalid command json string: ") + e.what();
    }
    return result;
}

std::string CommandPayloadCodec::stringValueOr(const nlohmann::json &object,
                                               const char *key,
                                               const std::string &fallback) const
{
    if (object.is_object() && object.contains(key) && object[key].is_string())
        return object[key].get<std::string>();
    return fallback;
}

bool CommandPayloadCodec::applyConcisePropertyCommand(const nlohmann::json &payload,
                                                      std::string &method,
                                                      nlohmann::json &params) const
{
    if (!payload.is_object() || !method.empty())
        return false;

    if (payload.contains("relay_state")) {
        method = "set_relay";
        params = {{"state", payload["relay_state"]}};
        return true;
    }

    if (payload.contains("relay_status")) {
        method = "set_relay";
        params = {{"state", payload["relay_status"]}};
        return true;
    }

    if (payload.contains("control_mode")) {
        method = "set_mode";
        params = {{"mode", payload["control_mode"]}};
        return true;
    }

    if (payload.contains("collect_cycle")) {
        method = "set_collect_cycle";
        params = {{"cycle_ms", payload["collect_cycle"]}};
        return true;
    }

    return false;
}

std::string CommandPayloadCodec::extractTarget(const nlohmann::json &request) const
{
    if (!request.is_object())
        return {};

    for (const char *key : {"device", "deviceId", "deviceName", "target", "targetDevice"}) {
        if (request.contains(key) && request[key].is_string())
            return request[key].get<std::string>();
    }

    const auto params = request.contains("params") ? request["params"] : nlohmann::json::object();
    if (params.is_object()) {
        for (const char *key : {"device", "deviceId", "deviceName", "target", "targetDevice"}) {
            if (params.contains(key) && params[key].is_string())
                return params[key].get<std::string>();
        }
    }

    return {};
}

} // namespace gateway::codec
