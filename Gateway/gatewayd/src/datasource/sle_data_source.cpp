#include "datasource/sle_data_source.h"

#include "codec/modbus_parser.h"
#include "codec/sle_frame_parser.h"
#include "common/time_utils.h"

#include <sstream>

namespace gateway::datasource {

SleDataSource::SleDataSource(std::vector<model::DeviceInfo> devices,
                             std::vector<model::DtuDeviceInfo> dtu_devices,
                             std::shared_ptr<state::DeviceStateStore> state_store,
                             RouteTable &route_table,
                             log::Logger &logger)
    : devices_(std::move(devices)),
      dtu_devices_(std::move(dtu_devices)),
      state_store_(std::move(state_store)),
      route_table_(route_table),
      logger_(logger)
{
}

bool SleDataSource::init(const std::string &socket_path)
{
    // 构建 dtu_id → dtu_devices_ 索引 (O(1) 查找)
    for (size_t i = 0; i < dtu_devices_.size(); ++i) {
        if (dtu_devices_[i].node_id > 0)
            dtu_id_to_index_[dtu_devices_[i].node_id] = i;
    }

    // 构建 dtu_id → devices_ 索引 (O(1) 查找)
    for (size_t i = 0; i < devices_.size(); ++i) {
        if (devices_[i].dtu_id > 0)
            device_dtu_id_to_index_[devices_[i].dtu_id] = i;
    }

    if (!receiver_.init(socket_path)) {
        logger_.error("SLE-DS", "IPC receiver init failed");
        return false;
    }
    logger_.info("SLE-DS", std::to_string(dtu_devices_.size()) + " DTU, " +
                 std::to_string(devices_.size()) + " devices, socket=" + socket_path);
    return true;
}

void SleDataSource::deinit()
{
    receiver_.deinit();
}

bool SleDataSource::waitForClient(int timeout_ms)
{
    return receiver_.acceptClient(timeout_ms);
}

std::vector<model::TelemetryData> SleDataSource::collect()
{
    std::vector<model::TelemetryData> result;

    if (!receiver_.isConnected()) {
        if (!receiver_.acceptClient(1000))
            return result;
    }

    std::vector<uint8_t> raw;
    if (!receiver_.receiveRawFrame(raw)) {
        receiver_.closeClient();
        return result;
    }

    ++tick_;

    codec::SleFrameHeader header;
    if (!codec::parseSleFrameHeader(raw.data(), static_cast<uint16_t>(raw.size()), &header))
        return result;

    switch (header.frame_type) {
    case codec::SLE_FRAME_TYPE_DATA:
        return handleDataFrame(raw, header);
    case codec::SLE_FRAME_TYPE_HEARTBEAT:
        return handleHeartbeatFrame(raw, header);
    case codec::SLE_FRAME_TYPE_TOPO_SUMMARY:
        handleTopoFrame(raw, header);
        return result;
    default:
        return result;
    }
}

bool SleDataSource::isConnected() const
{
    return receiver_.isConnected();
}

std::vector<model::TelemetryData> SleDataSource::handleDataFrame(
    const std::vector<uint8_t> &raw, const codec::SleFrameHeader &header)
{
    std::vector<model::TelemetryData> result;

    const uint8_t *payload = raw.data() + codec::SLE_FRAME_HEADER_LEN;

    codec::SleDataPayload data_payload;
    if (!codec::parseSleDataPayload(payload, header.payload_len, &data_payload))
        return result;

    // 从帧头 src_node_id 找到 DTU 节点
    int dtu_id = static_cast<int>(header.src_node_id);
    auto dtu_it = dtu_id_to_index_.find(dtu_id);

    // 找该 DTU 下挂的外接设备
    const model::DeviceInfo *device = findDeviceByDtuId(dtu_id);

    model::TelemetryData data;
    data.ts_ms = common::nowMs();

    if (device != nullptr) {
        data.device_id = device->device_id;
        data.type = device->type;
    } else if (dtu_it != dtu_id_to_index_.end()) {
        // 没有外接设备，但有 DTU 配置 → 作为 DTU 节点上报
        data.device_id = dtu_devices_[dtu_it->second].device_id;
        data.type = model::DeviceType::DtuNode;
    } else {
        std::ostringstream oss;
        oss << "DTU_" << dtu_id;
        data.device_id = oss.str();
        data.type = model::DeviceType::DtuNode;
    }

    data.integer_values["online"] = 1;
    data.integer_values["dtu_id"] = dtu_id;

    if (codec::parseModbusResponse(data_payload.modbus_rtu, data_payload.modbus_len,
                                   data_payload.modbus_type, data)) {
        // Modbus 解析成功
    } else {
        data.integer_values["modbus_parse_error"] = 1;
    }

    if (state_store_)
        state_store_->overlay(data);

    result.push_back(std::move(data));
    return result;
}

std::vector<model::TelemetryData> SleDataSource::handleHeartbeatFrame(
    const std::vector<uint8_t> &raw, const codec::SleFrameHeader &header)
{
    std::vector<model::TelemetryData> result;

    // 心跳帧 payload: role(1)
    const uint8_t *payload = raw.data() + codec::SLE_FRAME_HEADER_LEN;
    if (header.payload_len < 1)
        return result;

    uint8_t role = payload[0];
    int node_id = static_cast<int>(header.src_node_id);

    // 使用 O(1) 哈希表查找 DTU 配置
    auto dtu_it = dtu_id_to_index_.find(node_id);
    if (dtu_it == dtu_id_to_index_.end()) {
        // 未知 DTU 节点，跳过
        return result;
    }
    const model::DtuDeviceInfo *dtu = &dtu_devices_[dtu_it->second];

    // 计算子节点数量和列表
    int child_count = 0;
    std::string child_ids_str;
    for (const auto &d : dtu_devices_) {
        if (d.parent_id == node_id) {
            if (child_count > 0) child_ids_str += ",";
            child_ids_str += std::to_string(d.node_id);
            child_count++;
        }
    }

    model::TelemetryData data;
    data.device_id = dtu->device_id;
    data.type = model::DeviceType::DtuNode;
    data.ts_ms = common::nowMs();

    // 使用统一的角色映射函数
    data.integer_values["role"] = model::sleRoleToCloudRole(role);
    data.string_values["name"] = dtu->device_id;
    data.bool_values["online"] = true;

    // 拓扑信息
    data.object_values["topology"] = {
        {"parent_id", dtu->parent_id},
        {"child_count", child_count},
        {"child_ids", child_ids_str},
    };

    result.push_back(std::move(data));
    return result;
}

void SleDataSource::handleTopoFrame(const std::vector<uint8_t> &raw,
                                    const codec::SleFrameHeader &header)
{
    const uint8_t *payload = raw.data() + codec::SLE_FRAME_HEADER_LEN;
    route_table_.updateFromTopo(header.src_node_id, payload, header.payload_len, common::nowMs());
}

const model::DeviceInfo *SleDataSource::findDeviceByDtuId(int dtu_id) const
{
    auto it = device_dtu_id_to_index_.find(dtu_id);
    if (it != device_dtu_id_to_index_.end()) {
        return &devices_[it->second];
    }
    return nullptr;
}

std::vector<model::TelemetryData> SleDataSource::generateDtuHeartbeats(int64_t ts_ms) const
{
    std::vector<model::TelemetryData> result;
    auto routes = route_table_.snapshot();

    // 为每个配置的 DTU 生成心跳
    for (const auto &dtu : dtu_devices_) {
        bool online = route_table_.isOnline(static_cast<uint16_t>(dtu.node_id));

        // 获取拓扑信息
        uint16_t parent_id = route_table_.getParentId(static_cast<uint16_t>(dtu.node_id));
        auto child_ids = route_table_.getChildIds(static_cast<uint16_t>(dtu.node_id));

        // 如果路由表中没有，用配置中的值
        if (parent_id == 0 && dtu.parent_id > 0)
            parent_id = static_cast<uint16_t>(dtu.parent_id);

        // 从路由表获取 SLE 角色
        // 如果配置中 parent_id=0，说明是 root 节点，强制使用 root 角色
        uint8_t sle_role = 0;
        bool is_root = (dtu.parent_id == 0);
        if (!is_root) {
            for (const auto &r : routes) {
                if (r.node_id == static_cast<uint16_t>(dtu.node_id)) {
                    sle_role = r.role;
                    break;
                }
            }
        } else {
            sle_role = 1;  // SLE_ROLE_ROOT = 1
        }

        // 构建 child_ids 字符串
        std::string child_ids_str;
        if (!child_ids.empty()) {
            for (size_t i = 0; i < child_ids.size(); ++i) {
                if (i > 0) child_ids_str += ",";
                child_ids_str += std::to_string(child_ids[i]);
            }
        } else if (!dtu.child_ids.empty()) {
            child_ids_str = dtu.child_ids;
        }

        model::TelemetryData data;
        data.device_id = dtu.device_id;
        data.type = model::DeviceType::DtuNode;
        data.ts_ms = ts_ms;
        data.integer_values["role"] = model::sleRoleToCloudRole(sle_role);
        data.string_values["name"] = dtu.device_id;
        data.bool_values["online"] = online;
        data.object_values["topology"] = {
            {"parent_id", static_cast<int>(parent_id)},
            {"child_count", static_cast<int>(child_ids.empty() ? 0 : child_ids.size())},
            {"child_ids", child_ids_str},
        };
        result.push_back(std::move(data));
    }

    if (!result.empty()) {
        logger_.info("SLE-DS", "generated " + std::to_string(result.size()) + " DTU heartbeats");
    }
    return result;
}

} // namespace gateway::datasource
