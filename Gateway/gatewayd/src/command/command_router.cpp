#include "command/command_router.h"

#include "command/command_payload_codec.h"
#include "codec/thingskit_topics.h"

#include <algorithm>
#include <cstring>

namespace gateway::command {

CommandParseResult CommandRouter::parse(const RawCommandMessage &raw,
                                        const config::AppConfig &config) const
{
    CommandParseResult parsed;
    const std::string response_topic = responseTopicFor(raw.topic);
    if (response_topic.empty())
        return parsed;

    const codec::CommandPayloadCodec payload_codec;
    const auto decoded = payload_codec.parsePayload(raw.payload);
    if (!decoded.error.empty()) {
        CommandRequest request;
        request.response_topic = response_topic;
        request.source_topic = raw.topic;
        request.request_id = requestIdFromTopic(raw.topic);
        request.received_ts_ms = raw.received_ts_ms;
        parsed.has_immediate_response = true;
        parsed.immediate_response =
            buildResponse(request, makeCommandError("BAD_JSON", decoded.error));
        return parsed;
    }

    const auto &payload = decoded.payload;
    const std::string target = payload_codec.extractTarget(payload);
    CommandRequest request;
    request.response_topic = response_topic;
    request.source_topic = raw.topic;
    request.original_payload = payload;
    request.request_id =
        payload_codec.stringValueOr(payload, "requestId", requestIdFromTopic(raw.topic));
    request.method = payload_codec.stringValueOr(payload, "method", "");
    request.params = payload.is_object() && payload.contains("params")
                         ? payload["params"]
                         : nlohmann::json::object();
    request.received_ts_ms = raw.received_ts_ms;

    if (!request.params.is_object())
        request.params = nlohmann::json::object();

    payload_codec.applyConcisePropertyCommand(payload, request.method, request.params);

    if (target.empty() || target == config.gateway.gateway_id || target == config.gateway.name) {
        request.target_device_id = config.gateway.gateway_id;
        request.target_type = model::DeviceType::Gateway;
        request.product_type = "gateway";
        request.target_found = true;
    } else {
        const auto it = std::find_if(config.devices.begin(), config.devices.end(),
                                     [&](const model::DeviceInfo &device) {
                                         return device.device_id == target ||
                                                device.name == target ||
                                                device.mac == target;
                                     });
        if (it != config.devices.end()) {
            request.target_device_id = it->device_id;
            request.target_type = it->type;
            request.product_type = it->product_id.empty()
                                       ? model::toString(it->type)
                                       : it->product_id;
            request.target_found = true;
        } else {
            request.target_device_id = target;
            request.target_type = model::DeviceType::Gateway;
            request.product_type = "unknown";
            request.target_found = false;
        }
    }

    parsed.has_request = true;
    parsed.request = std::move(request);
    return parsed;
}

CommandResponseMessage CommandRouter::buildResponse(const CommandRequest &request,
                                                    const CommandResult &result) const
{
    nlohmann::json response = {
        {"requestId", request.request_id},
        {"targetDeviceId", request.target_device_id},
        {"method", request.method},
        {"success", result.success},
        {"code", result.code},
        {"message", result.message},
        {"ts", result.finished_ts_ms},
        {"data", result.data},
    };

    if (request.original_payload.contains("device"))
        response["device"] = request.original_payload["device"];
    if (request.original_payload.contains("deviceName"))
        response["deviceName"] = request.original_payload["deviceName"];

    return {request.response_topic, response.dump()};
}

std::string CommandRouter::responseTopicFor(const std::string &topic) const
{
    if (topic.rfind(codec::thingskit::kRpcRequestTopicPrefix, 0) == 0) {
        const size_t prefix_len = std::strlen(codec::thingskit::kRpcRequestTopicPrefix);
        return std::string(codec::thingskit::kRpcResponseTopicPrefix) +
               topic.substr(prefix_len);
    }
    if (topic == codec::thingskit::kGatewayCommandRequestTopic)
        return codec::thingskit::kGatewayCommandResponseTopic;
    return {};
}

std::string CommandRouter::requestIdFromTopic(const std::string &topic) const
{
    if (topic.rfind(codec::thingskit::kRpcRequestTopicPrefix, 0) == 0)
        return topic.substr(std::strlen(codec::thingskit::kRpcRequestTopicPrefix));
    return {};
}

} // namespace gateway::command
