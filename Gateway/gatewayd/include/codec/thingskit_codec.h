#pragma once

#include "common/device_model.h"

#include <string>
#include <vector>

namespace gateway::codec {

// ThingsKitCodec 是内部模型到 ThingsKit JSON payload 的唯一出口。
// 它只做格式转换，不访问 MQTT、不读取配置、不决定 topic。
class ThingsKitCodec {
public:
    // 单设备完整 telemetry payload。
    static std::string buildTelemetryPayload(const model::TelemetryData &data);

    // 只输出 values 对象，用于网关属性和部分轻量上报。
    static std::string buildTelemetryValuesPayload(const model::TelemetryData &data);

    // 网关代子设备上报的批量 payload。
    static std::string buildGatewaySubDeviceTelemetryPayload(
        const std::vector<model::TelemetryData> &batch);

    static std::string buildGatewayAttributesPayload(const model::GatewayStatus &status);
    static std::string buildGatewayAttributesValuesPayload(const model::GatewayStatus &status);

    // 事件上报预留接口；当前主流程还没有真实发布事件。
    static std::string buildEventPayload(const std::string &device_id,
                                         const std::string &event,
                                         const std::string &severity,
                                         const std::string &message);
};

} // namespace gateway::codec
