#include "app/publish_manager.h"

#include "codec/thingskit_topics.h"
#include "common/time_utils.h"
#include "network/network_utils.h"

#include <chrono>
#include <map>
#include <sstream>
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
    case PublishMessageKind::RuleEvent:
        return "rule_event";
    case PublishMessageKind::CommandResponse:
        return "command_response";
    case PublishMessageKind::GatewayStatus:
        return "gateway_status";
    default:
        return "unknown";
    }
}

std::string commandLogFields(const PublishMessage &message)
{
    if (message.kind != PublishMessageKind::CommandResponse)
        return {};

    std::string fields;
    if (!message.request_id.empty())
        fields += ", request_id=" + message.request_id;
    if (!message.method.empty())
        fields += ", method=" + message.method;
    if (!message.target.empty())
        fields += ", target=" + message.target;
    return fields;
}

int commandResponseRetryDelayMs(int retry_count)
{
    if (retry_count <= 1)
        return 1000;
    if (retry_count == 2)
        return 3000;
    return 5000;
}

std::string telemetryDeviceIds(const std::vector<model::TelemetryData> &telemetry)
{
    std::ostringstream oss;
    for (size_t i = 0; i < telemetry.size(); ++i) {
        if (i > 0)
            oss << ',';
        oss << telemetry[i].device_id;
    }
    return oss.str();
}

bool telemetryOnlineValue(const model::TelemetryData &data, bool *online)
{
    if (online == nullptr)
        return false;

    const auto int_it = data.integer_values.find("online");
    if (int_it != data.integer_values.end()) {
        *online = int_it->second != 0;
        return true;
    }

    const auto bool_it = data.bool_values.find("online");
    if (bool_it != data.bool_values.end()) {
        *online = bool_it->second;
        return true;
    }

    return false;
}

} // namespace

PublishManager::PublishManager(PublishManagerDeps deps)
    : config_(deps.config),
      logger_(deps.logger),
      network_worker_(deps.network_worker),
      cloud_client_(deps.cloud_client),
      cache_store_(deps.cache_store),
      telemetry_queue_(deps.telemetry_queue),
      command_queue_(deps.command_queue),
      publish_queue_(deps.publish_queue),
      rule_engine_(deps.config),
      ai_analyzer_(deps.config, deps.logger)
{
    for (const auto &dtu : config_.dtu_devices) {
        if (!dtu.device_id.empty())
            device_online_states_[dtu.device_id] = false;
    }
    for (const auto &device : config_.devices) {
        if (!device.device_id.empty())
            device_online_states_[device.device_id] = false;
    }
}

