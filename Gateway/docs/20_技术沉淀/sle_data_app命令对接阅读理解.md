---
type: note
area: technical-notes
tags:
  - gateway/technical
  - gateway/sle
  - gateway/command
---

# sle_data_app 命令对接阅读理解

日期：2026-06-15  
范围：`sle_data_app` 与 `gatewayd` 的上行数据、下行命令、IPC 协议和源码阅读入口。

## 先读结论

`sle_data_app` 的上行数据链路已经进入真实 SLE 联调阶段；默认启动为 `real`，Mock 需要显式 `--mode mock`。下行命令链路的 IPC 通道已经打通，但 `sle_cmd_handler.c` 仍是 mock 成功响应，真实 SLE 写命令还没有落地。

因此当前验收要分开看：

- 上行真实数据：可以用 `COM23 + COM36` 两路 Root 压测全拓扑是否上云。
- 下行命令通道：可以验证 ThingsKit -> gatewayd -> IPC -> `sle_data_app` -> MQTT response 是否闭环。
- 下行真实执行：不能仅凭 success response 判断设备实际动作，必须等 `sle_cmd_handler` 接入真实 SLE write 后再验收。

## 上行数据流

源码入口：

| 阶段 | 文件 | 关键职责 |
| --- | --- | --- |
| 进程编排 | `sle_data_app/src/main.c` | 解析 `--mode`，启动 `ipc_sender`、`ipc_cmd_receiver`、`notify_printer`、真实 SLE 或 Mock。 |
| 真实 SLE | `sle_data_app/src/sle_multi_client.c` | 扫描、连接、接收 notify，将 RX 数据交给上层。 |
| Mock 数据 | `sle_data_app/src/mock_data_generator.c` | 生成 DTU 心跳和外接设备 Modbus 数据，用于无真实 root 调试。 |
| 批处理 | `sle_data_app/src/notify_printer.c` | 将 notify 入队，满 64 帧或时间窗口到达后 flush。 |
| 数据 IPC | `sle_data_app/src/ipc_sender.c` | 连接 `gatewayd` 数据 socket，发送长度前缀加帧体的 batch。 |
| gatewayd 接收 | `gatewayd/src/datasource/ipc_receiver.cpp` | 监听抽象 socket，读取 SLE IPC batch。 |
| 解析映射 | `gatewayd/src/datasource/sle_data_source.cpp`、`gatewayd/src/codec` | 解析 ST 帧、Modbus RTU，映射设备 ID。 |
| 发布缓存 | `gatewayd/src/app/publish_manager.cpp`、`gatewayd/src/storage` | MQTT 发布成功则上云，失败则写 SQLite cache。 |

运行形态：

```text
SLE notify 或 Mock frame
  -> notify_printer_enqueue()
  -> notify_printer worker
  -> ipc_sender_send_batch()
  -> gatewayd IpcReceiver
  -> SleDataSource
  -> ThingsKit telemetry JSON
```

上行调试看这几个关键字：

```text
[SLE][RX]
[IPC] connected to gatewayd
SLE-IPC batch collected
telemetry batch devices=
publish success kind=telemetry
```

## 下行命令流

源码入口：

| 阶段 | 文件 | 关键职责 |
| --- | --- | --- |
| MQTT 收命令 | `gatewayd/src/cloud/mqtt_cloud_client.cpp` | 接收 ThingsKit RPC 或 gateway command topic。 |
| 命令线程 | `gatewayd/src/app/command_manager.cpp` | 从队列取命令，调用 router/validator/executor。 |
| 路由解析 | `gatewayd/src/command/command_router.cpp`、`command_payload_codec.cpp` | 解析 target、method、params、request_id。 |
| 物模型校验 | `gatewayd/src/command/command_validator.cpp`、`thing_model_service_registry.cpp` | 根据 things_model service 判断命令是否支持。 |
| 执行分发 | `gatewayd/src/command/command_executor.cpp` | gateway 自身命令模拟执行，外接设备/DTU 命令走 IPC。 |
| IPC 发送 | `gatewayd/src/datasource/ipc_cmd_sender.cpp` | 连接 `@/var/run/gateway/sle_cmd.sock`，发送命令帧并等待响应。 |
| IPC 接收 | `sle_data_app/src/ipc_cmd_receiver.c` | 监听命令 socket，解析命令帧，调用 handler，回响应帧。 |
| 命令处理 | `sle_data_app/src/sle_cmd_handler.c` | 当前 mock 成功响应；真实 SLE 写命令待实现。 |

运行形态：

```text
ThingsKit RPC request
  -> CommandRouter
  -> CommandValidator
  -> CommandExecutor
  -> IpcCmdSender
  -> ipc_cmd_receiver
  -> sle_cmd_handler
  -> IpcCmdSender response
  -> ThingsKit RPC response
```

## IPC 命令协议

命令 socket 是抽象 Unix Socket：

```text
@/var/run/gateway/sle_cmd.sock
```

帧格式是：

```text
2 字节 LE 长度前缀 + frame body
```

request frame body：

| 字段 | 长度 | 含义 |
| --- | --- | --- |
| `frame_type` | 1 | `IPC_FRAME_TYPE_CMD_REQUEST`。 |
| `seq` | 2 | gatewayd 侧命令序号，小端。 |
| `dtu_id` | 1 | 目标 DTU node id。 |
| `method` | 1 | `CMD_METHOD_*` 枚举。 |
| `param_len` | 2 | 参数 JSON 长度，小端。 |
| `param_data` | `param_len` | params JSON 字符串。 |

response frame body：

