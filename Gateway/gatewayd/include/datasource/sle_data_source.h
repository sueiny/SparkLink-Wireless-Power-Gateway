#pragma once

#include "common/device_model.h"
#include "common/logger.h"
#include "config/config_manager.h"
#include "datasource/ipc_receiver.h"
#include "datasource/route_table.h"
#include "state/device_state_store.h"

#include <map>
#include <mutex>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace gateway::datasource {

class SleDataSource {
public:
    struct ExternalDeviceRoute {
        model::DeviceInfo device;
        uint16_t dtu_id = 0;
        uint16_t root_id = 0;
    };

    SleDataSource(std::vector<model::DeviceInfo> devices,
                  std::vector<model::DtuDeviceInfo> dtu_devices,
                  config::TopologyConfig topology_config,
                  std::shared_ptr<state::DeviceStateStore> state_store,
                  RouteTable &route_table,
                  log::Logger &logger);

    bool init(const std::string &socket_path);
    void deinit();

    bool waitForClient(int timeout_ms);
    std::vector<model::TelemetryData> collect();
    bool isConnected() const;

    // 查询 root_report 模式下由 0x06/0x05 形成的当前下行路由。
    bool resolveExternalDeviceRoute(const std::string &device_id,
                                    ExternalDeviceRoute *route,
                                    std::string *error) const;

private:
    std::vector<model::TelemetryData> handleDataFrame(const std::vector<uint8_t> &raw,
                                                       const codec::SleFrameHeader &header);
    std::vector<model::TelemetryData> handleDtuTopologyFrame(
        const std::vector<uint8_t> &raw,
        const codec::SleFrameHeader &header);
    std::vector<model::TelemetryData> handleExternalMapFrame(
        const std::vector<uint8_t> &raw,
        const codec::SleFrameHeader &header);
    std::vector<model::TelemetryData> buildDtuTopologyTelemetry(int64_t ts_ms) const;
    std::vector<model::TelemetryData> buildExternalMapTelemetry(int64_t ts_ms) const;
    void rebuildDynamicExternalState();

    // 按 0x06 当前映射查找外接设备；正式 root_report 模式不从 JSON 静态 dtu_id 推断。
    bool findDeviceForDataFrame(int dtu_id,
                                model::DeviceInfo *device,
                                std::string *reason = nullptr) const;

    bool useStaticTopology() const;
    bool useRootReportTopology() const;

    std::vector<model::DeviceInfo> devices_;        // 外接设备
    std::vector<model::DtuDeviceInfo> dtu_devices_; // DTU 节点
    std::unordered_map<int, size_t> dtu_id_to_index_;  // dtu_id → dtu_devices_ 索引 (O(1))
    std::unordered_map<int, size_t> device_dtu_id_to_index_;  // dtu_id → devices_ 索引 (O(1))
    std::unordered_map<std::string, size_t> device_id_to_index_;  // device_id → devices_ 索引
    config::TopologyConfig topology_config_;
    mutable std::mutex external_mutex_;
    std::map<uint16_t, std::vector<model::DeviceInfo>> external_snapshots_;
    std::unordered_map<std::string, model::DeviceInfo> dynamic_external_by_device_id_;
    std::unordered_map<std::string, std::string> dynamic_external_reason_by_device_id_;
    std::unordered_map<int, std::vector<std::string>> dynamic_dtu_id_to_device_ids_;
    std::unordered_map<std::string, bool> last_external_online_state_;
    std::shared_ptr<state::DeviceStateStore> state_store_;
    RouteTable &route_table_;
    IpcReceiver receiver_;
    log::Logger &logger_;
    int64_t tick_ = 0;
};

} // namespace gateway::datasource
