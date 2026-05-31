#include "cloud/mqtt_cloud_client.h"

#include <chrono>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>

extern "C" {
// 当前 RK3506 SDK 内的 mosquitto.h 没有暴露订阅和消息回调相关声明，
// 但 libmosquitto 实际提供这些符号；保留最小声明以兼容板端工具链。
struct mosquitto_message {
    int mid;
    char *topic;
    void *payload;
    int payloadlen;
    int qos;
    bool retain;
};

int mosquitto_subscribe(struct mosquitto *mosq, int *mid, const char *sub, int qos);
void mosquitto_message_callback_set(
    struct mosquitto *mosq,
    void (*on_message)(struct mosquitto *, void *, const struct mosquitto_message *));
}

namespace gateway::cloud {

namespace {

constexpr int kQos = 1;
constexpr bool kRetain = false;

// libmosquitto 的 init/cleanup 是进程级资源管理。
// gatewayd 当前只保留一个网关 MQTT client，但这里仍做引用计数，
// 防止后续增加测试 client 或备用云通道时出现“一个对象析构清理了全局库”的隐蔽问题。
std::mutex g_mosquitto_lib_mutex;
int g_mosquitto_lib_ref_count = 0;

const char *mosquittoErrorText(int rc)
{
    return mosquitto_strerror(rc);
}

void acquireMosquittoLib()
{
    std::lock_guard<std::mutex> lock(g_mosquitto_lib_mutex);
    if (g_mosquitto_lib_ref_count == 0)
        mosquitto_lib_init();
    ++g_mosquitto_lib_ref_count;
}

void releaseMosquittoLib()
{
    std::lock_guard<std::mutex> lock(g_mosquitto_lib_mutex);
    if (g_mosquitto_lib_ref_count <= 0)
        return;

    --g_mosquitto_lib_ref_count;
    if (g_mosquitto_lib_ref_count == 0)
        mosquitto_lib_cleanup();
}

} // namespace

MqttCloudClient::MqttCloudClient(const config::ThingsKitConfig &config, log::Logger &logger)
    : config_(config),
      logger_(logger),
      telemetry_topic_(buildTopic("telemetry")),
      attributes_topic_(buildTopic("attributes")),
      events_topic_(buildTopic("events"))
{
}

MqttCloudClient::~MqttCloudClient()
{
    // MqttCloudClient 持有 libmosquitto 的 client 资源。
    // 析构时统一停止 loop、断开连接并释放资源，避免进程退出时留下后台线程。
    disconnect();
    destroyClient();
}

bool MqttCloudClient::connect()
{
    if (isConnected())
        return true;

    ensureClientCreated();
    if (!client_)
        return false;

    // mosquitto_connect 只发起连接，真正的 CONNACK 结果在回调里到达。
    // 因此后面还需要 waitForConnected，避免 connect 返回成功但认证实际失败。
    logger_.info("MQTT", "connecting to " + config_.host + ":" + std::to_string(config_.port));

    const int rc = mosquitto_connect(
        client_,
        config_.host.c_str(),
        config_.port,
        config_.keepalive);
    if (rc != MOSQ_ERR_SUCCESS) {
        logger_.error("MQTT", std::string("connect failed: ") + mosquittoErrorText(rc));
        connected_.store(false);
        return false;
    }

    if (!loop_started_) {
        const int loop_rc = mosquitto_loop_start(client_);
        if (loop_rc != MOSQ_ERR_SUCCESS) {
            logger_.error("MQTT", std::string("loop_start failed: ") + mosquittoErrorText(loop_rc));
            connected_.store(false);
            return false;
        }
        loop_started_ = true;
    }

    if (!waitForConnected(5000)) {
        logger_.warn("MQTT", "connect timeout waiting for callback");
        return false;
    }

    return true;
}

bool MqttCloudClient::isConnected() const
{
    return connected_.load();
}

void MqttCloudClient::disconnect()
{
    if (!client_)
        return;

    if (connected_.exchange(false))
        logger_.info("MQTT", "disconnecting");

    if (loop_started_) {
        mosquitto_loop_stop(client_, true);
        loop_started_ = false;
    }

    mosquitto_disconnect(client_);
}

bool MqttCloudClient::publishTelemetry(const std::string &payload)
{
    return publishToTopic(telemetry_topic_, payload);
}

bool MqttCloudClient::publishAttributes(const std::string &payload)
{
    return publishToTopic(attributes_topic_, payload);
}

bool MqttCloudClient::publishEvent(const std::string &payload)
{
    return publishToTopic(events_topic_, payload);
}

bool MqttCloudClient::publishRaw(const std::string &topic, const std::string &payload)
{
    return publishToTopic(topic, payload);
}

bool MqttCloudClient::subscribeRaw(const std::string &topic_filter)
{
    if (!client_ || !isConnected()) {
        logger_.warn("MQTT", "subscribe skipped, MQTT is not connected: " + topic_filter);
        return false;
    }

    const int rc = mosquitto_subscribe(client_, nullptr, topic_filter.c_str(), kQos);
    if (rc != MOSQ_ERR_SUCCESS) {
        logger_.warn("MQTT", "subscribe failed topic=" + topic_filter +
                                 ", error=" + mosquittoErrorText(rc));
        return false;
    }

    logger_.info("MQTT", "subscribe success topic=" + topic_filter);
    return true;
}

void MqttCloudClient::setMessageCallback(MessageCallback callback)
{
    message_callback_ = std::move(callback);
}

bool MqttCloudClient::publishToTopic(const std::string &topic, const std::string &payload)
{
    // 本函数只负责把已经构造好的 JSON 字符串发布到指定 Topic。
    // 它不解析 payload，也不做缓存；失败后由 GatewayApp/CacheStore 决定处理方式。
    if (!client_ || !isConnected()) {
        logger_.warn("MQTT", "publish skipped, MQTT is not connected: " + topic);
        return false;
    }

    const int rc = mosquitto_publish(
        client_,
        nullptr,
        topic.c_str(),
        static_cast<int>(payload.size()),
        payload.data(),
        kQos,
        kRetain);
    if (rc != MOSQ_ERR_SUCCESS) {
        logger_.warn("MQTT", "publish failed topic=" + topic +
                                 ", error=" + mosquittoErrorText(rc));
        return false;
    }

    return true;
}

std::string MqttCloudClient::buildTopic(const std::string &suffix) const
{
    const std::string prefix = config_.topic_prefix.empty() ? "devices" : config_.topic_prefix;

    // ThingsKit 标准 Topic 不包含 client_id：
    //   v1/devices/me/attributes
    //   v1/devices/me/telemetry
    //   v1/gateway/telemetry
    // 老版本 gatewayd 的 devices/{id}/telemetry 格式保留兼容。
    if (prefix == "v1/gateway" || prefix == "v1/devices/me")
        return prefix + "/" + suffix;
    return prefix + "/" + config_.client_id + "/" + suffix;
}

void MqttCloudClient::ensureClientCreated()
{
    if (client_)
        return;

    if (!lib_acquired_) {
        // libmosquitto 的全局初始化/清理必须做引用计数。
        // 这样即使后续增加多个 MQTT client，也不会互相破坏全局库状态。
        acquireMosquittoLib();
        lib_acquired_ = true;
    }

    client_ = mosquitto_new(config_.client_id.c_str(), true, this);
    if (!client_) {
        logger_.error("MQTT", "mosquitto_new failed");
        return;
    }

    mosquitto_connect_callback_set(client_, &MqttCloudClient::onConnect);
    mosquitto_disconnect_callback_set(client_, &MqttCloudClient::onDisconnect);
    mosquitto_message_callback_set(client_, &MqttCloudClient::onMessage);

    if (!config_.username.empty() || !config_.password.empty()) {
        const int rc = mosquitto_username_pw_set(
            client_,
            config_.username.empty() ? nullptr : config_.username.c_str(),
            config_.password.empty() ? nullptr : config_.password.c_str());
        if (rc != MOSQ_ERR_SUCCESS)
            logger_.warn("MQTT", std::string("username_pw_set failed: ") + mosquittoErrorText(rc));
    }
}

void MqttCloudClient::destroyClient()
{
    if (client_) {
        mosquitto_destroy(client_);
        client_ = nullptr;
    }

    if (lib_acquired_) {
        releaseMosquittoLib();
        lib_acquired_ = false;
    }
}

bool MqttCloudClient::waitForConnected(int timeout_ms) const
{
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms);

