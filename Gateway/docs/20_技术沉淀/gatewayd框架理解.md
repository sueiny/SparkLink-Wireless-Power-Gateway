---
type: note
area: technical-notes
tags:
  - gateway/technical
  - gateway/gatewayd
---

# gatewayd 框架理解

## 项目中用在哪里

`gatewayd` 是 RK3506 网关主进程，负责把 SLE/模拟数据转成 ThingsKit MQTT 上报，并处理云端命令。它和 `sle_data_app` 是两个独立进程：

- `sle_data_app`：生成或接收 SLE notify 数据，经 Unix Socket 发送给 gatewayd。
- `gatewayd`：解析 SLE/Modbus，维护状态和缓存，发布到 ThingsKit。

## 模块清单

`gatewayd` 运行期模块：

- `app`：主编排和 worker 线程，包含采集、SLE IPC、网络、发布、命令。
- `cloud`：MQTT 客户端，只负责连接、订阅、发布。
- `codec`：SLE 帧、Modbus、ThingsKit payload 编解码。
- `command`：下行命令解析、校验、执行分发和响应构造。
- `common`：日志、时间、文件、阻塞队列、设备模型。
- `config`：读取 `gateway_config.json` 并做启动校验。
- `datasource`：Mock/SLE 数据源、IPC 接收、命令 IPC 发送、路由表。
- `network`：以太网、WiFi、蜂窝网络检测和默认路由切换。
- `state`：命令产生的设备状态补丁，持久化到 SQLite。
- `storage`：SQLite 封装和遥测失败缓存。

Gateway 目录其他模块：

- `sle_data_app`：C 侧 SLE/mock 数据进程，包含 notify 队列、IPC sender、命令 receiver、连接表。
- `sample/sle_tree_test_V1`：SLE 树网络样例代码，包含 root/relay/leaf/link/route/uart/proto。
- `docs` 与 `code_review`：项目方案、技术沉淀、阶段 review 和参考资料。

## 线程与队列

`GatewayApp` 初始化配置、日志、数据源、网络、MQTT、SQLite 后启动 worker：

- `SleIpcWorker` 或 `CollectWorker`：采集一批 `TelemetryData`。
- `NetworkWorker`：周期调用 `NetManager::ensureNetwork()`。
- `PublishManager`：消费遥测队列和发布队列，负责 MQTT 发布与缓存补传。
- `CommandManager`：消费云端命令队列，校验、执行、回包。
- libmosquitto 后台线程：收到 MQTT 消息后只入队，不解析业务。

队列容量：

- `telemetry_queue_`：64。
- `command_queue_`：64。
- `publish_queue_`：256。

队列满时丢弃最旧消息，并由主线程周期性记录 drop 计数。

## 上行数据流

```text
sle_data_app/mock 或 SLE 回调
  -> notify_printer 有界队列
  -> ipc_sender 抽象 Unix Socket
  -> gatewayd IpcReceiver
  -> SleDataSource 解析 SLE 帧
  -> ModbusParser 转 TelemetryData
  -> telemetry_queue
  -> PublishManager
  -> ThingsKitCodec
  -> MqttCloudClient
  -> ThingsKit MQTT
```

发布失败时：

```text
PublishManager
  -> CacheStore
  -> gateway.db telemetry_cache
  -> 网络恢复后按批补传
```

## 下行命令流

```text
ThingsKit MQTT command topic
  -> MqttCloudClient callback
  -> command_queue
  -> CommandRouter
  -> CommandValidator
  -> CommandExecutor
  -> IpcCmdSender
  -> sle_data_app ipc_cmd_receiver
  -> sle_cmd_handler
  -> response publish_queue
  -> PublishManager
  -> MQTT response topic
```

命令执行成功后，`StatePatchCodec` 会生成状态补丁，写入 `DeviceStateStore`，再投递即时遥测。这样即使即时遥测失败，后续周期采集也能叠加该状态。

## 性能关注点

- 热路径不能在 SDK/MQTT 回调里做格式化、数据库写入或网络 I/O。
- SLE 数据链路依赖队列和批处理，批量阈值要兼顾吞吐和低流量延迟。
- SQLite 缓存补传要限制速率，否则历史缓存会挤占实时上报和日志。
- 日志系统当前每行 flush，适合调试，但高频场景会放大 I/O。
- 网络检测包含 DNS/TCP 探测，超时设置会影响网络状态刷新速度。
