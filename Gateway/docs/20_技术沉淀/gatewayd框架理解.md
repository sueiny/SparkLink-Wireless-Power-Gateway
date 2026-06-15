---
type: note
area: technical-notes
tags:
  - gateway/technical
  - gateway/gatewayd
  - gateway/learning
---

# gatewayd 框架理解

本页保留为复盘和学习入口。`gatewayd` 运行期框架细节已迁移到模块目录：

```text
app/Gateway/gatewayd/docs/gatewayd框架理解.md
```

## 学习版要点

- `gatewayd` 是 C++17 主守护进程，负责配置加载、SLE IPC 接收、Modbus 解析、设备映射、SQLite 缓存、MQTT 上云和云端命令下发。
- 主链路是 `SleDataSource -> TelemetryQueue -> PublishManager -> MQTT/SQLite`。
- 下行链路是 `MQTT command -> CommandManager -> CommandExecutor -> IpcCmdSender -> sle_data_app`。
- 网络选择、默认路由和 MQTT 连接在 `network` 与 `cloud` 模块之间配合，当前真实上云使用 `wlan0`。
- 复盘时重点讲清楚：线程边界、队列边界、IPC 抽象 socket、缓存补传和命令回包。

## 关联

- [[20_技术沉淀/Gateway模块说明书]]
- [[20_技术沉淀/SLE数据源替换MockDataSource计划]]
- [[20_技术沉淀/sle_data_app使用说明]]
- [[10_项目复盘/Gateway上板测试记录]]
