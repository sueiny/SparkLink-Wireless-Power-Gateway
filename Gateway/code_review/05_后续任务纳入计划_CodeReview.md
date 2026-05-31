# 后续任务纳入计划 Code Review

> 日期：2026-05-11  
> 目的：把之前计划中的未完成任务纳入后续阶段，避免需求丢失。

## 1. 当前计划状态复盘

原计划中的任务可以重新归类为三层：

```text
第一层：已经完成并验证
第二层：已经预留，但暂不执行
第三层：后续真实接入阶段再做
```

## 2. 已完成并验证

| 任务 | 状态 | 说明 |
|------|------|------|
| MQTT 上云 | 已完成 | 能连接 ThingsKit 并发布 |
| 网关状态属性 | 已完成 | 网关自身属性和遥测都可上报 |
| 子设备遥测 | 已完成 | 通过 `v1/gateway/telemetry` 代发 |
| 失败缓存 | 已完成基础实现 | MQTT 失败可写缓存，连接恢复可补传 |
| 配置重构 | 已完成 | 新配置包含 meter/env/relay/DTU |
| access token | 已完成 | 默认使用 access token，保留 basic |
| DTU 拓扑文档 | 已完成 | README 中已说明通信拓扑和业务拓扑 |
| 命令下发解析 | 已完成 | 只解析、校验、回包 |

## 3. 已预留但暂不执行

| 任务 | 当前状态 | 边界 |
|------|----------|------|
| 网关 `reboot` 服务 | 物模型保留，板端只解析 | 不调用系统重启 |
| 网关 `ota_upgrade` 服务 | 物模型保留，板端只解析 | 不下载、不升级 |
| 电表 `set_relay` 服务 | 物模型保留，板端只解析 | 不写 Modbus 寄存器 |
| 电表 `clear_energy` 服务 | 物模型保留，板端只解析 | 不清零电量 |
| 继电器 `set_relay` 服务 | 物模型保留，板端只解析 | 不控制硬件 |
| 继电器 `set_mode` 服务 | 物模型保留，板端只解析 | 不改变模式 |
| DTU `set_collect_cycle` 服务 | 物模型保留，板端只解析 | 不修改实际采集周期 |
| DTU `trigger_collect` 服务 | 物模型保留，板端只解析 | 不触发真实采集 |
| 各类事件 | 物模型保留 | 不主动上报 |

## 4. 后续真实接入阶段任务

### 4.1 服务执行层

建议新增轻量模块，而不是继续膨胀 `GatewayApp`：

```text
CommandParser：解析 method/params/target
CommandRouter：按设备类型和服务名分发
CommandExecutor：执行具体动作
```

第一步可以只保留 `CommandParser` 和 `CommandRouter`，执行层先返回未实现。

### 4.2 事件上报层

事件建议分两步：

1. 用脚本先验证 ThingsKit 事件 payload 和 Topic。
2. 板端再接入事件触发逻辑。

不要直接从 mock 数据里大量自动触发事件，否则容易干扰平台测试。

### 4.3 真实 DTU/WS73 接入

后续不要改 `MqttCloudClient` 和 `ThingsKitMapper` 的职责边界。

建议新增：

```text
Ws73DataSource : IDataSource
DtuFrameDecoder
ModbusDeviceMapper
```

数据流保持：

```text
真实数据源 -> TelemetryData -> ThingsKitMapper -> MqttCloudClient
```

### 4.4 Modbus 写寄存器

服务执行最终会落到 Modbus 写寄存器，例如：

| 服务 | 后续真实动作 |
|------|--------------|
| 电表 `set_relay` | 写拉合闸控制寄存器 |
| 电表 `clear_energy` | 写电量清零寄存器 |
| 继电器 `set_relay` | 写继电器控制寄存器 |
| DTU `set_collect_cycle` | 修改采集配置并保存 |

当前阶段不要实现这些动作，因为还没有真实链路闭环。

## 5. 学习式总结

这一阶段最重要的经验是：先把“协议边界”做稳，再做“执行动作”。

命令链路可以分成三件事：

```text
能不能收到
能不能看懂
能不能执行
```

当前只完成前两件事：

```text
能收到
能看懂
```

第三件事：

```text
能执行
```

应该留到真实 DTU、Modbus、硬件控制链路明确之后再做。

