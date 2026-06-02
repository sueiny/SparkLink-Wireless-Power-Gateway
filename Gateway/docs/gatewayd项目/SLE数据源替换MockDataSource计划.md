# 计划：SLE 数据源替换 MockDataSource — Modbus + Mesh 模拟数据通过 IPC 接入 gatewayd

## 背景

**当前状态**：
- `sle_data_app`（C 进程）：通过 SLE 协议连接最多 8 个 DTU，接收 notify/indication 数据，但仅输出到日志文件 `/tmp/sle_app.log`，无任何 IPC 通道。
- `gatewayd`（C++17 进程）：使用 `MockDataSource` 生成模拟遥测数据，通过 `BlockingQueue` 送入发布流水线，最终 MQTT 上云。
- 两者完全独立运行，无数据互通。

**目标**：将 `sle_data_app` 改造为 `sle-daemon`，通过 Unix Socket 将真实 SLE 数据（含 Modbus + Mesh 信息）发送给 `gatewayd`，替换 `MockDataSource`。

**已有的架构规划**：`docs/gatewayd项目/网关后续架构规划.md` 已定义了 Unix Socket IPC 协议（`SleRawFrame` / `SleCommandFrame`）、通道路径和线程模型。

---

## 方案概述

采用 **Unix Domain Socket** IPC（非共享内存），原因：
1. 架构规划文档已明确选用 Unix Socket，数据量 96KB/s 完全在 Socket 能力范围内
2. Socket 天然支持进程隔离，sle-daemon 崩溃不影响 gatewayd
3. 双向通信（数据上行 + 命令下行）用 Socket 实现最自然
4. 无需处理共享内存的同步、环形缓冲区等复杂问题

---

## 实施步骤

### 阶段 1：定义 IPC 协议头文件（共用）

**新建文件**：`app/Gateway/common/ipc_protocol.h`

```cpp
// sle-daemon 和 gatewayd 共用的 IPC 帧定义
// 两个进程各自 include 此头文件

#pragma once
#include <cstdint>

#define IPC_DATA_SOCK_PATH   "/var/run/gateway/sle_data.sock"
#define IPC_CMD_SOCK_PATH    "/var/run/gateway/sle_cmd.sock"
#define IPC_MAGIC             0x534C4500  // "SLE\0"
#define IPC_VERSION           1

// 帧头
struct IpcFrameHeader {
    uint32_t magic;         // IPC_MAGIC
    uint8_t  version;       // IPC_VERSION
    uint8_t  frame_type;    // 0=data, 1=command
    uint16_t payload_len;   // 后续 payload 长度
};

// 数据帧 payload（sle-daemon -> gatewayd）
struct SleRawFrame {
    uint8_t  server_index;       // SLE 连接槽位 (0-7)
    uint8_t  mac[6];             // DTU MAC 地址
    uint16_t conn_id;            // SDK 连接 ID
    uint32_t rx_count;           // 接收计数
    uint8_t  mesh_hop_count;     // 跳数（从数据帧解析）
    uint8_t  mesh_ttl;           // TTL
    uint16_t data_len;           // Modbus 原始数据长度
    uint8_t  data[512];          // Modbus 原始数据
    int64_t  timestamp_ms;       // 时间戳
};

// 命令帧 payload（gatewayd -> sle-daemon）
struct SleCommandFrame {
    uint8_t  target_mac[6];      // 目标 DTU MAC
    uint16_t data_len;           // Modbus 请求长度
    uint8_t  data[256];          // Modbus 请求数据
};
```

### 阶段 2：改造 sle_data_app -> sle-daemon

#### 2.1 新增 IPC 发送模块

**新建文件**：`app/Gateway/sle_data_app/src/ipc_sender.c` + `inc/ipc_sender.h`

职责：
- 启动时创建 `/var/run/gateway/` 目录，连接 Unix Socket（stream 类型）
- 提供 `ipc_sender_send_frame(server_index, conn, data, len)` 接口
- 在 `notify_log_worker` 的消费循环中，将 `notify_packet_t` 封装为 `SleRawFrame` 并通过 Socket 发送
- 自动重连机制：连接断开时每 3 秒重试
- 线程安全：使用独立的发送 mutex

#### 2.2 修改 notify_printer.c

**修改文件**：`app/Gateway/sle_data_app/src/notify_printer.c`

在 `notify_log_worker()` 的消费循环中，在写日志之后增加 IPC 发送调用：

```c
// 现有：print_packet_to_stream(stderr, &packet);
// 现有：print_packet_to_stream(g_notify_printer.log_fp, &packet);
// 新增：
ipc_sender_send_packet(&packet);  // 封装为 SleRawFrame 发送
```

#### 2.3 新增命令接收模块（可后续补充）

