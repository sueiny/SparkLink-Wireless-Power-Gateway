---
type: note
area: project-explain
tags:
  - gateway/explain
  - gateway/architecture
  - gateway/rk3506
---

# Gateway 完整项目说明

日期：2026-07-01
范围：`app/Gateway` 当前源码、文档、配置、运行脚本和板端部署形态。

## 1. 项目背景

`Gateway` 属于“基于星闪的无线电力透传系统”的边缘侧与接入侧工程。项目面向配电柜、动力柜、表计箱、楼宇支路和工厂能耗采集等电力场景，解决传统 RS485/Modbus RTU 设备布线复杂、点位扩展困难、单台 4G DTU 独立上云成本高、设备管理分散、现场运维不便等问题。

系统的核心思路是：不改变存量 RS485 仪表和执行器本体，通过 DTU 节点完成本地 Modbus 采集与控制，通过星闪无线链路形成根节点、中继节点、叶子节点组成的现场网络，再由 RK3506 边缘网关统一汇聚、建模、缓存、分析和上云。

项目最终服务的是一条完整工程闭环：

```text
RS485/Modbus 设备
  -> DTU 节点采集或控制
  -> 星闪无线组网
  -> 根 DTU / Root 接入口
  -> RK3506 边缘网关
  -> ThingsKit 云平台
  -> 可视化、告警、命令下发和运维管理
```

相比“每台设备独立联网”的方案，本项目更强调多节点低成本接入、统一上云、边缘自治和后续智能化扩展；相比简单无线串口透传，本项目还包含设备模型、拓扑关系、缓存补传、云端物模型、命令下发、离线规则和可观测运行日志。

## 2. 项目定位

`app/Gateway` 不是单一程序目录，而是一组围绕 RK3506 网关和星闪 DTU 的工程集合，包括：

| 目录 | 定位 |
| --- | --- |
| `gatewayd` | RK3506 Linux 侧主网关进程，负责配置读取、数据源接入、Modbus/ST 解析、设备状态、ThingsKit MQTT 上云、缓存补传、命令解析与执行分发。 |
| `sle_data_app` | RK3506 Linux 侧 SLE 数据源进程，负责真实 SLE 连接、notify 批处理、数据 IPC 上送、命令 IPC 接收和 raw ST 下发转发。 |
| `sample/Dtu` | DTU 侧样例工程，包含 CONFIG 配置协议、RUN 透明桥接、UART/BLE/SLE 通道、存储和板级接口。 |
| `docs` | Gateway 内部文档库，与 Obsidian 风格保持一致，用于项目说明、复盘、技术沉淀、CodeReview、学习技能和参考资料。 |
| `code_review` | 阶段性审查记录，覆盖物模型、上传 payload、凭证模式、命令边界、云端验证、DTU 显示异常和 BOOL/ENUM 状态字段等议题。 |
| `.claude/skills/run-gateway` | 项目内构建、推送、测试驱动脚本，用于 `gatewayd` 和 `sle_data_app` 的板端工作流。 |
| `.opencode/skills/embedded-gateway-dev` | `gatewayd` C++17 嵌入式开发约束、CMake、交叉编译、MQTT/SQLite 集成经验。 |

当前项目以源码为准。部分早期文档仍保留“后续接入”“Mock 第一版”等历史描述，但当前源码已经包含 SLE IPC、命令 IPC、动态拓扑、SQLite 缓存、离线规则和 raw ST 下行等能力。

## 3. 总体架构

系统可以按设备侧、无线侧、边缘侧、平台侧四层理解。

### 3.1 设备侧

设备侧接入多种 RS485/Modbus RTU 设备：

| 设备类型 | 典型数据或能力 |
| --- | --- |
| 单相电表 | 电压、电流、功率、电能、频率、拉合闸状态、清零/控制预留。 |
| 三相电表 | 三相电参数扩展预留。 |
| 温湿度变送器 | 温度、湿度、在线状态、高温/高湿事件预留。 |
| 继电器模块 | 开关状态、控制模式、远程开合控制。 |
| DTU 节点 | 节点角色、MAC、父子拓扑、采集配置、在线状态。 |
| DTU 网关 | 网络状态、云连接状态、设备数量、缓存数量、版本信息。 |

### 3.2 无线侧

无线侧以星闪 DTU 形成现场网络。DTU 可作为根节点、中继节点或叶子节点，支持多级拓扑和跨区域扩展。

运行视角下存在两类拓扑：

