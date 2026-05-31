#pragma once

#include "json.hpp"

#include <string>

namespace gateway::codec {

struct ParsedCommandPayload {
    nlohmann::json payload = nlohmann::json::object();
    std::string error;
};

class CommandPayloadCodec {
public:
    // 支持两种 payload：普通 JSON 对象，或云平台转义后的 JSON 字符串。
    ParsedCommandPayload parsePayload(const std::string &raw_payload) const;

    std::string stringValueOr(const nlohmann::json &object,
                              const char *key,
                              const std::string &fallback) const;

    // 支持简洁命令格式：
    // {"relay_state":1,"deviceName":"RELAY_001"} -> method=set_relay, params.state=1。
    bool applyConcisePropertyCommand(const nlohmann::json &payload,
                                     std::string &method,
                                     nlohmann::json &params) const;

    // 从 device/deviceId/deviceName/target 等字段提取目标设备。
    std::string extractTarget(const nlohmann::json &request) const;
};

} // namespace gateway::codec
