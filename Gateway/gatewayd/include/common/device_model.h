#pragma once

#include "json.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace gateway::model {

// gatewayd 内部统一设备类型。
// 物模型 product_id 仍来自配置/JSON，这里只表达程序需要分支处理的类别。
enum class DeviceType {
    Gateway,
    SinglePhaseMeter,
    EnvSensor,
    Relay,
    DtuNode,
};

// 电表角色用于模拟主表/支表拓扑和线损统计。
enum class MeterRole {
    None,
    Main,
    Branch,
};

// 配置文件中的设备清单。
// 这些字段同时服务于模拟数据、命令目标匹配和 DTU 拓扑上报。
struct DeviceInfo {
    std::string device_id;
    std::string product_id;
    std::string name;
    DeviceType type = DeviceType::Gateway;
    MeterRole meter_role = MeterRole::None;
    std::string parent_meter_id;
    std::string mac;
    std::string parent_mac;
    std::string child_macs;
    int modbus_addr = 0;
    int modbus_type = 0;
    bool online = true;
};

// 采集线程和状态补丁线程之间传递的内部遥测模型。
// 它刻意不绑定 ThingsKit payload 形态，最终 JSON 由 ThingsKitCodec 统一生成。
struct TelemetryData {
    std::string device_id;
    DeviceType type = DeviceType::Gateway;
    int64_t ts_ms = 0;
    std::map<std::string, int64_t> integer_values;
    std::map<std::string, double> numeric_values;
    std::map<std::string, std::string> string_values;
    std::map<std::string, bool> bool_values;
    // ThingsKit 的 STRUCT 物模型字段需要在 values 中携带嵌套 JSON 对象。
    // 这里仅保存已经构造好的结构化值，不在模型层拼 topic 或 MQTT payload。
    std::map<std::string, nlohmann::json> object_values;

    nlohmann::json toFlatJson() const;
    void mergeFromFlatJson(const nlohmann::json &values);
};

struct GatewayStatus {
    std::string gateway_id;
    std::string version;
    std::string network_type;
    std::string network_ifname;
    bool cloud_connected = false;
    int device_count = 0;
    int cache_count = 0;
    int64_t ts_ms = 0;
};

DeviceType deviceTypeFromString(const std::string &text);
MeterRole meterRoleFromString(const std::string &text);
std::string toString(DeviceType type);
std::string toString(MeterRole role);

} // namespace gateway::model
