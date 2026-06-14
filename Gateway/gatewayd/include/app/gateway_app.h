#pragma once

#include "app/command_manager.h"
#include "app/collect_worker.h"
#include "app/network_worker.h"
#include "app/publish_manager.h"
#include "app/publish_types.h"
#include "app/sle_ipc_worker.h"
#include "cloud/mqtt_cloud_client.h"
#include "command/thing_model_service_registry.h"
#include "common/blocking_queue.h"
#include "common/logger.h"
#include "config/config_manager.h"
#include "codec/thingskit_codec.h"
#include "datasource/ipc_cmd_sender.h"
#include "datasource/mock_data_source.h"
#include "datasource/sle_data_source.h"
#include "network/net_manager.h"
#include "datasource/route_table.h"
#include "state/device_state_store.h"
#include "storage/cache_store.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gateway::app {

// GatewayApp 是 gatewayd 的主流程编排类。
// 它负责把配置、数据源、网络选择、JSON 映射和云端发布串起来；
// 但不直接实现 MQTT 协议细节、不生成模拟数据、不做 Linux 网络检测。
class GatewayApp {
public:
    explicit GatewayApp(std::string config_path);

    // 加载配置、初始化日志、数据源、网络和 MQTT client。
    // 失败返回 false，调用方直接退出进程。
    bool init();

    // gatewayd 主循环：启动采集、发布、命令线程并等待退出信号。
    // quit 由 SIGINT/SIGTERM 修改，确保程序可干净退出。
    int run(const std::atomic_bool &quit);

private:
    void enqueueCloudCommand(const std::string &topic, const std::string &payload);
    void enqueuePublishMessage(PublishMessage message);
    void stopQueues();
    void stopWorkers();
    void joinWorkers();

    std::string config_path_;
    config::ConfigManager config_manager_;
    log::Logger logger_;
    network::NetManager net_manager_;
    datasource::RouteTable route_table_;
    std::unique_ptr<datasource::MockDataSource> mock_data_source_;
    std::unique_ptr<datasource::SleDataSource> sle_data_source_;
    std::unique_ptr<datasource::IpcCmdSender> ipc_cmd_sender_;
    std::unique_ptr<cloud::MqttCloudClient> gateway_cloud_client_;
    std::unique_ptr<storage::CacheStore> cache_store_;

    command::ThingModelServiceRegistry service_registry_;
    std::shared_ptr<state::DeviceStateStore> state_store_;
    std::unique_ptr<CollectWorker> collect_worker_;
    std::unique_ptr<SleIpcWorker> sle_ipc_worker_;
    std::unique_ptr<NetworkWorker> network_worker_;
    std::unique_ptr<PublishManager> publish_manager_;
    std::unique_ptr<CommandManager> command_manager_;

    common::BlockingQueue<std::vector<model::TelemetryData>> telemetry_queue_{64};
    common::BlockingQueue<command::RawCommandMessage> command_queue_{64};
    common::BlockingQueue<PublishMessage> publish_queue_{256};
};

} // namespace gateway::app
