---
type: note
area: technical-notes
tags:
  - gateway/technical
  - gateway/sle
  - gateway/runtime
  - gateway/learning
---

# sle_data_app 使用说明

本页保留为复盘和学习入口。`sle_data_app` 的完整运行说明已迁移到模块目录：

```text
app/Gateway/sle_data_app/docs/使用说明.md
```

## 学习版要点

- `sle_data_app` 现在默认使用真实链路：不带参数等价于 `--mode real`。
- `--mode mock` 仅用于无真实 Root 时验证 IPC、解析、MQTT 链路。
- `--mode hybrid` 只用于排查，不作为正式验收模式。
- 板端真实监听入口：`bash .claude/skills/run-gateway/driver.sh test-real-listen`。
- 当前两路真实 Root 基线：Windows `COM23 + COM36`，脚本使用 `py -3 dtu_root_run_sender.py ... --scenario topology-all`。
- 空闲 5 分钟测试显示：启动阶段有接入抖动，两路 READY 后未继续新增断连。

## 关联

- [[20_技术沉淀/sle_data_app命令对接阅读理解]]
- [[20_技术沉淀/Gateway模块说明书]]
- [[10_项目复盘/Gateway上板测试记录]]
- [[00_项目说明/设备拓扑图]]