| 拓扑 | 含义 |
| --- | --- |
| DTU 通信拓扑 | 星闪网络中的 root、parent、child 关系，用于判断节点在线和转发路径。 |
| 业务设备拓扑 | 电表、环境传感器、继电器等外接设备挂载在哪个 DTU 下，用于上云展示和命令路由。 |

当前配置目标包含 `DTU_001` 到 `DTU_069` 共 69 个 DTU 节点，并包含 9 个外接设备。动态拓扑要求 root 上报 `dtu_topology` 和 `external_map`，启动超时、持久化路径和在线策略由 `gateway_config.json` 控制。

### 3.3 边缘侧

边缘侧运行在 RK3506 Buildroot Linux 上，核心由两个进程协作：

```text
sle_data_app
  -> 数据 socket: @var/run/gateway/sle_data.sock
  -> gatewayd
  -> MQTT / SQLite / rules / command response

gatewayd
  -> 命令 socket: @/var/run/gateway/sle_cmd.sock
  -> sle_data_app
  -> SLE write raw ST frame
```

`sle_data_app` 尽量保持“纯 SLE 连接和数据转发”职责；业务命令、物模型校验、Modbus/ST 封装、设备状态补丁主要由 `gatewayd` 完成。这种边界避免把云端业务逻辑扩散到 C 侧 SLE 进程。

### 3.4 平台侧

平台侧使用 ThingsKit。网关通过 MQTT access token 接入，当前主要 topic 包括：

| 类型 | Topic |
| --- | --- |
| 网关属性 | `v1/devices/me/attributes` |
| 网关遥测 | `v1/devices/me/telemetry` |
| 子设备遥测 | `v1/gateway/telemetry` |
| 网关 RPC | `v1/devices/me/rpc/request/+` |
| 网关命令 | `v1/gateway/commands/request` |

云端物模型位于 `gatewayd/things_model`，包括 `gateway_model.json`、`dtu_node_model.json`、`single_phase_meter_model.json`、`env_sensor_model.json`、`relay_device_model.json` 和汇总文件 `all_product_models.json`。

## 4. 核心工程模块

### 4.1 gatewayd

`gatewayd` 是 RK3506 主网关守护进程，使用 C++17 和 CMake 构建。它负责把 SLE/Mock 数据转换为统一遥测、状态、事件和命令响应，并完成网络连接、MQTT 发布、缓存补传和日志记录。

主要模块如下：

| 模块 | 路径 | 职责 |
| --- | --- | --- |
| app | `gatewayd/src/app`、`gatewayd/include/app` | 进程编排、worker 管理、采集队列、发布队列、命令队列和心跳。 |
| config | `gatewayd/src/config` | 读取 `gateway_config.json`，加载网关、ThingsKit、网络、发布、SLE、拓扑、离线分析和设备配置。 |
| datasource | `gatewayd/src/datasource` | Mock 数据源、SLE 数据源、数据 IPC 接收、命令 IPC 发送、路由表。 |
| codec | `gatewayd/src/codec` | SLE frame、Modbus RTU、ThingsKit payload、topic 和 raw ST 下行封装。 |
| command | `gatewayd/src/command` | 云端命令路由、payload 解析、物模型 service 校验、执行分发和响应。 |
| cloud | `gatewayd/src/cloud` | libmosquitto 封装、连接、订阅、发布、回调入队。 |
| network | `gatewayd/src/network` | 以太网、Wi-Fi、蜂窝网络检查，云端 TCP 连通性检测和优先级选择。 |
| storage/state | `gatewayd/src/storage`、`gatewayd/src/state` | SQLite 缓存、遥测补传、设备状态持久化和状态补丁。 |
| rules/ai | `gatewayd/src/rules`、`gatewayd/src/ai` | 离线规则、阈值判断、离线控制和 AI 风险评分预留。 |
| common | `gatewayd/src/common` | 日志、时间、文件工具、阻塞队列、设备模型基础类型。 |

`GatewayApp` 初始化时会先加载配置和日志，再加载物模型 service registry，初始化 SQLite 状态库，按 `sle.enable` 选择 `SleDataSource` 或 `MockDataSource`，创建 MQTT client、cache store、命令 IPC 发送器和多个 worker。运行时主线程只做心跳和队列丢弃监控，业务由 worker 处理。

### 4.2 sle_data_app

`sle_data_app` 是纯 C 工程，负责 SLE 侧真实数据接入和命令转发。当前默认启动模式为 `real`，无真实 root 时可以显式使用 `--mode mock`。

主要模块如下：

