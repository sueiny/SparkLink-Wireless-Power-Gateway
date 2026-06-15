---
type: note
area: technical-notes
tags:
  - gateway/technical
  - gateway/gatewayd
  - gateway/sle
  - gateway/learning
---

# SLE 数据源替换 MockDataSource 计划

本页保留为复盘和学习入口。`gatewayd` 侧替换数据源的详细计划已迁移到模块目录：

```text
app/Gateway/gatewayd/docs/SLE数据源替换MockDataSource计划.md
```

## 学习版要点

- 目标是让 `gatewayd` 从本地 Mock 数据源切换到 `sle_data_app` 的真实 SLE IPC 数据源。
- 运行期数据 socket 是抽象 Unix Socket：`@var/run/gateway/sle_data.sock`。
- IPC 帧格式是 `2 字节小端长度 + ST 原始帧`，支持连续 batch。
- 真实链路已通过两路 Root 全拓扑压力测试，`telemetry_cache=0`。
- 后续重点不是 Mock 替换本身，而是 SLE 连接稳定性、真实下行命令和长期压测。

## 关联

- [[20_技术沉淀/gatewayd框架理解]]
- [[20_技术沉淀/sle_data_app使用说明]]
- [[20_技术沉淀/sle_data_app命令对接阅读理解]]
- [[10_项目复盘/Gateway上板测试记录]]
