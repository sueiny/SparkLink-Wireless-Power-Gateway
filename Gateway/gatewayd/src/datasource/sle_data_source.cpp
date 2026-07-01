#include "datasource/sle_data_source.h"

#include "codec/modbus_parser.h"
#include "codec/sle_frame_parser.h"
#include "common/time_utils.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <set>
#include <utility>

namespace gateway::datasource {
namespace {

std::string trim(const std::string &text)
{
    size_t begin = 0;
    while (begin < text.size() &&
           std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    size_t end = text.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return text.substr(begin, end - begin);
}

bool parsePositiveInt(const std::string &text, int *out)
{
    if (text.empty() || out == nullptr)
        return false;
    int value = 0;
    for (char ch : text) {
        if (!std::isdigit(static_cast<unsigned char>(ch)))
            return false;
        value = value * 10 + (ch - '0');
        if (value > 65535)
            return false;
    }
    if (value <= 0)
        return false;
    *out = value;
    return true;
}

int suffixNumber(const std::string &device_id)
{
    const size_t pos = device_id.rfind('_');
    if (pos == std::string::npos || pos + 1 >= device_id.size())
        return 0;
    int value = 0;
    return parsePositiveInt(device_id.substr(pos + 1), &value) ? value : 0;
}

bool parseExternalMapPayload(const uint8_t *payload,
                             uint16_t payload_len,
                             std::vector<model::DeviceInfo> *devices,
                             std::string *error)
{
    if (devices == nullptr)
        return false;
    devices->clear();
    if (payload == nullptr && payload_len > 0) {
        if (error)
            *error = "empty external map payload";
        return false;
    }

    const std::string text = payload_len == 0
                                 ? std::string()
                                 : std::string(reinterpret_cast<const char *>(payload),
                                               payload_len);
    std::istringstream iss(text);
    std::string line;
    std::set<std::string> device_ids;

    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        line = trim(line);
        if (line.empty())
            continue;

        const size_t dash = line.find('-');
        if (dash == std::string::npos || dash == 0 || dash + 1 >= line.size()) {
            if (error)
                *error = "invalid external map line: " + line;
            return false;
        }

        const std::string dtu_text = trim(line.substr(0, dash));
        const std::string device_id = line.substr(dash + 1);

        int dtu_id = 0;
        if (!parsePositiveInt(dtu_text, &dtu_id)) {
            if (error)
                *error = "invalid node id in external map line: " + line;
            return false;
        }
        if (!device_ids.insert(device_id).second) {
            if (error)
                *error = "duplicate external device id: " + device_id;
            return false;
        }

        model::DeviceInfo device;
        device.device_id = device_id;
        device.name = device_id;
        device.station_id = suffixNumber(device_id);
        device.dtu_id = dtu_id;
        device.modbus_addr = 1;
        device.online = true;
        devices->push_back(std::move(device));
    }

    return true;
}

std::string joinChildIds(const std::vector<uint16_t> &child_ids)
{
    std::string result;
    for (size_t i = 0; i < child_ids.size(); ++i) {
        if (i > 0)
            result += ",";
        result += std::to_string(child_ids[i]);
    }
    return result;
}

} // namespace

SleDataSource::SleDataSource(std::vector<model::DeviceInfo> devices,
                             std::vector<model::DtuDeviceInfo> dtu_devices,
                             config::TopologyConfig topology_config,
                             std::shared_ptr<state::DeviceStateStore> state_store,
                             RouteTable &route_table,
                             log::Logger &logger)
    : devices_(std::move(devices)),
      dtu_devices_(std::move(dtu_devices)),
      topology_config_(std::move(topology_config)),
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
        if (!devices_[i].device_id.empty())
            device_id_to_index_[devices_[i].device_id] = i;
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
        logger_.info("SLE-IPC", "sle_data_app IPC client connected");
    }

    std::vector<uint8_t> raw;
    const auto receive_status = receiver_.receiveRawFrame(raw);
    if (receive_status == IpcReceiveStatus::Timeout) {
        return result;
    }
    if (receive_status != IpcReceiveStatus::Frame) {
        if (receive_status == IpcReceiveStatus::Disconnected)
            logger_.warn("SLE-IPC", "sle_data_app IPC client disconnected");
        else
            logger_.warn("SLE-IPC", "sle_data_app IPC receive error, closing client");
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
    case codec::SLE_FRAME_TYPE_DTU_TOPOLOGY:
        return handleDtuTopologyFrame(raw, header);
    case codec::SLE_FRAME_TYPE_EXTERNAL_MAP:
        return handleExternalMapFrame(raw, header);
    default:
        logger_.warn("SLE-DS", "unsupported ST frame_type=" +
                               std::to_string(header.frame_type) +
                               ", only DATA/05/06 are accepted on gateway-root link");
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

    // DATA payload 是纯 Modbus RTU；设备归属只能由 src_node_id + 0x06 当前映射确定。
    const int dtu_id = static_cast<int>(header.src_node_id);
    if (useRootReportTopology()) {
        if (dtu_id_to_index_.find(dtu_id) == dtu_id_to_index_.end()) {
            logger_.warn("SLE-DATA",
                         "drop DATA from unknown DTU node_id=" + std::to_string(dtu_id) +
                             ", strict_inventory=1");
            return result;
        }
        if (!route_table_.isOnline(static_cast<uint16_t>(dtu_id))) {
            logger_.warn("SLE-DATA",
                         "drop DATA from offline DTU node_id=" + std::to_string(dtu_id) +
                             ", reason=not_present_in_current_05");
            return result;
        }
    }

    model::DeviceInfo device_snapshot;
    std::string route_reason;
    const bool has_device = findDeviceForDataFrame(dtu_id, &device_snapshot, &route_reason);
    if (!has_device) {
        logger_.warn("SLE-DATA",
                     "drop DATA from dtu_id=" + std::to_string(dtu_id) +
                         ", reason=" + (route_reason.empty() ? "no_device_mapping" : route_reason));
        return result;
    }
    if (device_snapshot.modbus_type <= 0) {
        logger_.warn("SLE-DATA",
                     "drop DATA for device_id=" + device_snapshot.device_id +
                         ", reason=missing_modbus_type");
        return result;
    }

    model::TelemetryData data;
    data.ts_ms = common::nowMs();
    data.device_id = device_snapshot.device_id;
    data.type = device_snapshot.type;

    data.integer_values["online"] = device_snapshot.online ? 1 : 0;
    data.integer_values["dtu_id"] = dtu_id;
    data.integer_values["modbus_type"] = device_snapshot.modbus_type;
    data.integer_values["modbus_addr"] = device_snapshot.modbus_addr;

    if (codec::parseModbusResponse(data_payload.modbus_rtu, data_payload.modbus_len,
                                   static_cast<uint8_t>(device_snapshot.modbus_type), data)) {
        // Modbus 解析成功
    } else {
        data.integer_values["modbus_parse_error"] = 1;
    }

    if (state_store_)
        state_store_->overlay(data);

    result.push_back(std::move(data));
    return result;
}

