---
type: note
area: technical-notes
tags:
  - gateway/technical
  - gateway/modules
---

# Gateway 模块说明书

日期：2026-06-13  
范围：`app/Gateway` 运行期源码、测试工具、物模型、docs、code_review 与内部 skill。

## 总体架构

Gateway 由两个主要进程组成：

- `gatewayd`：C++17 主守护进程，负责配置加载、SLE/Mock 数据接入、协议解析、状态持久化、MQTT 上云、云端命令下发处理。
- `sle_data_app`：C 侧数据源进程，负责 SLE SDK/Mock 数据生成、notify 批处理、数据 IPC 上送，以及命令 IPC 接收。

核心链路：

```text
上行：sle_data_app -> data socket -> gatewayd -> telemetry queue -> MQTT/SQLite
下行：ThingsKit RPC -> gatewayd command queue -> cmd socket -> sle_data_app -> MQTT response
```

## gatewayd 模块

| 模块 | 职责 | 输入输出 | 关键文件 | 运行时关系 | 维护注意点 |
| --- | --- | --- | --- | --- | --- |
| `app` | 进程编排和 worker 生命周期管理。 | 输入配置、队列、数据源、MQTT client；输出 worker 线程和队列消息。 | `gateway_app.cpp`、`publish_manager.cpp`、`command_manager.cpp`、`network_worker.cpp`、`sle_ipc_worker.cpp`。 | `GatewayApp` 初始化所有模块，启动采集/网络/发布/命令线程。 | 保持 worker 职责单一；发布线程不要继续膨胀为缓存、实时、回包、统计的混合大类。 |
| `cloud` | MQTT 连接、订阅、发布。 | 输入 topic/payload；输出 MQTT publish/subscribe 和 message callback。 | `mqtt_cloud_client.cpp`、`mqtt_cloud_client.h`。 | libmosquitto 后台线程收到消息后只入队到 `CommandManager`。 | 回调里不能做 JSON 解析、数据库或阻塞 I/O；重连后必须恢复命令订阅。 |
| `codec` | 协议编解码。 | 输入 SLE 帧、Modbus RTU、TelemetryData；输出结构化遥测和 ThingsKit JSON。 | `sle_frame_parser.cpp`、`modbus_parser.cpp`、`thingskit_codec.cpp`、`thingskit_topics.h`。 | 位于数据源和发布模块之间，也提供固定 ThingsKit topic 常量。 | topic 常量应继续集中维护；解析错误建议接入统一日志或错误码，避免分散 `stderr`。 |
| `command` | 下行命令解析、物模型校验、执行分发、响应构造。 | 输入 MQTT topic/payload；输出 `CommandResult` 和 MQTT response。 | `command_router.cpp`、`command_validator.cpp`、`command_executor.cpp`、`thing_model_service_registry.cpp`。 | `CommandManager` 串接 router、validator、executor，再投递 response 到 publish queue。 | 需要补强 IPC 参数长度保护；命令 payload 示例应集中写入文档或测试。 |
| `common` | 通用能力：日志、队列、时间、文件、设备模型。 | 输入通用请求；输出基础工具能力。 | `logger.cpp`、`blocking_queue.h`、`device_model.cpp`、`file_utils.cpp`。 | 被所有模块依赖，是低层基础库。 | 日志每行 flush 和队列满丢旧策略会影响全局行为，改动要有回归测试。 |
| `config` | 读取和校验 `gateway_config.json`。 | 输入 JSON 文件；输出 `AppConfig`。 | `config_manager.cpp`、`config_manager.h`。 | 启动阶段由 `main/GatewayApp` 调用。 | 继续补充重复设备、DTU 拓扑、socket 路径长度等校验，减少运行期隐患。 |
| `datasource` | 数据源抽象、SLE IPC 接收、命令 IPC 发送、路由表。 | 输入 IPC 字节流或 mock 配置；输出遥测批次或命令响应。 | `ipc_receiver.cpp`、`ipc_cmd_sender.cpp`、`sle_data_source.cpp`、`mock_data_source.cpp`、`route_table.cpp`。 | `SleIpcWorker` 消费数据源，`CommandExecutor` 通过 `IpcCmdSender` 连接 `sle_data_app`。 | 运行期使用抽象 Unix Socket，测试工具必须保持同一语义；命令 IPC 要校验 payload 长度。 |
| `network` | 网络检测、接口选择、默认路由切换。 | 输入网络配置；输出 `NetworkState`。 | `net_manager.cpp`、`wifi_provider.cpp`、`ethernet_provider.cpp`、`cellular_provider.cpp`、`network_utils.cpp`。 | `NetworkWorker` 周期刷新状态，`PublishManager` 连接 MQTT 前使用选中接口。 | DNS/TCP 探测耗时要可观测；避免频繁 shell 命令和阻塞拖慢状态更新。 |
| `state` | 设备状态补丁持久化和遥测叠加。 | 输入命令执行结果和遥测；输出 SQLite 状态和叠加后的遥测。 | `device_state_store.cpp`、`state_patch_codec.cpp`。 | 命令成功后写状态库，后续采集叠加状态字段。 | 当前持久化偏全量重写，后续应改为单设备 upsert；状态字段要与物模型保持一致。 |
| `storage` | SQLite 封装、遥测失败缓存和补传。 | 输入待缓存 payload；输出查询、删除、补传批次。 | `sqlite_db.cpp`、`cache_store.cpp`。 | `PublishManager` 发布失败写缓存，网络恢复后补传。 | 大缓存补传要限速和降噪；SQLite 事务边界要保持清晰。 |
| `things_model` | ThingsKit 产品物模型和脚本。 | 输入 JSON 物模型；输出命令服务定义、属性和遥测 schema。 | `gateway_model.json`、`single_phase_meter_model.json`、`relay_device_model.json`、`env_sensor_model.json`、`dtu_node_model.json`。 | `ThingModelServiceRegistry` 启动时加载服务定义，云端平台也依赖这些 schema。 | 当前 `env_sensor` 无 service；`single_phase_meter` 和 `relay_device` 有 `set_relay`；变更前要同步平台。 |
| `test/tests` | 测试数据、IPC 工具、压力脚本。 | 输入测试帧和板端命令；输出测试结果。 | `test/ipc_send.c`、`test/gen_test_frames.py`、`tests/stress_test_*.sh`。 | `driver.sh` 会生成测试 payload、编译 `ipc_send` 并推送板端。 | `ipc_send` 需要支持抽象 socket，否则会与运行期链路不一致。 |

