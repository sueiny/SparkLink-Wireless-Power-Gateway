# sle_data_app docs

这个目录保存 `sle_data_app` 模块自身的工程文档。这里是源码旁边的主文档位置，适合记录真实 SLE 运行、Mock 调试、notify 批处理、数据 IPC、命令 IPC、串口脚本和连接参数。

`app/Gateway/docs` 只保留复盘、学习和索引入口；需要改 `sle_data_app` 工程细节时优先改本目录。

## 当前运行定位

`sle_data_app` 是 Gateway 的 C 侧 SLE 数据源进程：

```text
真实 DTU root / Mock
  -> SLE notify 队列
  -> notify_printer 批处理
  -> ipc_sender 数据 socket
  -> gatewayd SLE IPC worker
```

默认启动模式是 `real`。Mock 需要显式开启：

```bash
/userdata/gateway/bin/sle_data_app
/userdata/gateway/bin/sle_data_app --mode real
/userdata/gateway/bin/sle_data_app --mode mock
/userdata/gateway/bin/sle_data_app --mode hybrid
```

## 当前文档

- `使用说明.md`：启动模式、真实链路、Mock 调试、两路 Root 测试和常用 ADB 检查。
- `命令对接阅读理解.md`：ThingsKit 下行到命令 IPC、`ipc_cmd_receiver`、`sle_cmd_handler` 的阅读路径。
- `架构分析与改造计划.md`：`sle_data_app` 架构演进和后续改造方向。
- `Modbus寄存器仿真规格.md`：Mock/脚本侧 Modbus 响应和寄存器规格。
- `architecture.md`、`connection_flow.md`、`server_connections.md`、`sle_params.md`、`test_plan.md`：早期 SLE client 和连接参数设计记录，保留作为实现背景。

## 维护规则

- `main.c` 启动模式、socket、日志路径、真实/Mock 行为变更时，优先更新 `使用说明.md`。
- 命令 IPC 协议、handler、真实 SLE write 落地时，优先更新 `命令对接阅读理解.md`。
- SLE 连接参数、断连原因、压测结论变化时，同步更新本目录和 `app/Gateway/docs/10_项目复盘/Gateway上板测试记录.md`。
