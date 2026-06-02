# MQTT Integration Guide (Mosquitto)

## Architecture Overview

```
┌─────────────────┐    ┌──────────────────┐    ┌─────────────────┐
│  GatewayApp     │───▶│ MqttCloudClient  │───▶│   Mosquitto     │
│  (Orchestration)│    │ (Protocol Only)  │    │   Broker        │
└─────────────────┘    └──────────────────┘    └─────────────────┘
```

**Key Principle**: `MqttCloudClient` only handles MQTT protocol. It does not:
- Generate JSON payloads
- Read device data
- Determine network type
- Manage cache

## Class Design

### Header File

```cpp
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

    // Delete copy/move for resource-owning class
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

    // Static callbacks for mosquitto C API
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
```

## Implementation Patterns

### Constructor Pattern

```cpp
// Constructor: save config only, no side effects
MqttCloudClient::MqttCloudClient(const config::ThingsKitConfig &config, 
                                  log::Logger &logger)
    : config_(config)
    , logger_(logger)
{
    // Build topic strings from config
    telemetry_topic_ = buildTopic("/telemetry");
    attributes_topic_ = buildTopic("/attributes");
    events_topic_ = buildTopic("/events");
}
```

### Connection Management

```cpp
bool MqttCloudClient::connect()
{
    // 1. Ensure mosquitto library initialized
    if (!lib_acquired_) {
        if (mosquitto_lib_init() != MOSQ_ERR_SUCCESS) {
            logger_.error("MQTT", "Failed to initialize mosquitto library");
            return false;
        }
        lib_acquired_ = true;
    }

    // 2. Create client instance
    ensureClientCreated();
    if (!client_) {
        return false;
    }

    // 3. Set credentials if configured
    if (!config_.username.empty()) {
        mosquitto_username_pw_set(client_, 
                                  config_.username.c_str(),
                                  config_.password.c_str());
    }

    // 4. Set callbacks
    mosquitto_connect_callback_set(client_, onConnect);
    mosquitto_disconnect_callback_set(client_, onDisconnect);
    mosquitto_message_callback_set(client_, onMessage);

    // 5. Connect to broker
    int rc = mosquitto_connect(client_, 
                               config_.host.c_str(),
                               config_.port,
                               config_.keepalive);
    if (rc != MOSQ_ERR_SUCCESS) {
        logger_.error("MQTT", "Connection failed: " + std::string(mosquitto_strerror(rc)));
        return false;
    }

    // 6. Start background loop
    rc = mosquitto_loop_start(client_);
    if (rc != MOSQ_ERR_SUCCESS) {
        logger_.error("MQTT", "Failed to start loop: " + std::string(mosquitto_strerror(rc)));
        return false;
    }
    loop_started_ = true;

    // 7. Wait for connection callback
    return waitForConnected(5000);
}
```

### Publish Pattern

```cpp
bool MqttCloudClient::publishTelemetry(const std::string &payload)
{
    return publishToTopic(telemetry_topic_, payload);
}

bool MqttCloudClient::publishToTopic(const std::string &topic, const std::string &payload)
{
    if (!connected_.load()) {
        logger_.warn("MQTT", "Not connected, cannot publish to: " + topic);
        return false;
    }

    int rc = mosquitto_publish(client_, 
                               nullptr,  // message ID (auto)
                               topic.c_str(),
                               static_cast<int>(payload.size()),
                               payload.c_str(),
                               0,  // QoS
                               false);  // retain

    if (rc != MOSQ_ERR_SUCCESS) {
        logger_.error("MQTT", "Publish failed to " + topic + ": " + 
                     std::string(mosquitto_strerror(rc)));
        return false;
    }

    return true;
}
```

### Callback Pattern

