---
type: note
area: technical-notes
tags:
  - gateway/technical
  - gateway/sle
  - gateway/command
  - gateway/learning
---

# sle_data_app 命令对接阅读理解

本页保留为复盘和学习入口。`sle_data_app` 命令对接的完整阅读文档已迁移到模块目录：

```text
app/Gateway/sle_data_app/docs/命令对接阅读理解.md
```

## 学习版要点

- 上行数据链路：`SLE notify/mock -> notify_printer -> ipc_sender -> gatewayd`。
- 下行命令链路：`ThingsKit RPC -> gatewayd command -> IpcCmdSender -> ipc_cmd_receiver -> sle_cmd_handler -> response`。
- 命令 IPC socket 是 `@/var/run/gateway/sle_cmd.sock`。
- 当前 `ipc_cmd_receiver` 是真实 IPC server，但 `sle_cmd_handler.c` 仍是 `[CMD][MOCK]` 成功响应。
- `set_relay`、`set_mode`、`set_collect_cycle`、`trigger_collect`、`reboot` 能进入 handler 并回包。
- 真实 SLE 写 root/DTU/Modbus 设备还未落地，不能用当前 success response 证明设备真实动作。

## 关联

- [[20_技术沉淀/sle_data_app使用说明]]
- [[20_技术沉淀/Gateway模块说明书]]
- [[10_项目复盘/Gateway上板测试记录]]
- [[30_CodeReview/Gateway可维护性整改路线]]