## sle_data_app 模块

| 模块 | 职责 | 输入输出 | 关键文件 | 运行时关系 | 维护注意点 |
| --- | --- | --- | --- | --- | --- |
| 主流程 | 初始化信号、日志重定向、IPC、mock、命令接收器。 | 输入启动参数和默认配置；输出进程生命周期。 | `main.c`。 | 启动 `ipc_sender`、`ipc_cmd_receiver`、`notify_printer`、`mock_data_generator`。 | 启停顺序要保持成对；信号处理不要让 worker 随机退出。 |
| SLE client | SLE SDK 扫描、连接和 notify 回调。 | 输入 SDK 事件；输出 notify 数据。 | `sle_multi_client.c`、`sle_multi_client.h`。 | 当前主流程可跳过真实 SLE，仅使用 mock 数据。 | 文件较长，建议后续按扫描、连接、回调、状态拆分内部函数。 |
| server connections | 连接表和 server index 管理。 | 输入连接事件；输出连接状态查询。 | `server_connections.c`、`server_connections.h`。 | 供 SLE client 和 notify 处理使用。 | 保持连接表边界清楚，避免业务字段继续堆进连接管理。 |
| mock generator | 生成 DTU/设备模拟帧。 | 输入默认模拟参数；输出 notify 队列数据。 | `mock_data_generator.c`。 | 用于板端无真实 SLE 设备时验证 gatewayd 链路。 | Mock 数据结构要和 Modbus/SLE parser 同步，建议保留固定测试样例。 |
| notify printer | notify 队列、批处理和转发。 | 输入 SLE/mock 帧；输出 IPC batch。 | `notify_printer.c`。 | 消费有界队列，满批后调用 `ipc_sender_send_batch()`。 | 需要时间窗口 flush，避免低流量场景未满 64 帧时长期不发送。 |
| IPC sender | 连接 gatewayd 数据 socket 并发送 batch。 | 输入 frame batch；输出 Unix Socket 字节流。 | `ipc_sender.c`。 | 与 gatewayd `IpcReceiver` 使用抽象 socket。 | socket 语义要和测试工具一致；断线重连日志应可过滤。 |
| 命令 receiver | 监听命令 socket，读取命令帧并回包。 | 输入 gatewayd 命令帧；输出命令响应帧。 | `ipc_cmd_receiver.c`、`ipc_cmd_protocol.h`。 | gatewayd `IpcCmdSender` 连接该 socket。 | 已有长度校验；协议结构变化必须同步 gatewayd include path。 |
| 命令 handler | 执行 set_relay/set_mode/采集/重启等命令。 | 输入 IPC command request；输出 result code 和 JSON data。 | `sle_cmd_handler.c`。 | 当前为 mock 成功，后续接真实 SLE 写请求。 | 真实执行前要定义超时、失败码和设备不可达语义。 |
| Modbus 仿真 | 构造或解析模拟 Modbus 数据。 | 输入站号/寄存器；输出协议帧。 | `modbus_sim.c`、`modbus_sim.h`。 | 被 mock generator 使用。 | 寄存器规格要与 `Modbus寄存器仿真规格.md` 同步。 |