void PublishManager::run()
{
    ai_analyzer_.init();

    int64_t last_cache_flush_ms = 0;
    std::vector<PublishMessage> delayed_messages;

    while (!stop_.load()) {
        // 发布线程是 MQTT 的唯一写入口：
        // 先处理命令响应重试和其它待发布消息，再消费新的遥测批次。
        // 这样命令线程只入队，不会和 telemetry/cache 补传同时操作 cloud_client_。
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

    auto coalesced = coalesceTelemetryBatch(telemetry);
    if (coalesced.empty())
        return;
    if (coalesced.size() != telemetry.size()) {
        logger_.info("MQTT", "telemetry batch coalesced raw=" +
                                 std::to_string(telemetry.size()) +
                                 ", unique=" + std::to_string(coalesced.size()));
    }
    updateDeviceOnlineStates(coalesced);

    const int64_t now = common::nowMs();
    const bool offline_active = offlineAnalysisActive(now);
    if (offline_active) {
        const auto evaluation = rule_engine_.evaluate(coalesced, true, now);
        enqueueRuleEvents(evaluation.events);
        enqueueOfflineControlActions(evaluation.actions);
        const auto ai_evaluation = ai_analyzer_.evaluate(coalesced, true, now);
        enqueueAiEvents(ai_evaluation.events);
    } else {
        rule_engine_.evaluate(coalesced, false, now);
        ai_analyzer_.evaluate(coalesced, false, now);
    }

    logger_.info("MQTT", "telemetry batch devices=" + std::to_string(coalesced.size()) +
                         ", ids=" + telemetryDeviceIds(coalesced));

    const std::string payload =
        codec::ThingsKitCodec::buildGatewaySubDeviceTelemetryPayload(coalesced);
    publish_queue_.push({
        codec::thingskit::kGatewaySubDeviceTelemetryTopic,
        payload,
        PublishMessageKind::Telemetry,
        0,
        0,
        {},
        {},
        {},
    });
}

std::vector<model::TelemetryData> PublishManager::coalesceTelemetryBatch(
    const std::vector<model::TelemetryData> &telemetry) const
{
    std::vector<model::TelemetryData> result;
    std::map<std::string, size_t> index_by_device;

    for (const auto &item : telemetry) {
        if (item.device_id.empty())
            continue;

        const auto it = index_by_device.find(item.device_id);
        if (it == index_by_device.end()) {
            index_by_device[item.device_id] = result.size();
            result.push_back(item);
            continue;
        }

        auto &merged = result[it->second];
        merged.ts_ms = std::max(merged.ts_ms, item.ts_ms);
        merged.type = item.type;
        for (const auto &[key, value] : item.integer_values)
            merged.integer_values[key] = value;
        for (const auto &[key, value] : item.numeric_values)
            merged.numeric_values[key] = value;
        for (const auto &[key, value] : item.string_values)
            merged.string_values[key] = value;
        for (const auto &[key, value] : item.bool_values)
            merged.bool_values[key] = value;
        for (const auto &[key, value] : item.object_values)
            merged.object_values[key] = value;
    }

    return result;
}

void PublishManager::updateDeviceOnlineStates(
    const std::vector<model::TelemetryData> &telemetry)
{
    for (const auto &item : telemetry) {
        auto state_it = device_online_states_.find(item.device_id);
        if (state_it == device_online_states_.end())
            continue;

        bool online = false;
        if (telemetryOnlineValue(item, &online))
            state_it->second = online;
    }
}

int PublishManager::onlineDeviceCount() const
{
    int count = 0;
    for (const auto &item : device_online_states_) {
        if (item.second)
            ++count;
    }
    return count;
}

bool PublishManager::offlineAnalysisActive(int64_t now_ms)
{
    if (!config_.offline_analysis.enable ||
        (!config_.offline_analysis.rule_engine.enable &&
         !config_.offline_analysis.ai.enable)) {
        offline_raw_state_ = false;
        offline_raw_since_ms_ = 0;
        offline_analysis_active_ = false;
        return false;
    }

    const auto network_state = network_worker_.state();
    const bool raw_offline =
        !network_state.available || !network_state.cloud_reachable || !cloud_client_.isConnected();

    if (raw_offline != offline_raw_state_) {
        offline_raw_state_ = raw_offline;
        offline_raw_since_ms_ = now_ms;
        logger_.info("RULE", std::string("offline gate changed raw_offline=") +
                                 (raw_offline ? "1" : "0"));
    }

    if (raw_offline) {
        if (!offline_analysis_active_ &&
            now_ms - offline_raw_since_ms_ >= config_.offline_analysis.enter_hold_ms) {
            offline_analysis_active_ = true;
            logger_.warn("RULE", "offline analysis enabled");
        }
    } else {
        if (offline_analysis_active_ &&
            now_ms - offline_raw_since_ms_ >= config_.offline_analysis.exit_hold_ms) {
            offline_analysis_active_ = false;
            logger_.info("RULE", "offline analysis disabled");
        }
    }

    return offline_analysis_active_;
}

void PublishManager::enqueueRuleEvents(const std::vector<rules::RuleEvent> &events)
{
    if (events.empty())
        return;

    for (const auto &event : events) {
        const std::string payload = codec::ThingsKitCodec::buildEventPayload(
            event.device_id, event.event, event.severity, event.message, event.details);
        publish_queue_.push({
            cloud_client_.eventsTopic(),
            payload,
            PublishMessageKind::RuleEvent,
            0,
            0,
            {},
            event.event,
            event.device_id,
        });
        logger_.warn("RULE", "rule event enqueued device=" + event.device_id +
                                 ", event=" + event.event +
                                 ", severity=" + event.severity);
    }
}

void PublishManager::enqueueAiEvents(const std::vector<ai::AiRiskEvent> &events)
{
    if (events.empty())
        return;

    for (const auto &event : events) {
        const std::string payload = codec::ThingsKitCodec::buildEventPayload(
            event.device_id, "ai_risk", event.risk_level, event.message, event.details);
        publish_queue_.push({
            cloud_client_.eventsTopic(),
            payload,
            PublishMessageKind::RuleEvent,
            0,
            0,
            {},
            "ai_risk",
            event.device_id,
        });
        logger_.warn("AI", "ai risk event enqueued device=" + event.device_id +
                           ", risk_type=" + event.risk_type +
                           ", level=" + event.risk_level);
    }
}

void PublishManager::enqueueOfflineControlActions(
    const std::vector<rules::OfflineControlAction> &actions)
{
    if (actions.empty())
        return;

    for (const auto &action : actions) {
        nlohmann::json payload = {
            {"requestId", action.request_id},
            {"device", action.target_device_id},
            {"targetDeviceId", action.target_device_id},
            {"method", action.method},
            {"params", action.params},
            {"source", "offline_rule_engine"},
            {"ruleId", action.rule_id},
            {"message", action.message},
        };

        command::RawCommandMessage message;
        message.topic = codec::thingskit::kGatewayCommandRequestTopic;
        message.payload = payload.dump();
        message.received_ts_ms = common::nowMs();
        message.local_only = true;
        command_queue_.push(std::move(message));

        logger_.warn("RULE", "offline control enqueued request_id=" + action.request_id +
                                 ", target=" + action.target_device_id +
                                 ", method=" + action.method +
                                 ", rule_id=" + action.rule_id);
    }
}

void PublishManager::flushTelemetryCache()
{
    const auto network_state = network_worker_.state();
    if (!cache_store_ || !network_state.available || !network_state.cloud_reachable)
        return;

    if (!ensureCloudConnected())
        return;

    // 缓存补传保持小批量，避免历史积压恢复时抢占实时 telemetry 和命令 response。
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
    status.device_count = onlineDeviceCount();
    status.cache_count = cache_store_ ? static_cast<int>(cache_store_->pendingCount()) : 0;
    status.ts_ms = now;

    if (network_state.available && network_state.cloud_reachable) {
        ensureCloudConnected();
        status.cloud_connected = cloud_client_.isConnected();
    }

    // 网关自身状态走 telemetry，避免平台属性 scope 差异导致页面只显示部分字段。
    const std::string payload = codec::ThingsKitCodec::buildGatewayAttributesValuesPayload(status);
    logger_.info("MQTT", "gateway status telemetry payload=" + payload);

    publish_queue_.push({
        codec::thingskit::kGatewayTelemetryTopic,
        payload,
        PublishMessageKind::GatewayStatus,
        0,
        0,
        {},
        {},
        {},
    });
}

bool PublishManager::ensureCloudConnected()
{
    const auto ns = network_worker_.state();
    if (cloud_client_.isConnected() && ns.available && !connected_ifname_.empty() &&
        connected_ifname_ != ns.ifname) {
        logger_.info("MQTT", "network interface changed " + connected_ifname_ +
                                 " -> " + ns.ifname + ", reconnecting MQTT");
        cloud_client_.disconnect();
        command_topics_subscribed_ = false;
        connected_ifname_.clear();
    }

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

    if (ns.available) {
        cloud_client_.setBindInterface(ns.ifname);
        network::InterfaceLeaseInfo lease_info;
        std::string reason;
        if (!network::reconcileInterfaceNetwork(
                ns.ifname,
                network::defaultRouteMetricForIfname(ns.ifname),
                &lease_info,
                &reason)) {
            logger_.warn("MQTT", "network alignment failed before connect ifname=" +
                                     ns.ifname + ", reason=" + reason);
            return false;
        }
    }

    if (!cloud_client_.connect())
        return false;

    connected_ifname_ = ns.available ? ns.ifname : "";

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
                             commandLogFields(message) +
                             ", topic=" + message.topic +
                             ", bytes=" + std::to_string(message.payload.size()));
    return true;
}