| 模块 | 路径 | 职责 |
| --- | --- | --- |
| main | `sle_data_app/src/main.c` | 解析启动模式、启动 IPC、notify、真实 SLE 或 Mock、主循环和退出清理。 |
| SLE manager | `sle_data_app/src/sle_multi_client.c` | SLE 协议栈生命周期、扫描、连接、SDK 回调、notify 收包。 |
| connection table | `sle_data_app/src/server_connections.c` | root/server 连接状态、超时、重连和 root 路由基础状态。 |
| notify | `sle_data_app/src/notify_printer.c` | notify 队列、批处理、满 64 帧或时间窗口 flush。 |
| data IPC | `sle_data_app/src/ipc_sender.c` | 连接 `gatewayd` 数据 socket，发送长度前缀加帧体 batch。 |
| command IPC | `sle_data_app/src/ipc_cmd_receiver.c` | 监听命令 socket，接收 gatewayd 下发的 IPC 命令并返回响应。 |
| command handler | `sle_data_app/src/sle_cmd_handler.c` | 校验 raw ST 下行参数，选择 READY root 并调用 SLE write。 |
| mock/modbus | `sle_data_app/src/mock_data_generator.c`、`modbus_sim.c` | 无真实 root 时生成测试帧和 Modbus 仿真数据。 |
| config | `sle_data_app/src/sle_app_config.c` | 编译期默认配置和配置打印。 |

设计约束是稳定、清晰、可调试：SDK 回调线程不做 I/O，notify 队列满时丢弃并计数，IPC 失败走重连，不把业务物模型和云端命令解析放入 SLE 进程。

### 4.3 sample/Dtu

`sample/Dtu` 是 DTU 侧样例工程，用于描述和实现 DTU 设备的 CONFIG 与 RUN 两种工作主线。

| 分层 | 主要文件 | 职责 |
| --- | --- | --- |
| 启动层 | `dtu_main.c` | 创建 DTU 初始化任务。 |
| 总控层 | `manager/dtu_service.c` | storage、board、transport 初始化，输入分流和统一回包出口。 |
| CONFIG | `config/dtu_config*.c` | AA55 配置协议、CRC、命令表、GET/SET/COMMIT/REBOOT 等配置命令。 |
| RUN | `run/dtu_run.c` | SLE、UART0、UART1/485 透明桥接和后续 mesh 主线。 |
| storage | `storage/dtu_storage.c` | 默认值、NV 读写、运行配置缓存。 |
| transport | `transport/dtu_channel_*.c` | UART、BLE、SLE 通道初始化、接收 ring、发送和任务。 |
| board/common | `board`、`common` | 拨码、LED、日志、构建参数和跨模块类型。 |

CONFIG 模式负责参数配置和保存，RUN 模式负责现场透明桥接和业务数据传输。GPIO13 高电平进入 RUN，低电平进入 CONFIG；部分 UART 配置需要 COMMIT 后重启生效。

## 5. 功能说明

### 5.1 数据接入与采集

当前支持两种数据来源：

| 数据源 | 用途 | 当前状态 |
| --- | --- | --- |
| `SleDataSource` | 真实 SLE/DTU 数据通过 IPC 进入 `gatewayd`。 | 当前配置默认启用。 |
| `MockDataSource` | 无真实 SLE root 时生成 meter/env/relay/DTU 测试数据。 | 保留用于调试和链路排障。 |

真实链路中，`sle_data_app` 从 SLE notify 收到 root 或 DTU 数据，经批处理后通过数据 socket 发给 `gatewayd`；`gatewayd` 的 `SleIpcWorker` 在一个发布窗口内收集所有到达帧，非空则推入 telemetry queue。

### 5.2 Modbus/ST 解析与设备映射

`gatewayd` 侧通过 `codec` 和 `datasource` 模块解析 ST 帧与 Modbus 数据，并将原始数据映射为统一的 `TelemetryData`。外接设备和 DTU 的关系由拓扑帧、external map、配置设备表和路由表共同决定。

当前重点支持：

- 单相电表：电压、电流、功率、频率、电能、状态等。
- 温湿度变送器：温度、湿度、在线状态。
- 继电器：开关状态和模式。
- DTU 节点：node id、role、parent/root、topology、online、collect config。

### 5.3 ThingsKit 物模型和上云

`gatewayd` 将内部设备模型映射为 ThingsKit payload。网关自身作为直连设备，电表、传感器、继电器和 DTU 节点作为网关子设备上报。

子设备遥测采用 ThingsKit gateway telemetry 格式：