## 其他模块

| 模块 | 职责 | 输入输出 | 关键文件 | 运行时关系 | 维护注意点 |
| --- | --- | --- | --- | --- | --- |
| `sample/sle_tree_test_V1` | SLE 树形组网样例。 | 输入 UART/SLE 样例事件；输出 root/relay/leaf 路由行为。 | `ST_test_*.c`、`README.md`。 | 不直接参与 gatewayd 运行，是协议和组网参考。 | 样例代码可保留，但不要让其测试结论直接替代 gatewayd 上板验证。 |
| `docs` | 项目说明、复盘、技术沉淀、review、参考资料。 | 输入项目资料；输出可检索 Markdown。 | `00_项目说明`、`20_技术沉淀`、`30_CodeReview`。 | 与 Obsidian 双向同步。 | 主线索引只挂关键资料，ThingsKit/Yuque 全量资料留在参考区。 |
| `code_review` | 历史阶段 review 记录。 | 输入阶段性问题；输出 review 归档。 | `00_阶段总览_CodeReview.md` 等。 | 可作为 `docs/30_CodeReview` 的素材来源。 | 后续新增 review 优先进入标准 docs 分区，再按需回链历史目录。 |
| 内部 skills | 工程约束和运行流程。 | 输入开发/测试任务；输出 Codex/脚本执行规则。 | `.opencode/skills/embedded-gateway-dev`、`.claude/skills/run-gateway`、`~/.codex/skills/gateway-runtime`。 | 指导代码审查、上板构建、监听和命令下发验证。 | skill 要随路径、socket、driver 变化同步更新，避免自动化使用过期流程。 |

## 当前命令下发能力说明

已观察到板端命令链路正常：

- `METER_*` 的 `set_relay`：MQTT 收到 `v1/devices/me/rpc/request/<id>`，gatewayd 解析为 `method=set_relay`，通过命令 IPC 发给 `sle_data_app`，返回 `OK` 并发布 response。
- `RELAY_*` 的 `set_relay`：同样可收到、执行和回包。
- `env_sensor` 当前物模型没有 `services`，因此没有下发命令入口。
- `gateway` 和 `dtu_node` 物模型有 `reboot`，但本轮随机测试未重点覆盖。

重复下发现象已观察到：同一目标和参数短时间出现多个 `rpc/request/<id>`。gatewayd 均正常处理并回包，是否需要去重应先确认云端/页面侧触发逻辑。

## 设备 Service 对照表

| 设备/产品 | 支持 service | 参数 | 当前执行路径 | 测试结论 |
| --- | --- | --- | --- | --- |
| `single_phase_meter` / `METER_*` | `set_relay`、`clear_energy` | `set_relay.state` 为 `0/1`；`clear_energy` 当前无输入参数 | `set_relay` 走命令 IPC；`clear_energy` 在 executor 中保留为 unsupported | `set_relay` 已验证可收到、IPC 执行并回包；`clear_energy` 未启用真实执行 |
| `relay_device` / `RELAY_*` | `set_relay` | `state` 为 `0/1` | 走命令 IPC 到 `sle_data_app` | 已验证可收到、执行并回包 |
| `env_sensor` | 无 | 无 | Validator 应拒绝不支持 service | 当前不应在平台侧暴露下发入口 |
| `gateway` | `reboot`、`ota_upgrade` | `reboot` 无输入；`ota_upgrade.url/version` | gateway 自身命令走模拟/预留路径，不走 SLE IPC | 本轮未重点测试，`ota_upgrade` 为 reserved/unsupported |
| `dtu_node` | `reboot` | 无输入 | 通过目标 DTU ID 走命令 IPC | 本轮未重点测试 |

排查建议：随机测试时优先使用 `METER_*` 和 `RELAY_*` 的 `set_relay`。如果对 `env_sensor` 下发命令，应按“不支持 service”处理，而不是判断为链路故障。
