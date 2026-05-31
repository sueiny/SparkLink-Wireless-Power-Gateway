#pragma once

#include "config/config_manager.h"
#include "common/logger.h"

#include <atomic>
#include <functional>
#include <mosquitto.h>
#include <string>

struct mosquitto_message;

namespace gateway::cloud {

// MqttCloudClient 只负责 ThingsKit MQTT 通道。
// 它不生成 ThingsKit JSON，不读取设备数据，不判断网络类型，也不操作缓存。
class MqttCloudClient final {
public:
    using MessageCallback = std::function<void(const std::string &topic,
                                               const std::string &payload)>;

    // 构造函数只保存 MQTT 配置和日志引用，不立即联网。
    // 真实连接由 connect() 显式触发，便于 GatewayApp 控制重连时机。
    MqttCloudClient(const config::ThingsKitConfig &config, log::Logger &logger);
    ~MqttCloudClient();

    MqttCloudClient(const MqttCloudClient &) = delete;
    MqttCloudClient &operator=(const MqttCloudClient &) = delete;

    // 建立 MQTT 连接并启动 libmosquitto 后台 loop。
    // 成功返回 true；认证失败、TCP 失败或等待连接回调超时都会返回 false。
    bool connect();

    // 返回最近一次 MQTT 连接回调维护的连接状态。
    bool isConnected() const;

    // 主动断开连接并停止后台 loop。允许重复调用。
    void disconnect();

    // 发布到配置生成的 telemetry Topic。
    bool publishTelemetry(const std::string &payload);

    // 发布到配置生成的 attributes Topic。
    bool publishAttributes(const std::string &payload);

    // 发布到配置生成的 events Topic。
    bool publishEvent(const std::string &payload);

    // 发布到配置以外的明确 Topic。
    // 该函数仍然只负责 MQTT 发送，不理解 payload 含义；用于同一个网关连接
    // 同时发布网关自身属性和网关子设备遥测，避免创建多个相同 client_id 的连接。
    bool publishRaw(const std::string &topic, const std::string &payload);

    // 订阅原始 topic filter。命令 topic 的具体常量在 thingskit_topics.h 中集中维护。
    bool subscribeRaw(const std::string &topic_filter);

    // 回调在 libmosquitto 线程里触发；GatewayApp 里只入队，不做解析和发布。
    void setMessageCallback(MessageCallback callback);

private:
    bool publishToTopic(const std::string &topic, const std::string &payload);
    std::string buildTopic(const std::string &suffix) const;
    void ensureClientCreated();
    void destroyClient();
    bool waitForConnected(int timeout_ms) const;

    static void onConnect(struct mosquitto *client, void *userdata, int rc);
    static void onDisconnect(struct mosquitto *client, void *userdata, int rc);
    static void onMessage(struct mosquitto *client,
                          void *userdata,
                          const ::mosquitto_message *message);

    config::ThingsKitConfig config_;
    log::Logger &logger_;
    struct mosquitto *client_ = nullptr;
    std::atomic_bool connected_{false};
    bool loop_started_ = false;
    bool lib_acquired_ = false;

    std::string telemetry_topic_;
    std::string attributes_topic_;
    std::string events_topic_;
    MessageCallback message_callback_;
};

} // namespace gateway::cloud