std::vector<model::TelemetryData> SleDataSource::handleDtuTopologyFrame(
    const std::vector<uint8_t> &raw,
    const codec::SleFrameHeader &header)
{
    std::vector<model::TelemetryData> result;
    if (!useRootReportTopology()) {
        logger_.warn("SLE-DS", "ignored ST 0x05 because topology.source is not root_report");
        return result;
    }

    const uint8_t *payload = raw.data() + codec::SLE_FRAME_HEADER_LEN;
    const auto update = route_table_.updateFromTopologyText(
        header.src_node_id,
        payload,
        header.payload_len,
        static_cast<size_t>(topology_config_.expected_dtu_count),
        dtu_devices_,
        common::nowMs());

    if (!update.ok) {
        logger_.error("SLE-TOPO", "ST 0x05 parse failed: " + update.error);
        return result;
    }

    logger_.info("SLE-TOPO",
                 "ST 0x05 accepted root=" + std::to_string(header.src_node_id) +
                     ", observed_dtu=" + std::to_string(update.observed_count) +
                     ", expected_dtu=" + std::to_string(update.expected_count) +
                     ", complete=" + std::to_string(update.complete ? 1 : 0) +
                     ", snapshot_only=1");

    const int64_t now_ms = common::nowMs();
    {
        std::lock_guard<std::mutex> lock(external_mutex_);
        rebuildDynamicExternalState();
    }
    result = buildDtuTopologyTelemetry(now_ms);
    auto external = buildExternalMapTelemetry(now_ms);
    result.insert(result.end(),
                  std::make_move_iterator(external.begin()),
                  std::make_move_iterator(external.end()));
    return result;
}

