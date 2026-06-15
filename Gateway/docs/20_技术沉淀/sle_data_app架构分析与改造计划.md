---
type: note
area: technical-notes
tags:
  - gateway/technical
  - gateway/sle
  - gateway/learning
---

# sle_data_app 架构分析与改造计划

本页保留为复盘和学习入口。`sle_data_app` 架构分析与改造细节已迁移到模块目录：

```text
app/Gateway/sle_data_app/docs/架构分析与改造计划.md
```

## 学习版要点

- `sle_data_app` 从独立 SLE client 测试程序，已经演进为 Gateway 的真实 SLE 数据源进程。
- 当前主流程默认 `real`，Mock 需要显式 `--mode mock`。
- 上行可靠性关键点：notify 队列、64 帧满批、时间窗口 flush、IPC 重连。
- 下行命令关键点：命令 socket、参数长度保护、handler mock 成功与真实 SLE write 的边界。
- 后续主要改造方向：真实下行 `set_relay`、SLE 连接保持策略、长时压力测试。

## 关联

- [[20_技术沉淀/sle_data_app使用说明]]
- [[20_技术沉淀/sle_data_app命令对接阅读理解]]
- [[20_技术沉淀/SLE数据源替换MockDataSource计划]]
- [[10_项目复盘/Gateway上板测试记录]]
