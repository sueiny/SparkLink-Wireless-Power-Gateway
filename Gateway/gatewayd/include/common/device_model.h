#pragma once

#include "json.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace gateway::model {

enum class DeviceType {
    Gateway,
    SinglePhaseMeter,
    EnvSensor,
    Relay,
    DtuNode,
};

// DTU 节点物模型（V2：ID-based，无 MAC）
struct DtuNodeModel {
    uint16_t id = 0;                          // 节点 ID (1-255)
    uint8_t role = 0;                         // SLE 角色: 0=Root, 1=Relay, 2=Leaf, 3=Gateway
    std::string name;                         // "DTU_036"
    bool online = false;                      // 路由表中存在即在线
    uint16_t parent_id = 0;                   // 父节点 ID，0=无父节点
    std::vector<uint16_t> child_ids;          // 子节点 ID 列表
};

// 外接设备配置（V2：用 station_id + dtu_id 替代 MAC）
struct DeviceInfo {
    std::string device_id;                    // ThingsKit 设备 ID，如 "METER_001"
    std::string product_id;
    std::string name;
    DeviceType type = DeviceType::Gateway;
    int station_id = 0;                       // Modbus 站号 (1-255)
    int dtu_id = 0;                           // 挂载的 DTU 节点 ID
    int modbus_addr = 0;
    int modbus_type = 0;                      // 0x02=电表, 0x03=温湿度, 0x04=继电器
    bool online = true;
    // V1 兼容字段（过渡期保留，后续删除）
    int dtu_node_id = 0;
};

// DTU 节点配置（V2：ID-based）
struct DtuDeviceInfo {
    std::string device_id;                    // "DTU_036"
    int node_id = 0;                          // SLE 节点 ID
    int parent_id = 0;                        // 父节点 ID
    std::string child_ids;                    // 子节点 ID 列表，逗号分隔
};

struct TelemetryData {
    std::string device_id;
    DeviceType type = DeviceType::Gateway;
    int64_t ts_ms = 0;
    std::map<std::string, int64_t> integer_values;
    std::map<std::string, double> numeric_values;
    std::map<std::string, std::string> string_values;
    std::map<std::string, bool> bool_values;
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
std::string toString(DeviceType type);

// 根据设备类型和站号生成设备名称：METER_001, RELAY_101, ENV_005
std::string generateDeviceName(DeviceType type, int station_id);

// 根据节点 ID 生成 DTU 名称：DTU_036
std::string generateDtuName(int node_id);

// SLE 角色 → 云端角色映射：Root(1)→0, Relay(2)/Leaf(3)→1
uint8_t sleRoleToCloudRole(uint8_t sle_role);

} // namespace gateway::model