```json
{
  "METER_001": [
    {
      "ts": 1710000000000,
      "values": {
        "voltage": 220.1,
        "current": 3.2,
        "online": 1
      }
    }
  ]
}
```

物模型和命令校验绑定在一起：`gatewayd` 启动时加载 things model 的 services/inputData，命令进入 executor 前会先通过 service registry 和 validator 判断目标设备、服务名和参数是否合法。

### 5.4 网络接入

`gateway_config.json` 中保留以太网、Wi-Fi 和蜂窝网络配置。当前网络模块支持：

- 接口存在检测。
- IPv4 地址检测。
- 云端 TCP 连通性检测。
- 网络脚本拉起。
- `ethernet`、`wifi`、`cellular` 和 `auto` 模式。
- 自动优先级配置，默认可按以太网、Wi-Fi、蜂窝网络顺序选择。

当前配置中的 `network.mode` 为 `cellular`，蜂窝模块配置为 ML307，接口候选包含 `cell0`、`wwan0`、`enx*`。实际板端排障时需要结合默认路由、接口 IP 和 MQTT 连接日志判断当前生效链路。

### 5.5 缓存补传

`gatewayd` 使用 SQLite 作为运行数据库，默认路径是 `/userdata/gateway/data/gateway.db`。当 MQTT 发布失败或网络不可达时，遥测可进入本地缓存；网络恢复后由发布流程补传。

缓存相关配置包括：

- `publish.enable_cache`
- `publish.cache_ttl_ms`
- `telemetry_cache` 表
- 运行日志中的 `CACHE`、`publish success`、`telemetry_cache` 统计

旧文档中出现过 JSONL 缓存描述；当前源码和运行技能中更应以 SQLite `telemetry_cache` 为准。

### 5.6 命令下发

命令下发链路已经形成清晰边界：

```text
ThingsKit RPC 或 gateway command
  -> MqttCloudClient callback 入队
  -> CommandManager
  -> CommandRouter / CommandPayloadCodec
  -> CommandValidator / ThingModelServiceRegistry
  -> CommandExecutor
  -> IpcCmdSender
  -> sle_data_app ipc_cmd_receiver
  -> sle_cmd_handler raw ST bridge
  -> SLE write
  -> command response publish
```

当前真实下行重点是 raw ST bridge：`gatewayd` 根据目标设备和命令构造 Modbus RTU 与完整 ST DATA 帧，`sle_data_app` 不再解释业务含义，只校验 raw ST 参数和 root 路由，然后写入 SLE。

当前命令状态可按目标理解：

| 目标 | 命令 | 状态 |
| --- | --- | --- |
| `METER_*` | `set_relay` | gatewayd 构造 meter Modbus `0x10` 和 ST DATA，走 raw ST IPC，真实执行需以 root/DTU 侧收包和设备动作验证。 |
| `RELAY_*` | `set_relay` | gatewayd 构造 relay Modbus `0x05` 和 ST DATA，走 raw ST IPC，真实执行需以 root/DTU 侧收包和设备动作验证。 |
| `METER_*` | `clear_energy` | 当前保留或 unsupported，不作为真实下发验收项。 |
| `env_sensor` | 无 service | 物模型校验应拒绝。 |
| `gateway` | `reboot`、`ota_upgrade` | 网关自身命令，部分为模拟或预留路径。 |
| `dtu_node` | `reboot`、`set_collect_cycle`、`trigger_collect` | 仍需结合具体 ST/Modbus 帧定义继续落地。 |

### 5.7 离线规则、联动与 AI 预留

`gateway_config.json` 已包含 `offline_analysis` 配置。当前方向包括：

- 离线时本地规则引擎生效。
- 单相电表过压、欠压、频率异常、过流阈值。
- 温湿度越限阈值。
- DTU 离线超时判断。
- 继电器离线控制和恢复后闭合策略。
- AI 风险评分配置预留，当前 `ai.enable` 为 false。

这部分适合用于后续“云不可达时仍具备本地自治”的演示，但真实验收时需要区分配置预留、规则触发日志、命令发出和设备实际动作。

### 5.8 日志与可观测性

核心日志和检查点：

