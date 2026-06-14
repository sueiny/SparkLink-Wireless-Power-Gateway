#pragma once

#include "common/device_model.h"
#include "common/logger.h"
#include "config/config_manager.h"
#include "datasource/ipc_receiver.h"
#include "datasource/route_table.h"
#include "state/device_state_store.h"

#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace gateway::datasource {

class SleDataSource {
public:
    SleDataSource(std::vector<model::DeviceInfo> devices,
                  std::vector<model::DtuDeviceInfo> dtu_devices,
                  std::shared_ptr<state::DeviceStateStore> state_store,
                  RouteTable &route_table,
                  log::Logger &logger);

    bool init(const std::string &socket_path);
    void deinit();

    bool waitForClient(int timeout_ms);
    std::vector<model::TelemetryData> collect();
    bool isConnected() const;

    // 为所有 DTU 节点生成心跳 TelemetryData。
    // online 由路由表存在性决定。
    std::vector<model::TelemetryData> generateDtuHeartbeats(int64_t ts_ms) const;

private:
    std::vector<model::TelemetryData> handleDataFrame(const std::vector<uint8_t> &raw,
                                                       const codec::SleFrameHeader &header);
    std::vector<model::TelemetryData> handleHeartbeatFrame(const std::vector<uint8_t> &raw,
                                                            const codec::SleFrameHeader &header);
    void handleTopoFrame(const std::vector<uint8_t> &raw, const codec::SleFrameHeader &header);

    // 按 dtu_id 查找外接设备 (O(1) 查找)
    const model::DeviceInfo *findDeviceByDtuId(int dtu_id) const;

    std::vector<model::DeviceInfo> devices_;        // 外接设备
    std::vector<model::DtuDeviceInfo> dtu_devices_; // DTU 节点
    std::unordered_map<int, size_t> dtu_id_to_index_;  // dtu_id → dtu_devices_ 索引 (O(1))
    std::unordered_map<int, size_t> device_dtu_id_to_index_;  // dtu_id → devices_ 索引 (O(1))
    std::shared_ptr<state::DeviceStateStore> state_store_;
    RouteTable &route_table_;
    IpcReceiver receiver_;
    log::Logger &logger_;
    int64_t tick_ = 0;
};

} // namespace gateway::datasource
