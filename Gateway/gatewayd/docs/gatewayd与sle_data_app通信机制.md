# gatewayd 与 sle_data_app 通信机制

## 1. 总览

`gatewayd` 和 `sle_data_app` 是两个独立进程，当前通过两个 Unix Domain Socket 通道通信：

| 通道 | 方向 | Socket | 连接角色 | 作用 |
|------|------|--------|----------|------|
| 数据上行 | `sle_data_app` → `gatewayd` | `/var/run/gateway/sle_data.sock` | `gatewayd` 服务端，`sle_data_app` 客户端 | 发送 SLE ST 帧、Modbus 数据、拓扑数据 |
| 命令下行 | `gatewayd` → `sle_data_app` → `gatewayd` | `/var/run/gateway/sle_cmd.sock` | `sle_data_app` 服务端，`gatewayd` 客户端 | 下发命令并同步等待执行结果 |

配置入口在 `gateway_config.json`：

```json
"sle": {
  "enable": true,
  "data_socket": "/var/run/gateway/sle_data.sock",
  "cmd_socket": "/var/run/gateway/sle_cmd.sock",
  "roots": [{"node_id": 1}, {"node_id": 12}]
}
```

当前实现重点：

- `sle.enable=true` 时，`gatewayd` 使用 `SleDataSource + SleIpcWorker` 替代 `MockDataSource`。
- 上行数据走批量 IPC，再进入 `telemetry_queue_`，由 `PublishManager` 上报 ThingsKit。
- 下行命令从 ThingsKit MQTT 进入 `CommandManager`，再通过 `IpcCmdSender` 发到 `sle_data_app`。
- `sle_data_app` 的命令执行侧当前仍是 Mock：收到命令后返回模拟成功，不真正调用 SLE SDK 写设备。

## 2. 进程启动和模块装配

### 2.1 gatewayd

`gatewayd` 在初始化时根据 `sle.enable` 选择数据源：

- `app/Gateway/gatewayd/src/app/gateway_app.cpp`
- `app/Gateway/gatewayd/src/datasource/sle_data_source.cpp`
- `app/Gateway/gatewayd/src/app/sle_ipc_worker.cpp`

关键装配关系：

```text
GatewayApp::init()
  ├── SleDataSource(init data_socket)
  ├── IpcCmdSender(init cmd_socket)
  ├── SleIpcWorker(data_source -> telemetry_queue)
  ├── PublishManager(telemetry_queue -> MQTT)
  └── CommandManager(command_queue -> IpcCmdSender -> publish_queue)
```

`GatewayApp::run()` 启动 worker 线程：

```text
SleIpcWorker     接收 SLE 数据帧并转成 TelemetryData
NetworkWorker    维护网络状态
PublishManager   发布遥测、网关状态、命令响应
CommandManager   处理云端下行命令
```

### 2.2 sle_data_app

`sle_data_app` 启动时初始化两个 IPC 组件：

- `app/Gateway/sle_data_app/src/ipc_sender.c`：数据上行客户端，首次发送时连接 `gatewayd`。
- `app/Gateway/sle_data_app/src/ipc_cmd_receiver.c`：命令下行服务端，独立线程监听 `gatewayd` 命令。
- `app/Gateway/sle_data_app/src/notify_printer.c`：SLE/mock 回调和数据 IPC 之间的缓冲层。
- `app/Gateway/sle_data_app/src/sle_cmd_handler.c`：命令处理回调，当前为 Mock 实现。

启动顺序：

```text
main()
  ├── ipc_sender_init()
  ├── sle_cmd_handler_init()
  ├── ipc_cmd_receiver_init(cmd_socket, sle_cmd_handler_process)
  ├── notify_printer_start()
  ├── mock_data_generator_start() 或 sle_manager_init()
  └── 等待退出信号
```

## 3. Socket 命名规则

两个通道都使用 Linux abstract namespace Unix socket，但名字处理不完全相同。

### 3.1 数据上行 socket

配置写法：

```text
/var/run/gateway/sle_data.sock
```

实际 abstract name：

