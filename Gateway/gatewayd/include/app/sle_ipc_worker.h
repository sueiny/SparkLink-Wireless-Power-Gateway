#pragma once

#include "app/worker_base.h"
#include "common/blocking_queue.h"
#include "common/device_model.h"
#include "common/logger.h"
#include "config/config_manager.h"
#include "datasource/sle_data_source.h"

#include <vector>

namespace gateway::app {

// SLE IPC 工作线程。
//
// 职责：
// - 持续从 SleDataSource 接收 SleIpcFrame 并转为 TelemetryData。
// - 把 TelemetryData 放入 telemetry_queue，交给发布线程统一上云。
//
// 非职责：
// - 不拼 ThingsKit JSON。
// - 不直接调用 MQTT。
// - 不做 Modbus 解析（当前阶段）。
class SleIpcWorker final : public WorkerBase<SleIpcWorker> {
public:
    SleIpcWorker(const config::AppConfig &config,
                 log::Logger &logger,
                 datasource::SleDataSource &data_source,
                 common::BlockingQueue<std::vector<model::TelemetryData>> &telemetry_queue);

    const char *name() const override { return "sle_ipc"; }

private:
    friend class WorkerBase<SleIpcWorker>;
    void run();

    const config::AppConfig &config_;
    log::Logger &logger_;
    datasource::SleDataSource &data_source_;
    common::BlockingQueue<std::vector<model::TelemetryData>> &telemetry_queue_;
};

} // namespace gateway::app
