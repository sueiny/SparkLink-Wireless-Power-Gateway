#pragma once

#include "common/time_utils.h"
#include "json.hpp"
#include "common/device_model.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace gateway::command {

// MQTT 回调线程只构造 RawCommandMessage 并入队，避免在 libmosquitto 回调里做重活。
struct RawCommandMessage {
    std::string topic;
    std::string payload;
    int64_t received_ts_ms = 0;
};

struct ServiceParamSpec {
    // 物模型 inputData.identifier。
    std::string identifier;
    // 物模型 dataType.type，例如 INT/ENUM/TEXT。
    std::string type;
    // ENUM 参数允许值；非 ENUM 参数保持为空。
    std::vector<int64_t> enum_values;
};

struct ServiceSpec {
    // 物模型 service.identifier，对应云端下发 method。
    std::string identifier;
    std::string call_type;
    std::vector<ServiceParamSpec> input_params;
};

// Router 解析后的内部命令对象。
// 后续 Validator/Executor/StatePatchCodec 都只依赖它，不再关心原始 MQTT topic。
struct CommandRequest {
    std::string request_id;
    std::string response_topic;
    std::string source_topic;
    std::string target_device_id;
    model::DeviceType target_type = model::DeviceType::Gateway;
    std::string product_type;
    std::string method;
    nlohmann::json params = nlohmann::json::object();
    nlohmann::json original_payload = nlohmann::json::object();
    int64_t received_ts_ms = 0;
    bool target_found = false;
};

struct CommandResult {
    bool success = false;
    std::string code;
    std::string message;
    nlohmann::json data = nlohmann::json::object();
    int64_t finished_ts_ms = 0;
};

// 已经构造好的命令响应 MQTT 消息。
// PublishManager 只需要按 topic/payload 发布即可。
struct CommandResponseMessage {
    std::string topic;
    std::string payload;
};

// CommandParseResult 同时承载正常请求和 BAD_JSON/未知 topic 等立即响应。
struct CommandParseResult {
    bool has_request = false;
    bool has_immediate_response = false;
    CommandRequest request;
    CommandResponseMessage immediate_response;
};

inline CommandResult makeCommandResult(bool success,
                                       std::string code,
                                       std::string message,
                                       nlohmann::json data = nlohmann::json::object())
{
    CommandResult result;
    result.success = success;
    result.code = std::move(code);
    result.message = std::move(message);
    result.data = std::move(data);
    result.finished_ts_ms = common::nowMs();
    return result;
}

inline CommandResult makeCommandError(std::string code, std::string message)
{
    return makeCommandResult(false, std::move(code), std::move(message));
}

} // namespace gateway::command
