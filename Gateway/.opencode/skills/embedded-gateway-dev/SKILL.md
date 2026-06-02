---
name: embedded-gateway-dev
description: Use when editing C/C++ source files, CMakeLists.txt, or working with embedded Linux gateway development. Triggers on keywords like gateway, embedded, cross-compile, MQTT, mosquitto, SQLite, network provider, device state, or RK3506.
---

# Embedded Linux Gateway Development Skill

## Project Context

This skill is optimized for the **RK3506 Linux 6.1 Gateway** project (`gatewayd`), a C++17 embedded gateway application using CMake build system with MQTT (mosquitto) and SQLite dependencies.

## Code Style Standards

### Naming Conventions

```cpp
// Namespace: lowercase with gateway:: prefix
namespace gateway::cloud { }
namespace gateway::network { }

// Class: PascalCase
class MqttCloudClient final { };
class NetManager { };

// Function/Method: camelCase
bool init();
void publishTelemetry(const std::string &payload);

// Member variable: snake_case_ with trailing underscore
std::string config_path_;
log::Logger &logger_;
std::atomic_bool connected_{false};

// Constant: kPascalCase with k prefix
constexpr long kMaxLogBytes = 5L * 1024L * 1024L;
constexpr int kMaxRotateFiles = 3;

// Local variable: snake_case
std::string normalized = text;
int index = kMaxRotateFiles - 1;
```

### File Organization

```
include/
├── module_name/
│   └── file_name.h        # Header with declarations
src/
├── module_name/
│   └── file_name.cpp      # Implementation
```

### Header File Structure

```cpp
#pragma once

// 1. Corresponding header (for .cpp files)
#include "module_name/file_name.h"

// 2. Project headers
#include "common/logger.h"
#include "config/config_manager.h"

// 3. Third-party libraries
#include <mosquitto.h>
#include <json.hpp>

// 4. Standard library
#include <string>
#include <memory>
#include <atomic>
```

### Include Order Rules

1. **Self header first** - `.cpp` file includes its own `.h` first
2. **Project headers** - Other project headers in alphabetical order
3. **Third-party libraries** - External dependencies (mosquitto, json, sqlite)
4. **Standard library** - C++ standard headers in alphabetical order

## Architecture Principles

### High Cohesion, Low Coupling

```cpp
// GOOD: Single responsibility
// MqttCloudClient only handles MQTT protocol, not JSON generation
class MqttCloudClient final {
public:
    bool publishTelemetry(const std::string &payload);  // Takes pre-formed JSON
    bool publishAttributes(const std::string &payload); // Doesn't know content
};

// GOOD: Dependency injection via constructor
GatewayApp(std::string config_path);

// GOOD: Interface abstraction for network providers
class INetworkProvider {
public:
    virtual ~INetworkProvider() = default;
    virtual NetworkState check(bool allow_bring_up, bool *cloud_reachable) = 0;
};
```

### Class Design Pattern

```cpp
// Header file: minimal public interface, clear documentation
class ExampleManager {
public:
    // Constructor only saves config, no side effects
    explicit ExampleManager(const config::ExampleConfig &config, log::Logger &logger);
    
    // Explicit init() for resources that may fail
    bool init();
    
    // Clear single-purpose methods
    bool doSomething(const std::string &input);
    
    // Delete copy/move for resource-owning classes
    ExampleManager(const ExampleManager &) = delete;
    ExampleManager &operator=(const ExampleManager &) = delete;

private:
    // Implementation details hidden
    bool internalHelper();
    
    // Members with trailing underscore
    config::ExampleConfig config_;
    log::Logger &logger_;
    std::atomic_bool initialized_{false};
};
```

### Error Handling Pattern

```cpp
// Return bool for success/failure, error via output parameter
bool ConfigManager::load(const std::string &path, std::string *error) {
    if (!fileExists(path)) {
        if (error) *error = "config file not found: " + path;
        return false;
    }
    // ... implementation
    return true;
}

// Or return bool directly for simple cases
bool MqttCloudClient::connect() {
    if (!ensureClientCreated()) {
        return false;
    }
    // ... implementation
    return true;
}
```

### Thread Safety Pattern

```cpp
class ThreadSafeComponent {
public:
    void safeMethod() {
        std::lock_guard<std::mutex> lock(mutex_);
        // Critical section
    }
    
    void atomicOperation() {
        flag_.store(true);
    }

private:
    mutable std::mutex mutex_;
    std::atomic_bool flag_{false};
};
```

### Resource Management

```cpp
// Use smart pointers for heap-allocated resources
std::unique_ptr<datasource::MockDataSource> data_source_;
std::shared_ptr<state::DeviceStateStore> state_store_;

// Use RAII for locks
void write(Level level, const std::string &module, const std::string &message) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Automatically released when scope exits
}
```

## CMake Best Practices

### Cross-Compilation Support