| 字段 | 长度 | 含义 |
| --- | --- | --- |
| `frame_type` | 1 | `IPC_FRAME_TYPE_CMD_RESPONSE`。 |
| `seq` | 2 | 与 request 对应。 |
| `result_code` | 1 | `CMD_RESULT_OK`、`FAILED`、`TIMEOUT`、`UNSUPPORTED` 等。 |
| `data_len` | 2 | response data 长度。 |
| `data` | `data_len` | JSON 或文本响应。 |

长度边界：

- `gatewayd` 在 `CommandExecutor::executeViaIpc()` 里拒绝超过 `IPC_CMD_MAX_PARAM_LEN` 的 params。
- `sle_data_app` 在 `ipc_cmd_receiver.c` 里校验 `param_len > IPC_CMD_MAX_PARAM_LEN` 并拒绝处理。

## 支持命令对照

| 目标 | method/service | gatewayd 行为 | sle_data_app 当前行为 | 真实执行状态 |
| --- | --- | --- | --- | --- |
| `METER_*` | `set_relay` | 通过设备配置找到 `dtu_id`，走命令 IPC | `[CMD][MOCK] set_relay` 并返回成功 JSON | IPC 闭环已验证，真实 SLE 写待实现。 |
| `RELAY_*` | `set_relay` | 同上 | `[CMD][MOCK] set_relay` 并返回成功 JSON | IPC 闭环已验证，真实 SLE 写待实现。 |
| `METER_*` | `clear_energy` | executor 中保留为 unsupported/reserved | 不进入真实 SLE handler | 不作为当前验收项。 |
| `env_sensor` | 无 service | validator 应拒绝 unknown service | 不进入 handler | 平台侧不应暴露下发入口。 |
| `gateway` | `reboot`、`ota_upgrade` | gateway 自身模拟/预留路径，不走 SLE IPC | 不涉及 DTU handler | `ota_upgrade` reserved/unsupported。 |
| `dtu_node` | `reboot` | 找到 DTU node id 后走 IPC | `[CMD][MOCK] reboot` 并返回成功 JSON | 真实 root reboot 待实现。 |
| 外接设备/DTU | `set_mode`、`set_collect_cycle`、`trigger_collect` | 可映射到 IPC method | handler 返回 mock success | 真实写配置/采集触发待实现。 |

## 日志字段怎么串

优先用这些字段定位一次命令：

| 字段 | 来源 | 用途 |
| --- | --- | --- |
| `request_id` | ThingsKit topic 或 payload | 串起 MQTT request、IPC send、MQTT response。 |
| `method` | payload 或 concise property command | 判断服务名是否被正确转换。 |
| `target` / `targetDeviceId` | payload、router response | 判断命令落到哪个设备。 |
| `dtu_id` | executor、IPC request | 判断外接设备映射到哪个 DTU。 |
| `seq` | `IpcCmdSender` 与 `ipc_cmd_receiver` | 串起 IPC request/response。 |
| `result` / `code` | handler、CommandResult、MQTT response | 判断失败原因。 |

常用 grep：

```bash
adb shell "grep -E 'request_id|method=|target|cmd sent|cmd response received|command_response|\\[CMD\\]' /userdata/gateway/data/log/gateway.log /tmp/sle_data_app.out | tail -120"
```

如果日志显示 `UNKNOWN_SERVICE` 或 `BAD_PARAMS`，先查 `things_model/*.json` 和 `command_validator.cpp`，不要先改 SLE 层。

## 阅读顺序

建议按运行链路读，不按文件名读：

1. `sle_data_app/src/main.c`：确认默认 real、mock 显式开启、线程启动顺序。
2. `sle_data_app/src/notify_printer.c`：确认批处理、时间窗口 flush、队列丢弃策略。
3. `sle_data_app/src/ipc_sender.c`：确认数据 socket、重连和 batch 编码。
4. `gatewayd/src/datasource/ipc_receiver.cpp`：确认 gatewayd 如何接收 batch。
5. `gatewayd/src/datasource/sle_data_source.cpp`：确认 ST/Modbus 到 TelemetryData 的转换。
6. `gatewayd/src/command/command_router.cpp` 和 `command_payload_codec.cpp`：确认云端 payload 怎么变成 `CommandRequest`。
7. `gatewayd/src/command/command_validator.cpp`：确认物模型 service 校验。
8. `gatewayd/src/command/command_executor.cpp`：确认哪些命令走 IPC，哪些是模拟/预留。
9. `gatewayd/src/datasource/ipc_cmd_sender.cpp`：确认命令 IPC 发送和 `request_id` 日志。
10. `sle_data_app/src/ipc_cmd_receiver.c` 与 `sle_cmd_handler.c`：确认命令最终在 C 侧如何处理。

## 后续真实下行落地方向

`sle_cmd_handler.c` 需要从 mock 成功改为真实执行时，建议先只做 `set_relay`：

- 根据 `dtu_id` 查到当前 root/DTU 的 SLE connection。
- 将 `params` JSON 转成 Modbus 写单寄存器或线圈请求。
- 通过 SLE write API 下发到 root。
- 等待 ACK 或超时，返回 `CMD_RESULT_OK/TIMEOUT/FAILED`。
- 日志必须带 `dtu_id`、`method`、`seq`、`result`，gatewayd 侧带 `request_id`。

不要一次性把 `set_mode`、`set_collect_cycle`、`trigger_collect`、`reboot` 全部真实化；先把 `set_relay` 的失败码和超时语义跑稳。

## 关联文档

- [[20_技术沉淀/sle_data_app使用说明]]
- [[20_技术沉淀/Gateway模块说明书]]
- [[10_项目复盘/Gateway上板测试记录]]
- [[30_CodeReview/Gateway可维护性整改路线]]
