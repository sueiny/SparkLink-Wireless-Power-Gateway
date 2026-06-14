#pragma once

#include <cstdint>
#include <string>

namespace gateway::app {

// 发布队列中的消息类型。
// 不同类型的失败处理不同：遥测可落盘补传，命令响应只做内存重试。
enum class PublishMessageKind {
    Telemetry,
    CommandResponse,
    GatewayStatus,
};

// PublishManager 消费的统一发布消息。
// topic/payload 已经是最终 MQTT 形态，PublishManager 不再理解业务对象。
struct PublishMessage {
    std::string topic;
    std::string payload;
    PublishMessageKind kind = PublishMessageKind::Telemetry;
    int retry_count = 0;
    int64_t next_retry_ts_ms = 0;
    std::string request_id;
    std::string method;
    std::string target;
};

} // namespace gateway::app
