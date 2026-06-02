#include "app/gateway_app.h"

#include "common/constants.h"
#include "common/file_utils.h"
#include "common/time_utils.h"

#include <algorithm>
#include <chrono>
#include <sstream>
#include <thread>
#include <utility>

namespace gateway::app {
namespace {

constexpr int64_t kQueueDropLogIntervalMs = 10000;

config::ThingsKitConfig gatewayCloudConfig(const config::ThingsKitConfig &base)
{
    config::ThingsKitConfig gateway_cfg = base;
    gateway_cfg.topic_prefix = "v1/devices/me";
    return gateway_cfg;
}

void logQueueDropDelta(log::Logger &logger,
                       const char *queue_name,
                       size_t current,
                       size_t &last)
{
    if (current > last) {
        logger.warn("APP", std::string("queue dropped messages queue=") +
                               queue_name +
                               ", delta=" + std::to_string(current - last) +
                               ", total=" + std::to_string(current));
    }
    last = current;
}

} // namespace

GatewayApp::GatewayApp(std::string config_path)
    : config_path_(std::move(config_path))
{
}

bool GatewayApp::init()
{
    std::string error;
    if (!config_manager_.load(config_path_, &error)) {
        logger_.error("APP", error);
        return false;
    }

    const auto &cfg = config_manager_.config();
    if (!logger_.init(cfg.log.dir, cfg.log.level)) {
        logger_.error("APP", "failed to initialize log dir: " + cfg.log.dir);
        return false;
    }

    std::ostringstream summary;
    summary << "gatewayd start, gateway_id=" << cfg.gateway.gateway_id
            << ", version=" << cfg.gateway.version
            << ", devices=" << cfg.devices.size()
            << ", mqtt_host=" << cfg.thingskit.host;
    logger_.info("APP", summary.str());

    // 物模型必须先加载成功，后续命令校验严格依赖 services/inputData。
    if (!service_registry_.load(config_path_, logger_))
        return false;

    // 状态库先于数据源初始化。数据源每轮采集会读取状态覆盖值，
    // 这样云端命令写入的继电器/电表状态能在周期遥测中持续保持。
    state_store_ = std::make_shared<state::DeviceStateStore>();
    if (!state_store_->init(common::kDefaultDbPath, logger_))
        return false;

    // 根据 sle.enable 配置选择数据源：SLE 真实数据 或 模拟数据。
    if (cfg.sle.enable) {
        sle_data_source_ = std::make_unique<datasource::SleDataSource>(
            cfg.devices, cfg.dtu_devices, state_store_, route_table_, logger_);
        if (!sle_data_source_->init(cfg.sle.data_socket)) {
            logger_.error("DATA", "failed to initialize SleDataSource");
            return false;
        }
        logger_.info("DATA", "SleDataSource initialized, socket=" + cfg.sle.data_socket);
    } else {
        mock_data_source_ = std::make_unique<datasource::MockDataSource>(
            cfg.devices, cfg.mock, cfg.publish.interval_ms, state_store_);
        if (!mock_data_source_->init()) {
            logger_.error("DATA", "failed to initialize MockDataSource");
            return false;
        }
        logger_.info("DATA", "MockDataSource initialized");
    }

    if (!net_manager_.init(cfg.network, &logger_)) {
        logger_.error("NET", "failed to initialize NetManager");
        return false;
    }

    gateway_cloud_client_ = std::make_unique<cloud::MqttCloudClient>(
        gatewayCloudConfig(cfg.thingskit), logger_);
    gateway_cloud_client_->setMessageCallback(
        [this](const std::string &topic, const std::string &payload) {
            // libmosquitto 回调线程只入队，真正解析和响应由 CommandManager 完成。
            enqueueCloudCommand(topic, payload);
        });

    if (cfg.publish.enable_cache) {
        cache_store_ = std::make_unique<storage::CacheStore>(
            common::kDefaultDbPath, cfg.publish.cache_ttl_ms, logger_);
        if (!cache_store_->init()) {
            logger_.error("CACHE", "failed to initialize cache store");
            return false;
        }
    } else {
        logger_.warn("CACHE", "telemetry cache disabled by config");
    }

    if (cfg.sle.enable) {
        sle_ipc_worker_ = std::make_unique<SleIpcWorker>(
            cfg,
            logger_,
            *sle_data_source_,
            telemetry_queue_);
    } else {
        collect_worker_ = std::make_unique<CollectWorker>(
            cfg,
            logger_,
            *mock_data_source_,
            telemetry_queue_);
    }
    network_worker_ = std::make_unique<NetworkWorker>(logger_, net_manager_);
    publish_manager_ = std::make_unique<PublishManager>(PublishManagerDeps{
        cfg,
        logger_,
        *network_worker_,
        *gateway_cloud_client_,
        cache_store_.get(),
        telemetry_queue_,
        publish_queue_});
    command_manager_ = std::make_unique<CommandManager>(
        cfg,
        logger_,
        service_registry_,
        state_store_,
        command_queue_,
        telemetry_queue_,
        publish_queue_);

    return true;
}

int GatewayApp::run(const std::atomic_bool &quit)
{
    logger_.info("APP", "starting gatewayd worker threads");
    if (collect_worker_)
        collect_worker_->start();
    if (sle_ipc_worker_)
        sle_ipc_worker_->start();
    network_worker_->start();
    publish_manager_->start();
    command_manager_->start();

    size_t last_telemetry_drops = telemetry_queue_.droppedCount();
    size_t last_command_drops = command_queue_.droppedCount();
    size_t last_publish_drops = publish_queue_.droppedCount();
    int64_t last_drop_log_ms = common::nowMs();

    // 主线程不参与业务处理，只周期性观察队列丢弃情况，并等待退出信号。
    while (!quit.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        const int64_t now = common::nowMs();
        if (now - last_drop_log_ms >= kQueueDropLogIntervalMs) {
            logQueueDropDelta(logger_, "telemetry", telemetry_queue_.droppedCount(), last_telemetry_drops);
            logQueueDropDelta(logger_, "command", command_queue_.droppedCount(), last_command_drops);
            logQueueDropDelta(logger_, "publish", publish_queue_.droppedCount(), last_publish_drops);
            last_drop_log_ms = now;
        }
    }

    stopWorkers();
    stopQueues();
    joinWorkers();

    if (gateway_cloud_client_)
        gateway_cloud_client_->disconnect();

    logger_.info("APP", "gatewayd exit");
    return 0;
}

void GatewayApp::enqueueCloudCommand(const std::string &topic, const std::string &payload)
{
    command::RawCommandMessage message;
    message.topic = topic;
    message.payload = payload;
    message.received_ts_ms = common::nowMs();
    command_queue_.push(std::move(message));
    logger_.info("CMD", "command enqueued topic=" + topic +
                            ", bytes=" + std::to_string(payload.size()));
}

void GatewayApp::enqueuePublishMessage(PublishMessage message)
{
    publish_queue_.push(std::move(message));
}

void GatewayApp::stopQueues()
{
    telemetry_queue_.stop();
    command_queue_.stop();
    publish_queue_.stop();
}

void GatewayApp::stopWorkers()
{
    // 先停止所有 worker，再释放资源。顺序很重要：
    // SleIpcWorker 可能阻塞在 read()，必须先 stop() 使其退出循环，
    // join() 确保线程退出后才能安全 deinit data source。
    if (collect_worker_)
        collect_worker_->stop();
    if (sle_ipc_worker_)
        sle_ipc_worker_->stop();
    if (network_worker_)
        network_worker_->stop();
    if (publish_manager_)
        publish_manager_->stop();
    if (command_manager_)
        command_manager_->stop();
}

void GatewayApp::joinWorkers()
{
    if (collect_worker_)
        collect_worker_->join();
    if (sle_ipc_worker_)
        sle_ipc_worker_->join();
    // join 之后 worker 线程已退出，此时安全释放 data source
    if (sle_data_source_)
        sle_data_source_->deinit();
    if (command_manager_)
        command_manager_->join();
    if (publish_manager_)
        publish_manager_->join();
    if (network_worker_)
        network_worker_->join();
}

} // namespace gateway::app
