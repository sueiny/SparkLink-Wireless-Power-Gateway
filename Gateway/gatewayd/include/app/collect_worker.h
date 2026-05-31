#pragma once

#include "app/worker_base.h"
#include "codec/thingskit_codec.h"
#include "common/blocking_queue.h"
#include "common/logger.h"
#include "config/config_manager.h"
#include "datasource/mock_data_source.h"

#include <vector>

namespace gateway::app {

// 采集线程入口。
//
// 职责：
// - 按 publish.interval_ms 周期调用 MockDataSource::collect()。
// - 把一批 TelemetryData 放入 telemetry_queue，交给发布线程统一上云。
//
// 非职责：
// - 不拼 ThingsKit JSON。
// - 不直接调用 MQTT。
// - 不处理缓存补传。
class CollectWorker final : public WorkerBase<CollectWorker> {
public:
    CollectWorker(const config::AppConfig &config,
                  log::Logger &logger,
                  datasource::MockDataSource &data_source,
                  common::BlockingQueue<std::vector<model::TelemetryData>> &telemetry_queue);

    const char *name() const override { return "collect"; }

private:
    friend class WorkerBase<CollectWorker>;

    // WorkerBase 会在线程中调用 run()；run() 内部一直循环直到 stop_ 置位。
    void run();

    const config::AppConfig &config_;
    log::Logger &logger_;
    datasource::MockDataSource &data_source_;
    common::BlockingQueue<std::vector<model::TelemetryData>> &telemetry_queue_;
};

} // namespace gateway::app