    while (std::chrono::steady_clock::now() < deadline) {
        if (connected_.load())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    return connected_.load();
}

void MqttCloudClient::onConnect(struct mosquitto *, void *userdata, int rc)
{
    // libmosquitto 在网络线程中触发回调。
    // 这里仅更新连接状态并写日志，不做发布、缓存等复杂工作。
    auto *self = static_cast<MqttCloudClient *>(userdata);
    if (!self)
        return;

    if (rc == 0) {
        self->connected_.store(true);
        self->logger_.info("MQTT", "connected");
    } else {
        self->connected_.store(false);
        std::ostringstream ss;
        ss << "connect callback failed, rc=" << rc;
        self->logger_.warn("MQTT", ss.str());
    }
}

void MqttCloudClient::onDisconnect(struct mosquitto *, void *userdata, int rc)
{
    // 断开回调只维护连接状态。
    // 是否重连由 GatewayApp 在后续完整主循环中统一编排。
    auto *self = static_cast<MqttCloudClient *>(userdata);
    if (!self)
        return;

    self->connected_.store(false);
    std::ostringstream ss;
    ss << "disconnected, rc=" << rc;
    self->logger_.warn("MQTT", ss.str());
}

void MqttCloudClient::onMessage(struct mosquitto *,
                                void *userdata,
                                const ::mosquitto_message *message)
{
    auto *self = static_cast<MqttCloudClient *>(userdata);
    if (!self || !message || !message->topic || !message->payload)
        return;

    const std::string topic = message->topic;
    const std::string payload(static_cast<const char *>(message->payload),
                              static_cast<size_t>(message->payloadlen));
    self->logger_.info("MQTT", "message received topic=" + topic +
                                   ", bytes=" + std::to_string(payload.size()));

    if (self->message_callback_)
        self->message_callback_(topic, payload);
}

} // namespace gateway::cloud
