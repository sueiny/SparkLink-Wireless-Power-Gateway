#include "app/publish_manager.h"

#include "codec/thingskit_topics.h"
#include "common/time_utils.h"
#include "network/network_utils.h"

#include <chrono>
#include <thread>

namespace gateway::app {
namespace {

constexpr size_t kMaxCacheFlushCount = 20;
constexpr int kMaxCommandResponseRetries = 5;
constexpr int kPublishIdleWaitMs = 300;
constexpr int64_t kCacheFlushIntervalMs = 2000;

const char *publishKindText(PublishMessageKind kind)
{
    switch (kind) {
    case PublishMessageKind::Telemetry:
        return "telemetry";
    case PublishMessageKind::CommandResponse:
        return "command_response";
    case PublishMessageKind::GatewayStatus:
        return "gateway_status";
    default:
        return "unknown";
    }
}

int commandResponseRetryDelayMs(int retry_count)
{
    if (retry_count <= 1)
        return 1000;
    if (retry_count == 2)
        return 3000;
    return 5000;
}

} // namespace

PublishManager::PublishManager(PublishManagerDeps deps)
    : config_(deps.config),
      logger_(deps.logger),
      network_worker_(deps.network_worker),
      cloud_client_(deps.cloud_client),
      cache_store_(deps.cache_store),
      telemetry_queue_(deps.telemetry_queue),
      publish_queue_(deps.publish_queue)
{
}

void PublishManager::run()
{
    int64_t last_cache_flush_ms = 0;
    std::vector<PublishMessage> delayed_messages;

    while (!stop_.load()) {
        // 先处理命令响应重试和其它待发布消息，再消费新的遥测批次。
        publishPendingMessages(delayed_messages);

        std::vector<model::TelemetryData> telemetry;
        if (telemetry_queue_.pop(telemetry, kPublishIdleWaitMs))
            publishTelemetryBatch(telemetry);

        const int64_t after_publish_ms = common::nowMs();
        if (last_cache_flush_ms == 0 ||
            after_publish_ms - last_cache_flush_ms >= kCacheFlushIntervalMs) {
            flushTelemetryCache();
            last_cache_flush_ms = after_publish_ms;
        }

        publishGatewayStatusIfDue();
    }

    for (auto &message : delayed_messages)
        message.next_retry_ts_ms = 0;
    // 退出前给内存中的命令响应最后一次发布机会，但不无限阻塞。
    publishPendingMessages(delayed_messages);
    logger_.info("APP", "publish thread stopped");
}

void PublishManager::publishTelemetryBatch(const std::vector<model::TelemetryData> &telemetry)
{
    if (telemetry.empty())
        return;

    const std::string payload =
        codec::ThingsKitCodec::buildGatewaySubDeviceTelemetryPayload(telemetry);
    publish_queue_.push({
        codec::thingskit::kGatewaySubDeviceTelemetryTopic,
        payload,
        PublishMessageKind::Telemetry,
        0,
        0,
    });
}

void PublishManager::flushTelemetryCache()
{
    const auto network_state = network_worker_.state();
    if (!cache_store_ || !network_state.available || !network_state.cloud_reachable)
        return;

    if (!ensureCloudConnected())
        return;

    const auto pending = cache_store_->loadPendingTelemetry(kMaxCacheFlushCount);
    if (pending.empty())
        return;

    std::vector<storage::CachedTelemetry> remains;
    remains.reserve(pending.size());

    bool stop_on_failure = false;
    size_t attempted = 0;
    for (const auto &item : pending) {
        if (attempted >= kMaxCacheFlushCount) {
            remains.push_back(item);
            continue;
        }

        if (!stop_on_failure && cloud_client_.publishRaw(item.topic, item.payload)) {
            logger_.info("CACHE", "flushed telemetry cache topic=" + item.topic +
                                      ", bytes=" + std::to_string(item.payload.size()));
            ++attempted;
            continue;
        }

        // 一旦某条补传失败，保留本批后续记录，避免乱序删除或持续打爆网络。
        stop_on_failure = true;
        ++attempted;
        remains.push_back(item);
    }

    if (cache_store_->rewritePendingTelemetry(remains)) {
        logger_.info("CACHE", "cache flush done, success=" +
                                  std::to_string(pending.size() - remains.size()) +
                                  ", remains=" + std::to_string(remains.size()));
    }
}

void PublishManager::publishGatewayStatusIfDue()
{
    const int64_t now = common::nowMs();
    const int interval_ms = config_.publish.gateway_status_interval_ms > 0
                                ? config_.publish.gateway_status_interval_ms
                                : 10000;

    if (last_status_ms_ != 0 && now - last_status_ms_ < interval_ms)
        return;

    last_status_ms_ = now;

    model::GatewayStatus status;
    status.gateway_id = config_.gateway.gateway_id;
    status.version = config_.gateway.version;
    const auto network_state = network_worker_.state();
    status.network_type = network_state.available ? network_state.name : "none";
    status.network_ifname = network_state.available ? network_state.ifname : "none";
    status.cloud_connected = cloud_client_.isConnected();
    status.device_count = static_cast<int>(config_.devices.size());
    status.cache_count = cache_store_ ? static_cast<int>(cache_store_->pendingCount()) : 0;
    status.ts_ms = now;

    if (network_state.available && network_state.cloud_reachable) {
        ensureCloudConnected();
        status.cloud_connected = cloud_client_.isConnected();
    }

    const std::string payload = codec::ThingsKitCodec::buildGatewayAttributesValuesPayload(status);
    logger_.info("ATTR", payload);

    publish_queue_.push({
        codec::thingskit::kGatewayAttributesTopic,
        payload,
        PublishMessageKind::GatewayStatus,
        0,
        0,
    });
    publish_queue_.push({
        codec::thingskit::kGatewayTelemetryTopic,
        payload,
        PublishMessageKind::GatewayStatus,
        0,
        0,
    });
}

bool PublishManager::ensureCloudConnected()
{
    if (cloud_client_.isConnected()) {
        if (!command_topics_subscribed_) {
            // MQTT 可能重连成功但订阅状态丢失，所以连接存在时也要确认订阅标记。
            const bool rpc_ok = cloud_client_.subscribeRaw(codec::thingskit::kRpcRequestTopicFilter);
            const bool gateway_ok =
                cloud_client_.subscribeRaw(codec::thingskit::kGatewayCommandRequestTopic);
            command_topics_subscribed_ = rpc_ok && gateway_ok;
        }
        return true;
    }

    command_topics_subscribed_ = false;

    // 在 MQTT 连接前，确保默认路由走选中的网络接口。
    // mosquitto 内部创建 socket 时使用系统默认路由，
    // 必须在 connect 之前把路由切到正确的接口。
    const auto ns = network_worker_.state();
    if (ns.available) {
        cloud_client_.setBindInterface(ns.ifname);
        // 强制设置默认路由走选中接口
        network::setDefaultRouteVia(ns.ifname);
    }

    if (!cloud_client_.connect())
        return false;

    const bool rpc_ok = cloud_client_.subscribeRaw(codec::thingskit::kRpcRequestTopicFilter);
    const bool gateway_ok =
        cloud_client_.subscribeRaw(codec::thingskit::kGatewayCommandRequestTopic);
    command_topics_subscribed_ = rpc_ok && gateway_ok;
    return true;
}

void PublishManager::publishPendingMessages(std::vector<PublishMessage> &delayed_messages)
{
    const int64_t now = common::nowMs();
    for (auto it = delayed_messages.begin(); it != delayed_messages.end();) {
        if (it->next_retry_ts_ms > now) {
            ++it;
            continue;
        }

        PublishMessage message = std::move(*it);
        it = delayed_messages.erase(it);
        if (!publishMessage(message))
            handlePublishFailure(std::move(message), delayed_messages);
    }

    PublishMessage message;
    while (publish_queue_.pop(message, 0)) {
        if (!publishMessage(message))
            handlePublishFailure(std::move(message), delayed_messages);
    }
}

bool PublishManager::publishMessage(const PublishMessage &message)
{
    const auto network_state = network_worker_.state();
    if (!network_state.available || !network_state.cloud_reachable || !ensureCloudConnected()) {
        logger_.warn("MQTT", std::string("publish skipped kind=") +
                                 publishKindText(message.kind) +
                                 ", topic=" + message.topic);
        return false;
    }

    if (!cloud_client_.publishRaw(message.topic, message.payload))
        return false;

    logger_.info("MQTT", std::string("publish success kind=") +
                             publishKindText(message.kind) +
                             ", topic=" + message.topic +
                             ", bytes=" + std::to_string(message.payload.size()));
    return true;
}

void PublishManager::handlePublishFailure(PublishMessage message,
                                          std::vector<PublishMessage> &delayed_messages)
{
    if (message.kind == PublishMessageKind::Telemetry) {
        // 只有上行遥测进入 SQLite 缓存；命令响应不能混入遥测补传表。
        if (cache_store_)
            cache_store_->appendTelemetry(message.topic, message.payload);
        return;
    }

    if (message.kind == PublishMessageKind::CommandResponse) {
        if (message.retry_count >= kMaxCommandResponseRetries) {
            logger_.warn("CMD", "command response dropped topic=" + message.topic +
                                    ", retries=" + std::to_string(message.retry_count));
            return;
        }

        ++message.retry_count;
        message.next_retry_ts_ms =
            common::nowMs() + commandResponseRetryDelayMs(message.retry_count);
        logger_.warn("CMD", "command response retry topic=" + message.topic +
                                ", retry=" + std::to_string(message.retry_count));
        delayed_messages.push_back(std::move(message));
        return;
    }

    logger_.warn("MQTT", std::string("publish dropped kind=") +
                             publishKindText(message.kind) +
                             ", topic=" + message.topic);
}

} // namespace gateway::app
