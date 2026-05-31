#include "app/collect_worker.h"

#include "common/time_utils.h"

namespace gateway::app {

CollectWorker::CollectWorker(
    const config::AppConfig &config,
    log::Logger &logger,
    datasource::MockDataSource &data_source,
    common::BlockingQueue<std::vector<model::TelemetryData>> &telemetry_queue)
    : config_(config),
      logger_(logger),
      data_source_(data_source),
      telemetry_queue_(telemetry_queue)
{
}

void CollectWorker::run()
{
    const int interval_ms = config_.publish.interval_ms > 0 ? config_.publish.interval_ms : 5000;

    while (!stop_.load()) {
        const auto telemetry = data_source_.collect();
        if (!telemetry.empty()) {
            logger_.info("DATA", "collected " + std::to_string(telemetry.size()) + " devices");
            for (const auto &item : telemetry)
                logger_.debug("DATA", codec::ThingsKitCodec::buildTelemetryPayload(item));
            telemetry_queue_.push(telemetry);
        }

        common::interruptibleSleep(stop_, interval_ms);
    }

    logger_.info("APP", "collect worker stopped");
}

} // namespace gateway::app