```cmake
# Support custom sysroot for dependencies
set(GATEWAYD_MOSQUITTO_ROOT "" CACHE PATH "Optional mosquitto sysroot")
if(GATEWAYD_MOSQUITTO_ROOT)
    list(APPEND CMAKE_PREFIX_PATH "${GATEWAYD_MOSQUITTO_ROOT}")
endif()

# Use find_path and find_library for flexibility
find_path(MOSQUITTO_INCLUDE_DIR
    NAMES mosquitto.h
    HINTS "${GATEWAYD_MOSQUITTO_ROOT}/usr/include"
)
find_library(MOSQUITTO_LIBRARY
    NAMES mosquitto
    HINTS "${GATEWAYD_MOSQUITTO_ROOT}/usr/lib"
)
```

### Build Configuration

```cmake
# C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Warning flags
target_compile_options(target PRIVATE
    -Wall
    -Wextra
    -Wno-psabi  # Suppress ABI warnings for ARM
)

# Linker options for embedded
target_link_options(target PRIVATE
    -Wl,-rpath,/userdata/gateway/lib
    -Wl,--allow-shlib-undefined
)
```

## Module-Specific Guidelines

### Network Module (`network/`)

```cpp
// Interface-based design for network providers
class INetworkProvider {
public:
    virtual ~INetworkProvider() = default;
    virtual NetworkState check(bool allow_bring_up, bool *cloud_reachable) = 0;
};

// Concrete implementations
class EthernetProvider : public INetworkProvider { };
class WifiProvider : public INetworkProvider { };
class CellularProvider : public INetworkProvider { };

// Manager selects provider based on priority
class NetManager {
    std::vector<std::unique_ptr<INetworkProvider>> providers_;
};
```

### Cloud Module (`cloud/`)

```cpp
// MQTT client: protocol only, no business logic
class MqttCloudClient final {
    // Callback for incoming messages (runs in mosquitto thread)
    using MessageCallback = std::function<void(const std::string &topic,
                                               const std::string &payload)>;
    void setMessageCallback(MessageCallback callback);
};
```

### Storage Module (`storage/`)

```cpp
// Cache with TTL support
class CacheStore {
    bool put(const std::string &key, const std::string &value, int64_t ttl_ms);
    bool get(const std::string &key, std::string *value);
    bool remove(const std::string &key);
};
```

### State Module (`state/`)

```cpp
// Thread-safe device state
class DeviceStateStore {
    void update(const std::string &device_id, const DeviceState &state);
    DeviceState get(const std::string &device_id) const;
};
```

## Comment Standards

### File-Level Comments

```cpp
// MqttCloudClient 只负责 ThingsKit MQTT 通道。
// 它不生成 ThingsKit JSON，不读取设备数据，不判断网络类型，也不操作缓存。
class MqttCloudClient final {
```

### Method-Level Comments

```cpp
// 建立 MQTT 连接并启动 libmosquitto 后台 loop。
// 成功返回 true；认证失败、TCP 失败或等待连接回调超时都会返回 false。
bool connect();

// 构造函数只保存 MQTT 配置和日志引用，不立即联网。
// 真实连接由 connect() 显式触发，便于 GatewayApp 控制重连时机。
MqttCloudClient(const config::ThingsKitConfig &config, log::Logger &logger);
```

### Inline Comments

```cpp
// --mqtt-test 是独立诊断入口，只验证云连接和基础发布，不启动采集/命令/网络 worker。
if (options.mqtt_test)
    return runMqttTest(options.config_path);

// MQTT 测试模式只验证云通道：连接 Broker，并发布一条固定 telemetry JSON。
// 不启动 GatewayApp 主循环，也不采集模拟数据、不做缓存。
```

## Anti-Patterns to Avoid

### ❌ God Class
```cpp
// BAD: Class does everything
class GatewayManager {
    void initMQTT();
    void initNetwork();
    void initData();
    void processCommands();
    void generateJSON();
    void cacheData();
    // ... 20 more methods
};
```

### ❌ Constructor Side Effects
```cpp
// BAD: Constructor does I/O
MqttCloudClient::MqttCloudClient(const Config &config) {
    connect();  // May fail, but can't return error
    loadState();  // Side effect in constructor
}
```

### ❌ Raw Pointers for Ownership
```cpp
// BAD: Manual memory management
class Manager {
    DataSource *source_;  // Who owns this? Who deletes?
};
```

### ❌ Mixed Concerns
```cpp
// BAD: MQTT client generates JSON
bool MqttCloudClient::publishTelemetry(const DeviceData &data) {
    json j;
    j["voltage"] = data.voltage;  // Business logic in protocol class
    return publish(j.dump());
}
```

## Debugging Tips

### Log Module Usage

```cpp
// Use module name for filtering
logger.info("MQTT", "Connected to broker");
logger.error("NETWORK", "Failed to bring up WiFi");
logger.debug("CACHE", "Cache hit for key: " + key);
```

### Common Issues

1. **MQTT Connection Failures**: Check `cloud/mqtt_cloud_client.cpp` connection logic
2. **Network Interface Issues**: Verify `network/net_manager.cpp` provider selection
3. **Cache Misses**: Check `storage/cache_store.cpp` TTL configuration
4. **Thread Safety**: Ensure proper mutex usage in shared state

## References

- [CMake Cross-Compilation](references/cmake-patterns.md)
- [MQTT Integration Guide](references/mqtt-integration.md)
- [Cross-Compilation Setup](references/cross-compile.md)