```text
@var/run/gateway/sle_data.sock
```

原因：

- `gatewayd` 的 `IpcReceiver` 绑定时会去掉开头 `/`，再在 `sun_path[0]` 写入 `\0`。
- `sle_data_app` 的 `ipc_sender` 连接时也会去掉开头 `/`，再在 `sun_path[0]` 写入 `\0`。

因此数据通道 abstract name 不包含 `/`。

### 3.2 命令下行 socket

配置写法：

```text
/var/run/gateway/sle_cmd.sock
```

实际 abstract name：

```text
@/var/run/gateway/sle_cmd.sock
```

原因：

- `sle_data_app` 中 `CMD_SOCKET_PATH` 定义为 `"\0/var/run/gateway/sle_cmd.sock"`。
- `gatewayd` 的 `IpcCmdSender` 在 `sun_path[0]` 写入 `\0` 后，把配置中的完整路径复制到 `sun_path + 1`，保留 `/`。

因此命令通道 abstract name 包含 `/`。

> 调试时不要把这两个 socket 当成文件系统路径。abstract socket 不需要 `mkdir` 或 `unlink`，也不会出现在 `/var/run/gateway/` 目录下。

## 4. 数据上行通道

### 4.1 流程

```text
SLE notify/mock data
  -> notify_printer 队列
  -> prepare_ipc_packet()
  -> ipc_sender_send_batch()
  -> gatewayd IpcReceiver::receiveRawFrame()
  -> SleDataSource::collect()
  -> parseSleFrameHeader()
  -> handleDataFrame / handleHeartbeatFrame / handleTopoFrame
  -> telemetry_queue_
  -> PublishManager
  -> ThingsKit MQTT
```

### 4.2 IPC 外层帧

数据通道每一帧采用统一的长度前缀：

```text
[0-1] frame_len  2B little-endian
[2-N] frame_body 具体 SLE ST 帧原始字节
```

限制：

- `gatewayd` 接收侧拒绝 `frame_len == 0`。
- `gatewayd` 接收侧拒绝 `frame_len > 256`。
- `sle_data_app` 支持最多 64 帧批量发送，批量发送只是连续写入多组 `len + body`，协议上仍是一帧一帧读取。

### 4.3 sle_data_app 发送侧

`notify_printer` 的设计目标是避免 SLE SDK 回调被日志和 socket 写阻塞：

```text
回调线程
  -> 只复制 notify 数据入队

log_worker 线程
  -> 输出日志
  -> 对 ASCII HEX ST 帧做重组/解码
  -> 凑批后调用 ipc_sender_send_batch()
```

批量策略：

- 最大批量：64 帧。
- 首帧入批后最多等待 1000ms。
- 如果进程退出，会 drain 队列中剩余数据。

### 4.4 gatewayd 接收侧

`SleIpcWorker` 以 `publish.interval_ms` 作为批处理窗口：

```text
while window 未结束:
  frames = SleDataSource::collect()
  batch += frames

if batch 非空:
  telemetry_queue_.push(batch)
```

`SleDataSource::collect()` 行为：

- 如果未连接，尝试 `acceptClient(1000)`。
- 成功连接后读取 `2B len + body`。
- 解析 SLE ST 帧头。
- 根据 `frame_type` 分派：
  - `HEARTBEAT`：生成 DTU 节点状态和拓扑遥测。
  - `DATA`：解析 Modbus RTU，生成外接设备或 DTU 节点遥测。
  - `TOPO_SUMMARY`：更新 `RouteTable`，不直接产生遥测。

### 4.5 SLE ST 帧格式

`gatewayd` 期望 frame body 是 SLE ST 帧：

```text
[0]    0x53 ('S')
[1]    0x54 ('T')
[2]    version = 0x01
[3]    frame_type
[4]    src_role
[5-6]  src_node_id little-endian
[7-8]  dst_node_id little-endian
[9-10] seq little-endian
[11-12] payload_len little-endian
[13-N] payload
```

当前支持的 `frame_type`：