| 项目 | 路径或关键字 |
| --- | --- |
| gatewayd 日志 | `/userdata/gateway/data/log/gateway.log` |
| sle_data_app 输出 | `/tmp/sle_data_app.out` |
| SLE 应用日志 | `/tmp/sle_app.log` |
| SLE raw 日志 | `/tmp/sle_stack_raw.log` |
| gatewayd 心跳 | `/tmp/gatewayd.heartbeat` |
| 数据 socket | `@var/run/gateway/sle_data.sock` |
| 命令 socket | `@/var/run/gateway/sle_cmd.sock` |
| 关键日志 | `SLE-IPC batch collected`、`telemetry batch devices=`、`publish success`、`cloud_connected`、`cmd sent`、`cmd response received` |

## 6. 配置与运行路径

### 6.1 关键路径

| 项目 | 路径 |
| --- | --- |
| 项目根目录 | `app/Gateway` |
| 网关源码 | `app/Gateway/gatewayd` |
| SLE 数据源源码 | `app/Gateway/sle_data_app` |
| DTU 样例源码 | `app/Gateway/sample/Dtu` |
| 主配置 | `app/Gateway/gatewayd/config/gateway_config.json` |
| 板端配置 | `/userdata/gateway/config/gateway_config.json` |
| 板端程序目录 | `/userdata/gateway/bin` |
| 板端日志目录 | `/userdata/gateway/data/log` |
| 板端数据库 | `/userdata/gateway/data/gateway.db` |

### 6.2 构建与部署

推荐从 `app/Gateway` 使用项目脚本：

```bash
bash .claude/skills/run-gateway/driver.sh build-sle
bash .claude/skills/run-gateway/driver.sh build-gw
bash .claude/skills/run-gateway/driver.sh push
bash .claude/skills/run-gateway/driver.sh full
```

`gatewayd` 单独构建入口：

```bash
make -C app/Gateway/gatewayd
make -C app/Gateway/gatewayd push
```

如果 Buildroot staging 中找不到 mosquitto，可传入 `MOSQUITTO_ROOT`。首次构建通常需要确认 Buildroot 已构建 `sqlite` 和 `mosquitto`。

### 6.3 板端运行

常用启动方式：

```bash
adb shell "killall gatewayd 2>/dev/null; nohup /userdata/gateway/bin/gatewayd --config /userdata/gateway/config/gateway_config.json >/tmp/gatewayd.out 2>&1 &"
adb shell "killall sle_data_app 2>/dev/null; nohup /userdata/gateway/bin/sle_data_app --mode real >/tmp/sle_data_app.out 2>&1 &"
```

无真实 root 时使用 Mock：

```bash
adb shell "killall sle_data_app 2>/dev/null"
adb shell "nohup /userdata/gateway/bin/sle_data_app --mode mock >/tmp/sle_data_app.out 2>&1 &"
```

常用检查：

```bash
adb shell "ps | grep -E 'gatewayd|sle_data_app' | grep -v grep"
adb shell "cat /proc/net/unix | grep -E 'sle_data|sle_cmd'"
adb shell "tail -80 /tmp/sle_data_app.out /tmp/sle_app.log /userdata/gateway/data/log/gateway.log"
adb shell "grep -E 'SLE-IPC batch collected|telemetry batch|publish success|cloud_connected' /userdata/gateway/data/log/gateway.log | tail -20"
adb shell "/userdata/gateway/bin/sqlite3 /userdata/gateway/data/gateway.db 'SELECT COUNT(*) FROM telemetry_cache;'"
```

## 7. 当前已具备能力

按当前源码和文档归纳，项目已经具备：

1. RK3506 Buildroot Linux 上的 `gatewayd` 主守护进程。
2. 纯 C `sle_data_app` 数据源进程，默认真实 SLE 模式，支持 Mock 调试。
3. 数据 IPC：`sle_data_app -> gatewayd`，用于 SLE notify batch 上送。
4. 命令 IPC：`gatewayd -> sle_data_app`，用于 raw ST 下行和响应回传。
5. 69 个 DTU 节点配置目标和外接设备配置。
6. 动态拓扑、外接设备映射、在线策略和拓扑持久化配置。
7. 单相电表、温湿度、继电器、DTU 节点、网关自身物模型。
8. ThingsKit MQTT 接入、属性/遥测/子设备遥测上报。
9. 云端命令接入、物模型 service 校验、命令执行分发和响应发布。
10. SQLite 状态库、遥测缓存和补传基础能力。
11. 网络检测和以太网/Wi-Fi/蜂窝链路选择能力。
12. 离线规则、离线控制和 AI 分析配置预留。
13. ADB 构建、推送、运行和测试脚本。
14. 文档库、复盘记录、CodeReview 记录和项目内部技能说明。

## 8. 当前边界与风险

以下内容需要在演示、答辩或交付说明中明确区分：

