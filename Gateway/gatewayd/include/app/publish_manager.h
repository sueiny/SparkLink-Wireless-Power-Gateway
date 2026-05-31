#pragma once

#include "app/publish_types.h"
#include "app/worker_base.h"
#include "app/network_worker.h"
#include "cloud/mqtt_cloud_client.h"
#include "codec/thingskit_codec.h"
#include "command/command_types.h"
#include "common/blocking_queue.h"
#include "common/logger.h"
#include "config/config_manager.h"
#include "storage/cache_store.h"

#include <vector>

namespace gateway::app {

// PublishManager 构造依赖较多，用聚合结构让调用点可读一些。
// 这些都是生命周期由 GatewayApp 持有的对象，PublishManager 只保存引用。
struct PublishManagerDeps {
    const config::AppConfig &config;
    log::Logger &logger;
    NetworkWorker &network_worker;
    cloud::MqttCloudClient &cloud_client;
    storage::CacheStore *cache_store;
    common::BlockingQueue<std::vector<model::TelemetryData>> &telemetry_queue;
    common::BlockingQueue<PublishMessage> &publish_queue;
};

// 发布线程入口。
//
// 职责：
// - 维护 MQTT 连接和命令 topic 订阅。
// - 消费遥测队列，把 TelemetryData 编码为 ThingsKit payload。
// - 消费统一发布队列，集中 publish。
// - 遥测失败写入 SQLite 缓存，网络恢复后补传。
// - 命令响应失败只做内存重试，不写入遥测缓存。
class PublishManager final : public WorkerBase<PublishManager> {
public:
    explicit PublishManager(PublishManagerDeps deps);

    const char *name() const override { return "publish"; }

private:
    friend class WorkerBase<PublishManager>;
    void run();

    // 把内部 TelemetryData 批次转成 ThingsKit 网关子设备遥测消息。
    void publishTelemetryBatch(const std::vector<model::TelemetryData> &telemetry);

    // 网络可用且云端可达时，按批次补传 SQLite 中的遥测缓存。
    void flushTelemetryCache();

    // 周期性发布网关在线状态、网络类型和缓存数量。
    void publishGatewayStatusIfDue();

    // 确保 MQTT 已连接，并确保 RPC/网关命令 topic 已订阅。
    bool ensureCloudConnected();

    // 处理实时发布队列和命令响应延迟重试队列。
    void publishPendingMessages(std::vector<PublishMessage> &delayed_messages);

    bool publishMessage(const PublishMessage &message);

    // 根据消息类型处理失败：遥测落库，命令响应重试，其它状态类消息丢弃并记录。
    void handlePublishFailure(PublishMessage message,
                              std::vector<PublishMessage> &delayed_messages);

    const config::AppConfig &config_;
    log::Logger &logger_;
    NetworkWorker &network_worker_;
    cloud::MqttCloudClient &cloud_client_;
    storage::CacheStore *cache_store_ = nullptr;
    common::BlockingQueue<std::vector<model::TelemetryData>> &telemetry_queue_;
    common::BlockingQueue<PublishMessage> &publish_queue_;
    int64_t last_status_ms_ = 0;
    bool command_topics_subscribed_ = false;
};

} // namespace gateway::app