| 值 | 名称 | gatewayd 行为 |
|----|------|---------------|
| `1` | `HEARTBEAT` | 上报 DTU 节点状态、角色、拓扑 |
| `2` | `DATA` | 解析 Modbus RTU，生成设备遥测 |
| `3` | `TOPO_SUMMARY` | 更新路由表 |
| `4` | `DEPTH_UPDATE` | 常量已定义，当前未处理 |

`DATA` payload 格式：

```text
[0]   modbus_type
[1]   modbus_len
[2-N] modbus_rtu
```

`modbus_type` 当前约定：

| 值 | 设备类型 |
|----|----------|
| `2` | 单相电表 |
| `3` | 温湿度变送器 |
| `4` | 继电器 |

## 5. 命令下行通道

### 5.1 流程

```text
ThingsKit MQTT command
  -> MqttCloudClient::onMessage()
  -> GatewayApp::enqueueCloudCommand()
  -> CommandManager::run()
  -> CommandRouter parse topic/payload
  -> CommandValidator validate services/inputData
  -> CommandExecutor::execute()
  -> IpcCmdSender::sendCommand()
  -> sle_data_app ipc_cmd_receiver
  -> sle_cmd_handler_process()
  -> IPC response
  -> CommandManager build response
  -> PublishManager publish command response
```

### 5.2 gatewayd 命令映射

`CommandExecutor` 会先把云端 `method` 映射成 IPC 命令枚举：

| 云端 method | IPC method | 说明 |
|-------------|------------|------|
| `set_relay` | `CMD_METHOD_SET_RELAY = 1` | 继电器控制 |
| `set_mode` | `CMD_METHOD_SET_MODE = 2` | 控制模式 |
| `set_collect_cycle` | `CMD_METHOD_SET_COLLECT_CYCLE = 3` | 采集周期 |
| `trigger_collect` | `CMD_METHOD_TRIGGER_COLLECT = 4` | 触发采集 |
| `reboot` | `CMD_METHOD_REBOOT = 5` | 重启 DTU |

目标 DTU 选择规则：

- 如果目标是 DTU 节点，直接使用该 DTU 的 `node_id`。
- 如果目标是外接设备，使用配置中的 `dtu_id` 找到其挂载 DTU。
- 如果找不到 DTU，回退到 `executeSimulated()`。
- 如果目标是网关自身，不走 IPC，直接走模拟执行。

### 5.3 命令 IPC 外层帧

命令通道也使用 2 字节 little-endian 长度前缀：

```text
[0-1] frame_len  2B little-endian
[2-N] frame_body 命令请求或命令响应
```

### 5.4 命令请求帧

方向：`gatewayd` → `sle_data_app`

```text
[0]     frame_type = 0x01
[1-2]   seq little-endian
[3]     dtu_id
[4]     method
[5-6]   param_len little-endian
[7-N]   param_data
```

说明：

- `seq` 由 `gatewayd` 递增生成，用于匹配响应。
- `param_data` 当前是云端 `params` JSON 字符串。
- `param_len` 最大 256 字节。
- `IpcCmdSender` 写完请求后同步等待响应，当前超时为 3000ms。

### 5.5 命令响应帧

方向：`sle_data_app` → `gatewayd`

```text
[0]     frame_type = 0x02
[1-2]   seq little-endian
[3]     result_code
[4-5]   data_len little-endian
[6-N]   data
```

响应码：

| 值 | 名称 | gatewayd 结果映射 |
|----|------|-------------------|
| `0` | `CMD_RESULT_OK` | `success=true, code=OK` |
| `1` | `CMD_RESULT_FAILED` | `success=false, code=FAILED` |
| `2` | `CMD_RESULT_TIMEOUT` | `success=false, code=TIMEOUT` |
| `3` | `CMD_RESULT_UNSUPPORTED` | `success=false, code=UNSUPPORTED` |

`data` 当前约定为 JSON，例如：

```json
{"result":1,"message":"relay set (mock)"}
```

## 6. 当前能力边界