**新建文件**：`app/Gateway/sle_data_app/src/ipc_cmd_receiver.c` + `inc/ipc_cmd_receiver.h`

职责：
- 监听 `/var/run/gateway/sle_cmd.sock`
- 接收 `SleCommandFrame`，转换为 SLE write 请求
- 通过 `ssapc_write_req()` 发送到目标 DTU

#### 2.4 修改 Makefile

增加 `ipc_sender.c`、`ipc_cmd_receiver.c` 的编译目标，链接时增加 socket 相关依赖（已是 `-lrt -lpthread`，无需额外库）。

#### 2.5 修改 main.c

在 `sle_manager_init()` 之后调用 `ipc_sender_init()`，在 shutdown 时调用 `ipc_sender_deinit()`。

### 阶段 3：gatewayd 新增 SleDataSource

#### 3.1 新增 IPC 接收模块

**新建文件**：`gatewayd/include/network/ipc_receiver.h` + `gatewayd/src/network/ipc_receiver.cpp`

职责：
- 创建并监听 Unix Socket `/var/run/gateway/sle_data.sock`
- accept 连接后循环接收 `IpcFrameHeader` + `SleRawFrame`
- 校验 magic 和 version
- 提供 `receiveFrame()` 接口，返回 `SleRawFrame`

#### 3.2 新增 Mesh 解析器

**新建文件**：`gatewayd/include/codec/mesh_parser.h` + `gatewayd/src/codec/mesh_parser.cpp`

职责：
- 从 `SleRawFrame.mac` 映射到 `device_id`（查配置表）
- 从 `SleRawFrame.data` 解析 Modbus RTU 响应帧：
  - 校验 CRC16
  - 提取功能码（0x03 读保持寄存器 / 0x04 读输入寄存器）
  - 根据 `modbus_type` 解码寄存器值为工程量（电压、电流、功率等）
- 输出结构化的 `TelemetryData`

#### 3.3 新增 SleDataSource 类

**新建文件**：`gatewayd/include/datasource/sle_data_source.h` + `gatewayd/src/datasource/sle_data_source.cpp`

接口设计（与 MockDataSource 同构）：

```cpp
namespace gateway::datasource {

class SleDataSource {
public:
    SleDataSource(std::vector<model::DeviceInfo> devices,
                  config::SleConfig sle_config,
                  std::shared_ptr<state::DeviceStateStore> state_store);

    bool init();
    // 阻塞等待一帧数据，超时返回空；CollectWorker 调用
    std::vector<model::TelemetryData> collect();

    // 发送命令到 sle-daemon
    bool sendCommand(const SleCommandFrame &cmd);

private:
    std::unique_ptr<network::IpcReceiver> receiver_;
    codec::MeshParser mesh_parser_;
    // ...
};

} // namespace gateway::datasource
```

#### 3.4 新增 SleIpcWorker（替代 CollectWorker）

**新建文件**：`gatewayd/include/app/sle_ipc_worker.h` + `gatewayd/src/app/sle_ipc_worker.cpp`

职责：
- 从 `IpcReceiver` 循环接收 `SleRawFrame`
- 调用 `MeshParser` 解析为 `TelemetryData`
- 放入 `telemetry_queue_`
- 可选：批量收集（5 秒窗口内的帧打包为一批），与现有 PublishManager 的批量消费模式兼容

#### 3.5 修改 GatewayApp

**修改文件**：`gatewayd/include/app/gateway_app.h` + `gatewayd/src/app/gateway_app.cpp`

- 在 `sle.enable=true` 时使用 `SleDataSource` + `SleIpcWorker` 替代 `MockDataSource` + `CollectWorker`
- 在 `sle.enable=false` 时保持现有 MockDataSource 不变（向后兼容）
- 命令下发路径：`CommandManager` 成功执行命令后，调用 `SleDataSource::sendCommand()` 发送到 sle-daemon

#### 3.6 修改配置系统

**修改文件**：`gatewayd/include/config/config_manager.h` + `gatewayd/src/config/config_manager.cpp`

在 `gateway_config.json` 中增加：

```json
{
  "sle": {
    "enable": true,
    "data_socket": "/var/run/gateway/sle_data.sock",
    "cmd_socket": "/var/run/gateway/sle_cmd.sock"
  }
}
```

### 阶段 4：Modbus 寄存器解析

**新建文件**：`gatewayd/include/codec/modbus_parser.h` + `gatewayd/src/codec/modbus_parser.cpp`

根据 `modbus_type` 解析 Modbus RTU 响应：

