---
type: note
area: technical-notes
tags:
  - gateway/technical
  - gateway/sle
  - gateway/runtime
---

# sle_data_app 使用说明

日期：2026-06-15  
范围：`app/Gateway/sle_data_app` 启动、真实 SLE 链路、Mock 调试、命令 IPC 和两路真实 Root 联调。

## 定位

`sle_data_app` 是 Gateway 的 C 侧 SLE 数据源进程。它负责接收真实 SLE notify 或生成 Mock notify，经过 `notify_printer` 批处理后，通过数据 IPC 发送给 `gatewayd`；同时它监听命令 IPC，接收 `gatewayd` 转发的云端命令并返回执行结果。

运行主链路：

```text
真实 DTU root / Mock
  -> SLE notify 队列
  -> notify_printer 批处理
  -> ipc_sender 数据 socket
  -> gatewayd SLE IPC worker
  -> Modbus 解析、设备映射、MQTT/SQLite
```

命令链路：

```text
ThingsKit RPC
  -> gatewayd command 模块
  -> IpcCmdSender 命令 socket
  -> sle_data_app ipc_cmd_receiver
  -> sle_cmd_handler
  -> gatewayd MQTT response
```

## 启动模式

当前默认模式是 `real`，即不带参数启动时使用真实 SLE manager。

```bash
/userdata/gateway/bin/sle_data_app
/userdata/gateway/bin/sle_data_app --mode real
```

模式说明：

| 模式 | 启动方式 | 用途 | 说明 |
| --- | --- | --- | --- |
| `real` | `sle_data_app` 或 `sle_data_app --mode real` | 真实 DTU root 联调和正式上行测试 | 启动 `sle_manager_init()`，不启动 `mock_data_generator`。 |
| `mock` | `sle_data_app --mode mock` | 无真实 Root 时验证 IPC、Modbus 解析、MQTT 上云 | 不启动真实 SLE manager，只生成本地模拟帧。 |
| `hybrid` | `sle_data_app --mode hybrid` | 排查阶段同时对比真实和 Mock 数据 | 同时启动真实 SLE 和 Mock，可能产生重复设备数据，不作为正式验收模式。 |

启动日志会打印：

```text
[SLE][STATUS] sle_data_app start mode=real raw_log=/tmp/sle_stack_raw.log
```

## 运行依赖

推荐先启动 `gatewayd`，再启动 `sle_data_app`。原因是 `sle_data_app` 的 `ipc_sender` 首次发送 batch 时连接 `gatewayd` 数据 socket；如果 `gatewayd` 未就绪，会进入重连路径，日志更难读。

关键路径：

| 项目 | 路径/关键字 | 用途 |
| --- | --- | --- |
| gatewayd 程序 | `/userdata/gateway/bin/gatewayd` | 主守护进程。 |
| sle_data_app 程序 | `/userdata/gateway/bin/sle_data_app` | SLE 数据源进程。 |
| gatewayd 配置 | `/userdata/gateway/config/gateway_config.json` | 包含 SLE enable、socket、MQTT、设备拓扑。 |
| 数据 socket | `@var/run/gateway/sle_data.sock` | `sle_data_app -> gatewayd` 上行数据通道，抽象 Unix Socket。 |
| 命令 socket | `@/var/run/gateway/sle_cmd.sock` | `gatewayd -> sle_data_app` 下行命令通道，抽象 Unix Socket。 |
| gatewayd 日志 | `/userdata/gateway/data/log/gateway.log` | MQTT、IPC、解析、发布日志。 |
| sle_data_app 进程输出 | `/tmp/sle_data_app.out` | 启动状态、命令接收、IPC 状态。 |
| SLE 应用日志 | `/tmp/sle_app.log` | SLE RX、连接、断连等日志。 |
| SLE raw 日志 | `/tmp/sle_stack_raw.log` | SDK stdout 原始日志。 |

常用 ADB 检查：

```bash
adb devices
adb shell "ps | grep -E 'gatewayd|sle_data_app' | grep -v grep"
adb shell "cat /proc/net/unix | grep -E 'sle_data|sle_cmd'"
adb shell "tail -80 /tmp/sle_data_app.out /tmp/sle_app.log /userdata/gateway/data/log/gateway.log"
adb shell "grep -E 'SLE-IPC batch collected|telemetry batch|publish success|cloud_connected' /userdata/gateway/data/log/gateway.log | tail -20"
adb shell "/userdata/gateway/bin/sqlite3 /userdata/gateway/data/gateway.db 'SELECT COUNT(*) FROM telemetry_cache;'"
```

## 真实两路 Root 测试

板端先进入真实监听：

```bash
cd /home/sueiny/rk3506_linux6.1_v1.2.0/app/Gateway
bash .claude/skills/run-gateway/driver.sh build-sle
bash .claude/skills/run-gateway/driver.sh build-gw
bash .claude/skills/run-gateway/driver.sh push
bash .claude/skills/run-gateway/driver.sh test-real-listen
```