| 项目 | 当前状态 |
|------|----------|
| 数据 IPC | 已接通，支持批量上送 SLE ST 帧 |
| SLE ST 帧解析 | 已支持 `HEARTBEAT`、`DATA`、`TOPO_SUMMARY` |
| Modbus 解析 | 已由 `modbus_parser.cpp` 转换成 `TelemetryData` |
| 命令 IPC | 已接通，支持请求/响应同步匹配 |
| 命令执行 | `sle_data_app` 侧仍是 Mock，不真正调用 SLE SDK |
| Socket 重连 | 数据通道由 `sle_data_app` 首次发送/失败后重连；`gatewayd` 断连后重新 accept |
| 告警事件 | 不在当前 IPC 主流程内，当前主流程仍是遥测和命令 |

## 7. 常见调试点

### 7.1 数据通道没有遥测

优先检查：

```bash
adb shell "ps | grep -E 'gatewayd|sle_data_app' | grep -v grep"
adb shell "grep -E 'IPC|SLE-IPC|SLE-DS|telemetry batch' /userdata/gateway/data/log/gateway.log | tail -100"
```

判断顺序：

1. `gatewayd` 是否输出 `SleDataSource initialized`。
2. `gatewayd` 是否输出 `receiver listening on @var/run/gateway/sle_data.sock`。
3. `sle_data_app` 是否输出 `connected to gatewayd`。
4. `gatewayd` 是否输出 `client connected`。
5. `gatewayd` 是否输出 `SLE-IPC batch collected ... devices`。

### 7.2 命令下发没有响应

优先检查：

```bash
adb shell "grep -E 'CMD|command|rpc|response' /userdata/gateway/data/log/gateway.log | tail -120"
```

判断顺序：

1. `PublishManager` 是否订阅了 `v1/devices/me/rpc/request/+` 和 `v1/gateway/commands/request`。
2. `MqttCloudClient` 是否收到 MQTT message。
3. `CommandManager` 是否完成解析和物模型校验。
4. `IpcCmdSender` 是否输出 `cmd socket connected to sle_data_app`。
5. `sle_data_app` 是否输出 `[CMD][RX]` 和 `[CMD][TX]`。
6. `gatewayd` 是否输出 `cmd response received`。

### 7.3 socket 连接失败

注意 abstract socket 名字：

- 数据通道是 `@var/run/gateway/sle_data.sock`。
- 命令通道是 `@/var/run/gateway/sle_cmd.sock`。

如果测试工具使用普通文件路径 `/var/run/gateway/*.sock` 去连接，可能报 `connect: No such file or directory`。这不代表运行时 IPC 一定失败，应先确认工具是否支持 abstract namespace。

## 8. 关键源码索引

| 主题 | 文件 |
|------|------|
| gatewayd 主流程装配 | `app/Gateway/gatewayd/src/app/gateway_app.cpp` |
| 数据 IPC 服务端 | `app/Gateway/gatewayd/src/datasource/ipc_receiver.cpp` |
| SLE 数据源解析 | `app/Gateway/gatewayd/src/datasource/sle_data_source.cpp` |
| SLE IPC worker | `app/Gateway/gatewayd/src/app/sle_ipc_worker.cpp` |
| SLE ST 帧解析 | `app/Gateway/gatewayd/src/codec/sle_frame_parser.cpp` |
| 命令 IPC 客户端 | `app/Gateway/gatewayd/src/datasource/ipc_cmd_sender.cpp` |
| 命令执行映射 | `app/Gateway/gatewayd/src/command/command_executor.cpp` |
| sle_data_app 数据发送 | `app/Gateway/sle_data_app/src/ipc_sender.c` |
| notify 队列和批量发送 | `app/Gateway/sle_data_app/src/notify_printer.c` |
| 命令 IPC 服务端 | `app/Gateway/sle_data_app/src/ipc_cmd_receiver.c` |
| 命令协议定义 | `app/Gateway/sle_data_app/inc/ipc_cmd_protocol.h` |
| 命令处理 Mock | `app/Gateway/sle_data_app/src/sle_cmd_handler.c` |

