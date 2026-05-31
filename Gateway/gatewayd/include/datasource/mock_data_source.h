#pragma once

#include "config/config_manager.h"
#include "state/device_state_store.h"

#include <map>
#include <memory>
#include <vector>

namespace gateway::datasource {

// 第一阶段数据源：按配置里的设备清单生成稳定可控的模拟遥测。
//
// 命令下发后的状态覆盖来自 DeviceStateStore，因此模拟值会在后续周期持续保持。
class MockDataSource {
public:
    MockDataSource(std::vector<model::DeviceInfo> devices,
                   config::MockConfig mock,
                   int publish_interval_ms,
                   std::shared_ptr<state::DeviceStateStore> state_store);

    bool init();

    // 每次调用返回一批设备遥测，GatewayApp/CollectWorker 不关心具体模拟算法。
    std::vector<model::TelemetryData> collect();

private:
    // 以下 build* 方法分别生成不同物模型的模拟字段。
    model::TelemetryData buildBranchMeter(const model::DeviceInfo &device,
                                          int branch_index,
                                          int64_t ts_ms);
    model::TelemetryData buildMainMeter(const model::DeviceInfo &device,
                                        int64_t ts_ms,
                                        double branch_power_sum,
                                        double branch_current_sum);
    model::TelemetryData buildEnvSensor(const model::DeviceInfo &device, int64_t ts_ms) const;
    model::TelemetryData buildRelay(const model::DeviceInfo &device, int64_t ts_ms) const;
    model::TelemetryData buildDtuNode(const model::DeviceInfo &device, int64_t ts_ms) const;
    void applyStateOverlay(model::TelemetryData &data) const;

    std::vector<model::DeviceInfo> devices_;
    config::MockConfig mock_;
    int64_t publish_interval_ms_ = 5000;
    int64_t tick_ = 0;
    std::map<std::string, double> energy_kwh_;
    std::shared_ptr<state::DeviceStateStore> state_store_;
};

} // namespace gateway::datasource