`test-real-listen` 会停止旧进程，启动 `gatewayd`，等待 SLE 数据 socket 和 MQTT，再启动：

```bash
/userdata/gateway/bin/sle_data_app --mode real
```

Windows 串口侧使用 `py`，当前两路真实 Root 基线是 `COM23 + COM36`：

```powershell
cd C:\Temp\GatewayTest
py -3 .\dtu_root_run_sender.py COM23 COM36 --scenario topology-all --duration 60 --interval 5 --line-delay 0.02 --warmup-sec 5 --warmup-interval 0.2 --warmup-text 12123213 --post-warmup-delay 8 --hold-open 10 --json-out .\topology_pressure_2way_com23_com36_60s.json
```

脚本固定使用 `115200 8N1`。串口打开后 DTU 会复位，因此脚本在正式发送前会持续写入 warmup 文本，再延迟发送协议帧，避免初始化窗口内丢掉首批 ST 帧。

验收关键字：

```bash
adb shell "grep -E '\\[SLE\\]\\[RX\\]|SLE-IPC batch collected|telemetry batch|publish success' /tmp/sle_app.log /userdata/gateway/data/log/gateway.log | tail -80"
```

通过标准：

- `sle_app.log` 有对应 root 的 `[SLE][RX]` 计数增长。
- `gateway.log` 有 `SLE-IPC batch collected`。
- `gateway.log` 有 `telemetry batch devices=... ids=...`，包含 `DTU_001~DTU_031`、`METER_001~007`、`ENV_001~002`、`RELAY_001~002`。
- MQTT 有 `publish success kind=telemetry`。
- `telemetry_cache` 为 0 或可解释为离线缓存待补传。

## Mock 调试

没有真实 root 或需要先排除 gatewayd/MQTT 问题时，显式使用 Mock：

```bash
adb shell "killall -9 sle_data_app 2>/dev/null"
adb shell "nohup /userdata/gateway/bin/sle_data_app --mode mock > /tmp/sle_data_app.out 2>&1 &"
```

Mock 模式只证明本地 IPC、Modbus 解析、设备映射、MQTT/SQLite 链路可用，不证明真实 SLE 连接稳定性。

Mock 验证重点：

```bash
adb shell "grep -E '\\[IPC\\]|SLE-IPC batch collected|publish success' /tmp/sle_data_app.out /userdata/gateway/data/log/gateway.log | tail -40"
```

## 命令对接现状

`sle_data_app` 已有命令 socket 和 IPC 回包链路。`gatewayd` 可把 ThingsKit 命令转成 IPC 命令发给 `ipc_cmd_receiver`，再由 `sle_cmd_handler` 返回结果。

当前重要限制：

- `ipc_cmd_receiver` 是真实 IPC server。
- `sle_cmd_handler` 目前仍是模拟成功实现，日志为 `[CMD][MOCK]`。
- `set_relay`、`set_mode`、`set_collect_cycle`、`trigger_collect`、`reboot` 能进入 handler 并回包。
- 真正通过 SLE 写 root/DTU/Modbus 设备的命令尚未落地。
- `clear_energy`、`ota_upgrade` 当前在 `gatewayd` executor 中是 reserved/unsupported，不应作为真实下发验收项。

命令日志检查：

```bash
adb shell "grep -E '\\[CMD\\]|request_id|cmd sent|cmd response received|command_response' /tmp/sle_data_app.out /userdata/gateway/data/log/gateway.log | tail -80"
```

## 常见误判

| 现象 | 优先判断 | 排查方式 |
| --- | --- | --- |
| MQTT 未连云 | 先看网络接口，当前可用链路通常是 WiFi | 查 `cloud_connected`、`network_ifname=wlan0`、默认路由。 |
| 串口脚本发送成功但 gatewayd 无数据 | root 可能未形成 SLE active connection | 看 `/tmp/sle_app.log` 的 `[SLE][RX]` 和 active MAC。 |
| `env_sensor` 下发被拒绝 | 当前物模型没有 service | 按“不支持 service”处理，不判断为链路故障。 |
| 命令返回成功但设备未动作 | 当前 `sle_cmd_handler` 是 mock 成功 | 真实 SLE 写命令落地前不能作为真实下行验收。 |
| 批次出现 `64+20` | 正常拆批 | 两路全拓扑一轮 84 条，满 64 先 flush，剩余 20 时间窗口 flush。 |

## 关联文档

- [[20_技术沉淀/sle_data_app命令对接阅读理解]]
- [[20_技术沉淀/Gateway模块说明书]]
- [[10_项目复盘/Gateway上板测试记录]]
- [[00_项目说明/设备拓扑图]]