| modbus_type | 设备类型 | 寄存器映射 |
|-------------|---------|-----------|
| 1 (三相表) | ThreePhaseMeter | 电压×3、电流×3、功率、电量 |
| 2 (单相表) | SinglePhaseMeter | 电压、电流、有功功率、功率因数、频率、电量 |
| 3 (温湿度) | EnvSensor | 温度、湿度 |
| 4 (继电器) | Relay | 继电器状态、控制模式 |

解析流程：
1. 校验 CRC16（Modbus RTU 帧尾 2 字节）
2. 提取功能码和字节数
3. 按设备类型查表，将寄存器原始值转为工程量（如 `raw/100.0` 得到电压）
4. 填充 `TelemetryData` 的 `numeric_values` / `integer_values`

### 阶段 5：集成测试

| 测试项 | 验证方法 | 预期 |
|--------|---------|------|
| Socket 连接 | 启动 sle-daemon + gatewayd | 自动建立连接 |
| 数据帧传输 | DTU 发送 notify | gatewayd 收到并解析 |
| Modbus 解析 | 已知寄存器值 | TelemetryData 字段正确 |
| MQTT 上云 | ThingsKit 平台 | 设备遥测正常显示 |
| 断线重连 | 杀死 sle-daemon 后重启 | gatewayd 自动恢复 |
| 命令下发 | 云端设置继电器 | sle-daemon 收到并转发 |
| Mock 回退 | sle.enable=false | 恢复模拟数据模式 |

---

## 文件变更清单

### 新建文件（8 个）

| 文件 | 进程 | 语言 | 说明 |
|------|------|------|------|
| `common/ipc_protocol.h` | 共用 | C/C++ | IPC 帧定义 |
| `sle_data_app/inc/ipc_sender.h` | sle-daemon | C | IPC 发送接口 |
| `sle_data_app/src/ipc_sender.c` | sle-daemon | C | IPC 发送实现 |
| `gatewayd/include/network/ipc_receiver.h` | gatewayd | C++ | IPC 接收接口 |
| `gatewayd/src/network/ipc_receiver.cpp` | gatewayd | C++ | IPC 接收实现 |
| `gatewayd/include/datasource/sle_data_source.h` | gatewayd | C++ | SLE 数据源接口 |
| `gatewayd/src/datasource/sle_data_source.cpp` | gatewayd | C++ | SLE 数据源实现 |
| `gatewayd/include/app/sle_ipc_worker.h` | gatewayd | C++ | SLE IPC 工作线程 |
| `gatewayd/src/app/sle_ipc_worker.cpp` | gatewayd | C++ | SLE IPC 工作线程实现 |

### 可选新建（Modbus 解析，可后续补充）

| 文件 | 说明 |
|------|------|
| `gatewayd/include/codec/mesh_parser.h` | Mesh 头解析 |
| `gatewayd/src/codec/mesh_parser.cpp` | Mesh 头解析实现 |
| `gatewayd/include/codec/modbus_parser.h` | Modbus RTU 解析 |
| `gatewayd/src/codec/modbus_parser.cpp` | Modbus RTU 解析实现 |

### 修改文件（6 个）

| 文件 | 修改内容 |
|------|---------|
| `sle_data_app/src/notify_printer.c` | 消费循环中增加 IPC 发送调用 |
| `sle_data_app/src/main.c` | 增加 ipc_sender 初始化/反初始化 |
| `sle_data_app/Makefile` | 增加 ipc_sender.c 编译 |
| `gatewayd/include/app/gateway_app.h` | 增加 SleDataSource 和 SleIpcWorker 成员 |
| `gatewayd/src/app/gateway_app.cpp` | 条件化选择 MockDataSource 或 SleDataSource |
| `gatewayd/include/config/config_manager.h` | 增加 SleConfig 结构 |

---

## 关键设计决策

1. **IPC 方式选择 Unix Socket 而非共享内存**：数据量 96KB/s，Socket 完全胜任；进程隔离更安全；双向通信更自然。

2. **sle-daemon 保持 C 语言**：不改语言，只增加 IPC 发送模块，最小化改动。

3. **SleDataSource 与 MockDataSource 同接口**：都返回 `std::vector<TelemetryData>`，CollectWorker/SleIpcWorker 对 gatewayd 其他模块透明。

4. **配置驱动切换**：`sle.enable` 字段决定使用哪个数据源，保持向后兼容。

5. **先通后精**：阶段 1-3 实现数据通道打通（原始 hex 透传），阶段 4 增加 Modbus 解析（可逐步完善每种设备类型）。

---

## 预计工时

| 阶段 | 任务 | 工时 |
|------|------|------|
| 1 | IPC 协议头文件 | 1h |
| 2 | sle-daemon IPC 发送 | 4h |
| 3 | gatewayd IPC 接收 + SleDataSource | 6h |
| 4 | Modbus 解析器 | 4h |
| 5 | 集成测试 | 4h |
| **总计** | | **~19h** |