| 边界 | 说明 |
| --- | --- |
| 真实 SLE 链路依赖现场 root/DTU 状态 | `gatewayd` 和 `sle_data_app` 就绪不等于真实 root 已连接，需看 `[SLE][RX]` 和 `SLE-IPC batch collected`。 |
| Mock 不代表真实无线稳定性 | Mock 只能验证本机 IPC、解析、映射、MQTT/SQLite 链路。 |
| 下行成功不等于设备已动作 | MQTT response 或 IPC OK 只说明命令链路返回，真实动作需看 root/DTU 收包、Modbus CRC、设备反馈和继电器/电表状态。 |
| 部分命令仍是预留 | `clear_energy`、`ota_upgrade`、部分 DTU 管理命令不应作为当前真实验收项。 |
| 网络模式和实际路由需现场确认 | 配置为 cellular 不代表实际默认路由一定走蜂窝，需结合接口、IP、路由和 MQTT 日志判断。 |
| 文档存在历史版本 | 早期文档里的 Mock-only、JSONL 缓存或“待接入 IPC”等说法可能已过期，当前应以源码和最新运行文档为准。 |
| 云端设备/物模型状态会变化 | ThingsKit 设备绑定、profile、产品模型和遥测回查需要现场实时验证。 |

## 9. 项目价值

从工程和产品角度看，本项目的价值集中在：

1. 低侵入改造：保留已有 RS485/Modbus 设备，通过 DTU 和网关完成无线化升级。
2. 降低联网成本：多个现场节点经一个边缘网关统一上云，减少单点蜂窝联网成本和管理复杂度。
3. 适配复杂现场：星闪多级组网适合金属柜体多、点位密集、跨区域部署的电力环境。
4. 云边协同：网关本地完成缓存、规则、状态和命令转发，云端负责展示、告警和管理。
5. 可扩展架构：数据源、物模型、命令、网络、存储、规则和 AI 分析都有清晰模块边界。
6. 可调试可维护：板端日志、SQLite、socket、ADB 脚本、CodeReview 和文档库形成了持续迭代基础。

## 10. 后续演进方向

建议后续按“真实链路优先、验收闭环优先”的顺序推进：

1. 固化真实 SLE 上行基线：持续验证两路 root、全拓扑、批处理、上云和缓存补传。
2. 固化真实下行验收：对 `METER_* set_relay`、`RELAY_* set_relay` 建立 ThingsKit 命令、gatewayd 日志、IPC、root COM、设备动作的完整证据链。
3. 完善 DTU 管理命令：为 `reboot`、`set_collect_cycle`、`trigger_collect` 定义稳定 ST/Modbus 帧和回执语义。
4. 强化动态拓扑：完善 root 上报、拓扑持久化、节点缺失策略和云端拓扑展示。
5. 压测缓存补传：覆盖断网、重连、MQTT publish 失败、缓存 TTL、数据库容量和补传速率。
6. 落地事件上报：把过压、欠压、过流、高温、高湿、节点离线等规则转成 ThingsKit 事件。
7. 完善离线自治：在云不可达时验证本地规则和继电器联动，网络恢复后补传状态与事件。
8. 接入 AI 分析：在稳定数据集基础上接入离线风险评分或云端趋势分析。
9. 整理交付文档：把真实测试证据、拓扑图、云端截图、命令闭环记录和 CodeReview 整改同步到 `docs`。

## 11. 推荐阅读路径

讲项目背景和价值：

- [[00_项目说明/作品介绍]]
- [[00_项目说明/系统架构与技术栈]]
- [[00_项目说明/设备拓扑图]]

理解 gatewayd：

- [[20_技术沉淀/gatewayd框架理解]]
- [[20_技术沉淀/Gateway模块说明书]]
- `gatewayd/README.md`

理解 SLE 数据源和命令：

- [[20_技术沉淀/sle_data_app使用说明]]
- [[20_技术沉淀/sle_data_app命令对接阅读理解]]
- `sle_data_app/docs/README.md`

理解测试、复盘和风险：

- [[10_项目复盘/Gateway上板测试记录]]
- [[30_CodeReview/Gateway全量模块性能CodeReview]]
- [[30_CodeReview/Gateway全量模块可维护性CodeReview]]
- [[30_CodeReview/Gateway可维护性整改路线]]

理解 ThingsKit 和参考资料：

- [[90_参考资料/ThingsKit使用指南]]
- `gatewayd/things_model/README.md`
- `docs/90_参考资料/thingskit-docs/`