void PublishManager::handlePublishFailure(PublishMessage message,
                                          std::vector<PublishMessage> &delayed_messages)
{
    if (message.kind == PublishMessageKind::Telemetry ||
        message.kind == PublishMessageKind::RuleEvent) {
        // 遥测和离线规则事件可落盘补传；命令响应不能混入补传表。
        if (cache_store_)
            cache_store_->appendMessage(message.topic, message.payload);
        return;
    }

    if (message.kind == PublishMessageKind::CommandResponse) {
        if (message.retry_count >= kMaxCommandResponseRetries) {
            logger_.warn("CMD", "command response dropped topic=" + message.topic +
                                    commandLogFields(message) +
                                    ", retries=" + std::to_string(message.retry_count));
            return;
        }

        ++message.retry_count;
        message.next_retry_ts_ms =
            common::nowMs() + commandResponseRetryDelayMs(message.retry_count);
        logger_.warn("CMD", "command response retry topic=" + message.topic +
                                commandLogFields(message) +
                                ", retry=" + std::to_string(message.retry_count));
        delayed_messages.push_back(std::move(message));
        return;
    }

    logger_.warn("MQTT", std::string("publish dropped kind=") +
                             publishKindText(message.kind) +
                             ", topic=" + message.topic);
}

} // namespace gateway::app