```cpp
// Static callback: extract userdata and delegate to instance method
void MqttCloudClient::onConnect(struct mosquitto *client, void *userdata, int rc)
{
    (void)client;
    auto *self = static_cast<MqttCloudClient *>(userdata);
    
    if (rc == 0) {
        self->connected_.store(true);
        self->logger_.info("MQTT", "Connected to broker");
    } else {
        self->connected_.store(false);
        self->logger_.error("MQTT", "Connection failed with code: " + std::to_string(rc));
    }
}

void MqttCloudClient::onMessage(struct mosquitto *client,
                                 void *userdata,
                                 const ::mosquitto_message *message)
{
    (void)client;
    auto *self = static_cast<MqttCloudClient *>(userdata);

    if (self->message_callback_ && message->payload) {
        std::string topic(message->topic);
        std::string payload(static_cast<char *>(message->payload), 
                           static_cast<size_t>(message->payloadlen));
        
        // Callback runs in mosquitto thread - GatewayApp should queue only
        self->message_callback_(topic, payload);
    }
}
```

### Cleanup Pattern

```cpp
void MqttCloudClient::disconnect()
{
    if (client_) {
        if (loop_started_) {
            mosquitto_loop_stop(client_, true);  // Force stop
            loop_started_ = false;
        }
        mosquitto_disconnect(client_);
    }
    connected_.store(false);
}

void MqttCloudClient::destroyClient()
{
    if (client_) {
        mosquitto_destroy(client_);
        client_ = nullptr;
    }
    if (lib_acquired_) {
        mosquitto_lib_cleanup();
        lib_acquired_ = false;
    }
}

MqttCloudClient::~MqttCloudClient()
{
    disconnect();
    destroyClient();
}
```

## Thread Safety

### Callback Safety

```cpp
// Callbacks run in mosquitto's network thread
// DO NOT: perform heavy processing, block, or call complex logic
// DO: queue messages for processing in application thread

void MqttCloudClient::onMessage(struct mosquitto *client,
                                 void *userdata,
                                 const ::mosquitto_message *message)
{
    auto *self = static_cast<MqttCloudClient *>(userdata);
    
    // Just queue the message - GatewayApp handles processing
    if (self->message_callback_) {
        self->message_callback_(topic, payload);
    }
}
```

### Atomic State

```cpp
// Use atomic for connection state accessed from multiple threads
std::atomic_bool connected_{false};

// Check before publish
if (!connected_.load()) {
    return false;
}
```

## Error Handling

### Connection Errors

```cpp
bool MqttCloudClient::waitForConnected(int timeout_ms) const
{
    auto start = std::chrono::steady_clock::now();
    while (!connected_.load()) {
        auto elapsed = std::chrono::steady_clock::now() - start;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() > timeout_ms) {
            logger_.error("MQTT", "Connection timeout");
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return true;
}
```

### Publish Errors

```cpp
// Log error details for debugging
if (rc != MOSQ_ERR_SUCCESS) {
    logger_.error("MQTT", "Publish failed: " + std::string(mosquitto_strerror(rc)));
    return false;
}
```

## Configuration Integration

### Topic Building

```cpp
std::string MqttCloudClient::buildTopic(const std::string &suffix) const
{
    // Example: "devices/gateway_001/telemetry"
    return config_.topic_prefix + "/" + config_.client_id + suffix;
}
```

### Reconnection Strategy

```cpp
// In GatewayApp: handle disconnect callback
void GatewayApp::handleDisconnect(int rc)
{
    if (rc != 0) {
        // Unexpected disconnect - attempt reconnect
        logger_.warn("MQTT", "Unexpected disconnect, attempting reconnect");
        scheduleReconnect();
    }
}
```

## Common Pitfalls

### ❌ Processing in Callback Thread

```cpp
// BAD: Heavy processing in mosquitto thread
void MqttCloudClient::onMessage(...) {
    auto data = parseJSON(payload);      // CPU intensive
    updateDatabase(data);                 // I/O blocking
    notifyAllClients(data);              // Network I/O
}
```

### ✅ Queue for Application Thread

```cpp
// GOOD: Queue message for application thread
void MqttCloudClient::onMessage(...) {
    if (message_callback_) {
        message_callback_(topic, payload);  // Just enqueue
    }
}
```

### ❌ Forgetting Cleanup

```cpp
// BAD: Resource leak
~MqttCloudClient() {
    // Missing mosquitto_destroy and lib_cleanup
}
```

### ✅ Proper RAII

```cpp
// GOOD: Clean destruction
~MqttCloudClient() {
    disconnect();
    destroyClient();  // Calls mosquitto_destroy and lib_cleanup
}
```