std::vector<model::TelemetryData> SleDataSource::handleExternalMapFrame(
    const std::vector<uint8_t> &raw,
    const codec::SleFrameHeader &header)
{
    std::vector<model::TelemetryData> result;
    if (!useRootReportTopology()) {
        logger_.warn("SLE-DS", "ignored ST 0x06 because topology.source is not root_report");
        return result;
    }

    const uint8_t *payload = raw.data() + codec::SLE_FRAME_HEADER_LEN;
    std::vector<model::DeviceInfo> parsed;
    std::string error;
    if (!parseExternalMapPayload(payload, header.payload_len, &parsed, &error)) {
        logger_.error("SLE-TOPO", "ST 0x06 parse failed: " + error);
        return result;
    }
    if (route_table_.findRootByNode(header.src_node_id) !=
        static_cast<int>(header.src_node_id)) {
        logger_.error("SLE-TOPO",
                      "ST 0x06 rejected root=" + std::to_string(header.src_node_id) +
                          ", reason=no_valid_05_snapshot");
        return result;
    }

    std::vector<model::DeviceInfo> normalized;
    normalized.reserve(parsed.size());
    for (const auto &device : parsed) {
        const auto device_it = device_id_to_index_.find(device.device_id);
        if (device_it == device_id_to_index_.end()) {
            logger_.warn("SLE-TOPO",
                         "ST 0x06 skip unknown external device_id=" +
                             device.device_id +
                             ", dtu_id=" + std::to_string(device.dtu_id));
            continue;
        }
        if (dtu_id_to_index_.find(device.dtu_id) == dtu_id_to_index_.end()) {
            logger_.warn("SLE-TOPO",
                         "ST 0x06 skip unknown DTU node dtu_id=" +
                             std::to_string(device.dtu_id) +
                             ", device_id=" + device.device_id);
            continue;
        }
        if (route_table_.findRootByNode(static_cast<uint16_t>(device.dtu_id)) !=
            static_cast<int>(header.src_node_id)) {
            logger_.warn("SLE-TOPO",
                         "ST 0x06 skip DTU outside current root topology dtu_id=" +
                             std::to_string(device.dtu_id) +
                             ", root=" + std::to_string(header.src_node_id) +
                             ", device_id=" + device.device_id);
            continue;
        }

        model::DeviceInfo normalized_device = devices_[device_it->second];
        normalized_device.dtu_id = device.dtu_id;
        normalized_device.online = true;
        normalized.push_back(std::move(normalized_device));
    }

    {
        std::lock_guard<std::mutex> lock(external_mutex_);

        const auto previous_snapshot_it = external_snapshots_.find(header.src_node_id);
        std::vector<model::DeviceInfo> previous_snapshot;
        if (previous_snapshot_it != external_snapshots_.end())
            previous_snapshot = previous_snapshot_it->second;

        external_snapshots_[header.src_node_id] = normalized;

        std::vector<model::DeviceInfo> aggregate;
        std::set<std::string> aggregate_device_ids;
        for (const auto &root_item : external_snapshots_) {
            for (const auto &device : root_item.second) {
                if (!aggregate_device_ids.insert(device.device_id).second) {
                    const std::string duplicate_device_id = device.device_id;
                    if (previous_snapshot_it != external_snapshots_.end())
                        external_snapshots_[header.src_node_id] = std::move(previous_snapshot);
                    else
                        external_snapshots_.erase(header.src_node_id);
                    logger_.error("SLE-TOPO",
                                  "ST 0x06 duplicate external device across root snapshots device_id=" +
                                      duplicate_device_id);
                    return result;
                }
                aggregate.push_back(device);
            }
        }

        if (static_cast<int>(aggregate.size()) > topology_config_.expected_external_device_count) {
            if (previous_snapshot_it != external_snapshots_.end())
                external_snapshots_[header.src_node_id] = std::move(previous_snapshot);
            else
                external_snapshots_.erase(header.src_node_id);
            logger_.error("SLE-TOPO",
                          "ST 0x06 external device count exceeds expected observed=" +
                              std::to_string(aggregate.size()) +
                              ", expected=" +
                              std::to_string(topology_config_.expected_external_device_count));
            return result;
        }

        rebuildDynamicExternalState();

        int online_count = 0;
        for (const auto &item : dynamic_external_by_device_id_) {
            if (item.second.online)
                ++online_count;
        }

        const bool complete =
            static_cast<int>(aggregate.size()) == topology_config_.expected_external_device_count;

        if (topology_config_.online_policy.emit_online_change) {
            for (const auto &item : dynamic_external_by_device_id_) {
                const auto &device = item.second;
                auto state_it = last_external_online_state_.find(device.device_id);
                if (state_it == last_external_online_state_.end() ||
                    state_it->second != device.online) {
                    last_external_online_state_[device.device_id] = device.online;
                    logger_.info("SLE-TOPO",
                                     "external online changed device_id=" + device.device_id +
                                     ", online=" + std::to_string(device.online ? 1 : 0) +
                                     ", dtu_id=" + std::to_string(device.dtu_id) +
                                     ", reason=" + dynamic_external_reason_by_device_id_[device.device_id]);
                }
            }
        }

        logger_.info("SLE-TOPO",
                     "ST 0x06 accepted root=" + std::to_string(header.src_node_id) +
                         ", observed_external=" + std::to_string(aggregate.size()) +
                         ", online_external=" + std::to_string(online_count) +
                         ", expected_external=" +
                         std::to_string(topology_config_.expected_external_device_count) +
                         ", complete=" + std::to_string(complete ? 1 : 0) +
                         ", snapshot_only=1");
    }

    return buildExternalMapTelemetry(common::nowMs());
}

