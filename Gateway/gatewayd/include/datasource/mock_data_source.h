#pragma once

#include "config/config_manager.h"
#include "state/device_state_store.h"

#include <map>
#include <memory>
#include <vector>

namespace gateway::datasource {

class MockDataSource {
public:
    MockDataSource(std::vector<model::DeviceInfo> devices,
                   config::MockConfig mock,
                   int publish_interval_ms,
                   std::shared_ptr<state::DeviceStateStore> state_store);

    bool init();
    std::vector<model::TelemetryData> collect();

private:
    model::TelemetryData buildMeter(const model::DeviceInfo &device,
                                    int branch_index, int64_t ts_ms);
    model::TelemetryData buildEnvSensor(const model::DeviceInfo &device, int64_t ts_ms) const;
    model::TelemetryData buildRelay(const model::DeviceInfo &device, int64_t ts_ms) const;
    void applyStateOverlay(model::TelemetryData &data) const;

    std::vector<model::DeviceInfo> devices_;
    config::MockConfig mock_;
    int64_t publish_interval_ms_ = 5000;
    int64_t tick_ = 0;
    std::map<std::string, double> energy_kwh_;
    std::shared_ptr<state::DeviceStateStore> state_store_;
};

} // namespace gateway::datasource
