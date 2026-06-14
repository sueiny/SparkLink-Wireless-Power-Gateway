#include "app/sle_ipc_worker.h"
#include "common/time_utils.h"

namespace gateway::app {

SleIpcWorker::SleIpcWorker(
    const config::AppConfig &config,
    log::Logger &logger,
    datasource::SleDataSource &data_source,
    common::BlockingQueue<std::vector<model::TelemetryData>> &telemetry_queue)
    : config_(config),
      logger_(logger),
      data_source_(data_source),
      telemetry_queue_(telemetry_queue)
{
}

void SleIpcWorker::run()
{
    const int interval_ms = config_.publish.interval_ms > 0 ? config_.publish.interval_ms : 5000;

    logger_.info("SLE-IPC", "worker started, batch window=" + std::to_string(interval_ms) + "ms");

    // 等待 sle-daemon 首次连接
    if (!data_source_.waitForClient(5000)) {
        logger_.warn("SLE-IPC", "no sle-daemon connection within 5s, will retry in run loop");
    }

    while (!stop_.load()) {
        // 在一个 publish 窗口内收集所有到达的帧
        std::vector<model::TelemetryData> batch;
        int64_t window_end = common::nowMs() + interval_ms;

        while (common::nowMs() < window_end && !stop_.load()) {
            auto frames = data_source_.collect();  // 内部有 accept 超时
            if (!frames.empty()) {
                batch.insert(batch.end(),
                    std::make_move_iterator(frames.begin()),
                    std::make_move_iterator(frames.end()));
            }
        }

        // 不再本地生成 DTU 心跳，所有数据都从 IPC 接收

        if (!batch.empty()) {
            logger_.info("SLE-IPC", "batch collected " + std::to_string(batch.size()) + " devices");
            telemetry_queue_.push(std::move(batch));
        }
    }

    logger_.info("SLE-IPC", "worker stopped");
}

} // namespace gateway::app