void SleDataSource::rebuildDynamicExternalState()
{
    std::vector<model::DeviceInfo> aggregate;
    for (const auto &root_item : external_snapshots_) {
        aggregate.insert(aggregate.end(), root_item.second.begin(), root_item.second.end());
    }

    dynamic_external_by_device_id_.clear();
    dynamic_external_reason_by_device_id_.clear();
    dynamic_dtu_id_to_device_ids_.clear();
    for (auto inventory_device : devices_) {
        inventory_device.online = false;
        dynamic_external_reason_by_device_id_[inventory_device.device_id] = "snapshot_missing";
        dynamic_external_by_device_id_[inventory_device.device_id] = inventory_device;
    }

    for (auto device : aggregate) {
        const bool dtu_online = route_table_.isOnline(static_cast<uint16_t>(device.dtu_id));
        device.online = dtu_online;
        dynamic_external_reason_by_device_id_[device.device_id] =
            dtu_online ? "snapshot_present" : "parent_dtu_offline";
        dynamic_external_by_device_id_[device.device_id] = device;
        dynamic_dtu_id_to_device_ids_[device.dtu_id].push_back(device.device_id);
    }
}

std::vector<model::TelemetryData> SleDataSource::buildDtuTopologyTelemetry(int64_t ts_ms) const
{
    std::vector<model::TelemetryData> result;
    const auto routes = route_table_.snapshot();
    result.reserve(routes.size());

    for (const auto &entry : routes) {
        auto dtu_it = dtu_id_to_index_.find(entry.node_id);
        if (dtu_it == dtu_id_to_index_.end()) {
            logger_.warn("SLE-TOPO",
                         "skip telemetry for unknown DTU node_id=" +
                             std::to_string(entry.node_id) +
                             ", strict_inventory=1");
            continue;
        }

        model::TelemetryData data;
        data.device_id = dtu_devices_[dtu_it->second].device_id;
        data.type = model::DeviceType::DtuNode;
        data.ts_ms = ts_ms;
        data.integer_values["online"] = entry.online ? 1 : 0;
        data.integer_values["role"] = entry.role > 0 ? model::sleRoleToCloudRole(entry.role) : 0;
        data.integer_values["node_id"] = entry.node_id;
        data.integer_values["parent_id"] = entry.parent_id;
        data.integer_values["root_id"] = route_table_.findRootByNode(entry.node_id);
        data.string_values["name"] = data.device_id;
        data.string_values["reason"] = entry.online ? "snapshot_present" : "snapshot_missing";
        data.object_values["topology"] = {
            {"parent_id", static_cast<int>(entry.parent_id)},
            {"child_count", static_cast<int>(entry.child_ids.size())},
            {"child_ids", joinChildIds(entry.child_ids)},
        };
        result.push_back(std::move(data));
    }

    return result;
}

