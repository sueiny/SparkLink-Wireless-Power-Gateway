# Embedded Gateway Development Skill

这个skill为RK3506 Linux 6.1网关项目提供专业的AI辅助开发支持。

## 文件结构

```
.opencode/skills/embedded-gateway-dev/
├── SKILL.md                    # 主skill文件
└── references/
    ├── cmake-patterns.md       # CMake构建模式
    ├── mqtt-integration.md     # MQTT集成指南
    └── cross-compile.md        # 交叉编译指南
```

## 触发条件

当编辑以下文件时自动激活：
- `.cpp` / `.h` / `.c` 源文件
- `CMakeLists.txt`
- 涉及MQTT、网络、存储等模块

当提及以下关键词时触发：
- gateway, embedded, cross-compile
- MQTT, mosquitto, SQLite
- network provider, device state
- RK3506

## 提供的能力

### 1. 代码风格指导
- 遵循gatewayd项目的命名规范
- 保持高内聚低耦合的设计原则
- 确保可读性和可维护性

### 2. 构建系统支持
- CMake交叉编译配置
- 依赖管理（mosquitto、SQLite）
- 构建脚本生成

### 3. 模块开发指导
- 网络模块（WiFi/蜂窝/以太网）
- 云连接模块（MQTT）
- 存储模块（SQLite）
- 状态管理模块

### 4. 调试和优化
- 日志系统使用
- 常见问题诊断
- 性能优化建议

## 使用方法

1. 确保opencode.json配置正确：
```json
{
  "$schema": "https://opencode.ai/config.json",
  "skills": {
    "paths": [".opencode/skills"]
  }
}
```

2. 重启opencode使配置生效

3. 开始编码时，AI会自动：
   - 遵循你的代码风格
   - 提供针对性的建议
   - 参考项目特定的模式

## 代码风格要点

### 命名规范
- 命名空间：`gateway::module`
- 类名：`PascalCase`
- 函数：`camelCase`
- 成员变量：`snake_case_`
- 常量：`kPascalCase`

### 设计原则
- 单一职责原则
- 依赖注入
- 接口抽象
- RAII资源管理
- 线程安全

### 注释规范
- 类级注释：说明职责和约束
- 方法级注释：说明功能、参数、返回值
- 行内注释：解释复杂逻辑

## 示例

### 创建新模块

```cpp
// include/example/example_manager.h
#pragma once

#include "config/config_manager.h"
#include "common/logger.h"

#include <atomic>
#include <memory>

namespace gateway::example {

// ExampleManager 负责示例功能。
// 它不直接访问网络，不操作缓存，不做JSON序列化。
class ExampleManager final {
public:
    // 构造函数只保存配置和日志引用，不执行初始化。
    explicit ExampleManager(const config::ExampleConfig &config, log::Logger &logger);
    
    // 显式初始化资源，失败返回false。
    bool init();
    
    // 执行示例操作。
    bool doSomething(const std::string &input);

    ExampleManager(const ExampleManager &) = delete;
    ExampleManager &operator=(const ExampleManager &) = delete;

private:
    config::ExampleConfig config_;
    log::Logger &logger_;
    std::atomic_bool initialized_{false};
};

} // namespace gateway::example
```

### CMake配置

```cmake
# 添加新模块源文件
add_executable(gatewayd
    # ... existing sources ...
    src/example/example_manager.cpp
)

# 如果有新依赖
find_path(EXAMPLE_INCLUDE_DIR
    NAMES example.h
    HINTS "${GATEWAYD_EXAMPLE_ROOT}/usr/include"
)
find_library(EXAMPLE_LIBRARY
    NAMES example
    HINTS "${GATEWAYD_EXAMPLE_ROOT}/usr/lib"
)
```

## 参考资源

- [CMake交叉编译模式](references/cmake-patterns.md)
- [MQTT集成指南](references/mqtt-integration.md)
- [交叉编译设置](references/cross-compile.md)

## 维护

如需修改skill内容，直接编辑`.opencode/skills/embedded-gateway-dev/`目录下的文件即可。修改后重启opencode生效。