std::vector<model::TelemetryData> SleDataSource::buildExternalMapTelemetry(int64_t ts_ms) const
{
    std::vector<model::DeviceInfo> devices;
    std::unordered_map<std::string, std::string> reasons;
    {
        std::lock_guard<std::mutex> lock(external_mutex_);
        devices.reserve(dynamic_external_by_device_id_.size());
        for (const auto &item : dynamic_external_by_device_id_)
            devices.push_back(item.second);
        reasons = dynamic_external_reason_by_device_id_;
    }

    std::vector<model::TelemetryData> result;
    result.reserve(devices.size());

    for (const auto &device : devices) {
        model::TelemetryData data;
        data.device_id = device.device_id;
        data.type = device.type;
        data.ts_ms = ts_ms;
        data.integer_values["online"] = device.online ? 1 : 0;
        data.integer_values["dtu_id"] = device.dtu_id;
        data.integer_values["root_id"] = route_table_.findRootByNode(static_cast<uint16_t>(device.dtu_id));
        data.integer_values["modbus_type"] = device.modbus_type;
        data.integer_values["modbus_addr"] = device.modbus_addr;
        data.string_values["name"] = device.name;
        auto reason_it = reasons.find(device.device_id);
        data.string_values["reason"] = reason_it != reasons.end()
                                           ? reason_it->second
                                           : (device.online ? "snapshot_present" : "snapshot_missing");
        if (state_store_)
            state_store_->overlay(data);
        result.push_back(std::move(data));
    }

    return result;
}

bool SleDataSource::findDeviceForDataFrame(int dtu_id,
                                           model::DeviceInfo *device,
                                           std::string *reason) const
{
    if (device == nullptr)
        return false;
    if (reason)
        reason->clear();

    if (useRootReportTopology()) {
        std::lock_guard<std::mutex> lock(external_mutex_);
        auto map_it = dynamic_dtu_id_to_device_ids_.find(dtu_id);
        if (map_it == dynamic_dtu_id_to_device_ids_.end()) {
            if (reason)
                *reason = "missing_06_mapping";
            return false;
        }
        if (map_it->second.size() != 1) {
            if (reason)
                *reason = "ambiguous_06_mapping_count=" + std::to_string(map_it->second.size());
            return false;
        }

        auto device_it = dynamic_external_by_device_id_.find(map_it->second.front());
        if (device_it == dynamic_external_by_device_id_.end()) {
            if (reason)
                *reason = "mapped_device_missing";
            return false;
        }
        if (!device_it->second.online) {
            if (reason) {
                const auto reason_it =
                    dynamic_external_reason_by_device_id_.find(device_it->second.device_id);
                *reason = reason_it != dynamic_external_reason_by_device_id_.end()
                              ? reason_it->second
                              : "mapped_device_offline";
            }
            return false;
        }

        *device = device_it->second;
        return true;
    }

    auto it = device_dtu_id_to_index_.find(dtu_id);
    if (it != device_dtu_id_to_index_.end()) {
        *device = devices_[it->second];
        return true;
    }
    if (reason)
        *reason = "no_static_device_mapping";
    return false;
}

bool SleDataSource::resolveExternalDeviceRoute(const std::string &device_id,
                                               ExternalDeviceRoute *route,
                                               std::string *error) const
{
    if (route == nullptr)
        return false;
    if (!useRootReportTopology()) {
        if (error)
            *error = "dynamic route query requires topology.source=root_report";
        return false;
    }

    std::lock_guard<std::mutex> lock(external_mutex_);
    const auto device_it = dynamic_external_by_device_id_.find(device_id);
    if (device_it == dynamic_external_by_device_id_.end()) {
        if (error)
            *error = "device not present in external inventory: " + device_id;
        return false;
    }

    const auto reason_it = dynamic_external_reason_by_device_id_.find(device_id);
    const std::string reason = reason_it != dynamic_external_reason_by_device_id_.end()
                                   ? reason_it->second
                                   : "unknown";
    if (!device_it->second.online || device_it->second.dtu_id <= 0) {
        if (error)
            *error = "device route unavailable: " + device_id + ", reason=" + reason;
        return false;
    }

    const int root_id = route_table_.findRootByNode(
        static_cast<uint16_t>(device_it->second.dtu_id));
    if (root_id <= 0) {
        if (error)
            *error = "device parent DTU is not in current 0x05 topology: " + device_id;
        return false;
    }

    route->device = device_it->second;
    route->dtu_id = static_cast<uint16_t>(device_it->second.dtu_id);
    route->root_id = static_cast<uint16_t>(root_id);
    return true;
}

bool SleDataSource::useStaticTopology() const
{
    return topology_config_.source == "static_json" &&
           topology_config_.static_json.enable_for_test;
}

bool SleDataSource::useRootReportTopology() const
{
    return topology_config_.source == "root_report";
}

} // namespace gateway::datasource
