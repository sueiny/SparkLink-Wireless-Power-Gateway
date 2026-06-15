---
type: log
area: project-review
tags:
  - gateway/retrospective
  - gateway/board-test
---

# Gateway 上板测试记录

日期：2026-06-13  
板端：ADB 设备 `299998dbc1021de4`  
目标路径：`/userdata/gateway`

## 测试目标

验证 `run-gateway` skill 中定义的构建、推送、板端运行和 MQTT/SQLite/SLE IPC 链路。

## 执行命令

```bash
cd /home/sueiny/rk3506_linux6.1_v1.2.0/app/Gateway
bash .claude/skills/run-gateway/driver.sh full
```

## full 流程结果

已通过：

- `sle_data_app` 交叉编译通过，产物约 22160 bytes。
- `gatewayd` CMake 交叉编译通过，产物约 701100 bytes。
- 测试数据生成通过：11 帧，`test_payload.bin` 364 bytes。
- `ipc_send` 测试工具编译通过。
- ADB push 通过：二进制、配置、things_model、测试文件均已推送。
- gatewayd 启动后 MQTT 连接成功，脚本检测到 `cloud_connected`。

编译警告：

- `sle_data_app/src/modbus_sim.c` 中 `write_float_as_regs` 未使用。

失败点：

```text
[SEND] read 364 bytes from /userdata/gateway/test/test_payload.bin
connect: No such file or directory
```

## 失败原因

gatewayd 实际监听的是抽象 Unix Socket：

```text
@var/run/gateway/sle_data.sock
```

`gatewayd/test/ipc_send.c` 使用普通文件系统路径连接 `/var/run/gateway/sle_data.sock`，所以报 `No such file or directory`。

这不是 gatewayd 没有启动监听，而是测试发送工具和运行期 IPC 语义不一致。

## 补充验证

为了验证真实运行期链路，手动启动板端 `sle_data_app`：

```bash
adb shell "nohup /userdata/gateway/bin/sle_data_app > /tmp/sle_data_app.out 2>&1 &"
```

观察结果：

- `sle_data_app` mock 线程生成 11 个业务设备帧和 31 个 DTU 心跳帧。
- `/tmp/sle_data_app.out` 出现 `[IPC] connected to gatewayd`。
- gatewayd 日志出现：

```text
[INFO][SLE-IPC] batch collected 64 devices
[INFO][MQTT] publish success kind=telemetry, topic=v1/gateway/telemetry, bytes=6606
```

网关状态上报显示：

```json
{"cache_count":21428,"cloud_connected":1,"device_count":11,"gateway_version":"1.0.0","network_ifname":"wlan0","network_type":"wifi"}
```

SQLite 检查：

- 测试时 `telemetry_cache` 仍有两万多行历史缓存。
- 最新 payload 长度约 6612 bytes，内容包含 DTU 拓扑与设备遥测。

## 结论

主链路可用：

```text
sle_data_app mock
  -> 抽象 Unix Socket
  -> gatewayd SLE IPC worker
  -> telemetry_queue
  -> PublishManager
  -> MQTT
```

需要修复的不是主链路，而是 `driver.sh test` 里的 `ipc_send` 工具。后续应让 `ipc_send` 支持抽象 socket，或直接用 `sle_data_app` mock 链路作为上板测试入口。

## 2026-06-14 修复记录

- `gatewayd/test/ipc_send.c` 已改为兼容普通文件系统 socket 和抽象 Unix Socket。
- `driver.sh test` 原命令行无需变更：继续传 `/var/run/gateway/sle_data.sock`，工具会在普通连接失败后自动尝试抽象 socket。
- `driver.sh test` 已补充启动前清理旧 `gateway.log`，并在发送测试帧前等待 `/proc/net/unix` 中出现 `@var/run/gateway/sle_data.sock`，避免误读历史 `cloud_connected` 后过早发送。

执行结果：

```text
bash .claude/skills/run-gateway/driver.sh build-ipc
bash .claude/skills/run-gateway/driver.sh push
bash .claude/skills/run-gateway/driver.sh test
```

已通过：

- `ipc_send` 输出 `[SEND] filesystem socket failed, connected as stripped abstract socket`。
- gatewayd 日志出现 `SLE-IPC batch collected 11 devices`。
- 测试 payload 已进入 `telemetry_cache`，网络恢复后日志显示缓存补传成功。

命令下发补充验证：

- 已启动 `sle_data_app`，板端 `/proc/net/unix` 显示命令 socket：`@/var/run/gateway/sle_cmd.sock`。
- 尝试用本机临时 MQTT client 直接 publish 到 `v1/devices/me/rpc/request/<id>`，broker 允许连接但拒绝 response topic 订阅，gatewayd 日志未收到该 request。
- 结论：直接 MQTT client 模拟 ThingsKit 下发受平台 ACL/路由限制，本轮未用该方式复测 `set_relay`；实际页面/平台下发仍按前次随机测试结论执行。

## 测试后状态

- 手动启动的 `sle_data_app` 已停止，避免继续生成 mock 数据。
- `gatewayd` 仍保持运行，和 `driver.sh test` 默认行为一致。
- 后续复核时 `telemetry_cache` 已继续下降到 11168 行，说明历史缓存补传仍在进行。

## 2026-06-15 真实 DTU Root 全拓扑压测

目标：按 `docs/00_项目说明/设备拓扑图.md` 覆盖 31 个 DTU 节点和 11 个外接设备，验证真实 DTU root 经 SLE 到 gatewayd 再到 ThingsKit MQTT 的上云链路。

本轮改动：

- `dtu_root_run_sender.py` 新增 `--scenario topology-all`，一轮发送 31 个 DTU heartbeat 和 11 个外接设备 DATA。
- 外接设备 DATA 覆盖 `METER_001~007`、`ENV_001~002`、`RELAY_001~002`，Modbus CRC 由脚本生成。
- `gatewayd` 新增 telemetry 批次日志：`telemetry batch devices=..., ids=...`，用于 MQTT 直连时确认设备 ID 覆盖。
- 修复 gatewayd IPC receiver：200ms 无数据不再误判为 client 断开，避免 `notify_printer` 未满 64 帧等待 1s flush 时后半批被关闭连接丢弃。

执行命令：

```bash
bash .claude/skills/run-gateway/driver.sh build-sle
bash .claude/skills/run-gateway/driver.sh build-gw
bash .claude/skills/run-gateway/driver.sh push
bash .claude/skills/run-gateway/driver.sh test-real-listen
```

Windows 串口压测：

```powershell
cd C:\Temp\GatewayTest
py -3 .\dtu_root_run_sender.py COM19 COM23 COM36 --scenario topology-all --duration 60 --interval 5 --line-delay 0.02 --warmup-sec 5 --warmup-interval 0.2 --warmup-text 12123213 --post-warmup-delay 8 --hold-open 10 --json-out .\topology_pressure_60s.json
```

结果：

- 三个 COM 口均完成发送：每口 11 轮、462 帧，总计 1386 帧。
- 串口脚本统计：`write_errors=0`，`crc_errors=0`，每口均覆盖 42 个设备 ID 和 31 个 DTU 节点。
- 板端本次实际 SLE active connection 为 2，因此 gatewayd 每完整轮收到 84 条 telemetry（42 个设备 ID 各 2 路）。
- 压力窗口内 gatewayd 出现 13 个 telemetry 发布批次，13 次 `publish success kind=telemetry`。
- 批次大小：`84,84,84,84,64,20,84,84,84,84,84,64,20`；`64+20` 为 IPC/发布窗口拆批，合计仍覆盖完整 84 条。
- 日志解析唯一设备 ID：42 个，缺失 ID：`none`，额外 ID：`none`。
- 错误关键字统计：`CRC error`、`modbus_parse_error`、`invalid frame`、`SLE-FRAME`、`SLE-DATA`、`notify queue dropped` 均为 0。
- SQLite `telemetry_cache` 为 0，说明 MQTT 已经通过 WiFi 直连上云，没有进入离线缓存。

代表性日志：

```text
[INFO][MQTT] telemetry batch devices=84, ids=DTU_001,...,DTU_031,METER_001,...,METER_007,ENV_001,ENV_002,RELAY_001,RELAY_002
[INFO][MQTT] publish success kind=telemetry, topic=v1/gateway/telemetry, bytes=6614
```

结论：

- 当前脚本模拟的全拓扑真实 SLE 上行链路已通过：42 个拓扑设备均进入 gatewayd telemetry，并成功发布到 `v1/gateway/telemetry`。
- 本轮发现并修复的 IPC timeout 误断连属于真实低流量和未满批场景的关键问题，修复后后半批 `METER_002~RELAY_002` 能稳定发布。
- 当前限制：板端本次只建立到 2 个真实 SLE root 的连接；COM 侧虽然三个端口都发送成功，但 gatewayd 侧按实际 SLE 连接收到两路副本。后续如需验证 3 路或 5 个 root 并发，需要先让对应 root 进入 SLE active connection。

## 2026-06-15 两路真实 Root 复测

目标：在当前板端实际只稳定接入两路 SLE root 的条件下，收敛到可用的两路串口组合，验证两路并发全拓扑上云。

执行前观察：

- `test-real-listen` 启动后，SLE active MAC 为 `12:a2:a3:a4:a5:a2` 和 `12:a2:a3:a4:a5:a3`。
- `COM19+COM23` 两路压测可上云，但后续单口复测显示 `COM19` 只确认串口写入，未形成新的 SLE/MQTT 上云批次，不作为当前两路基线。
- `COM23` 和 `COM36` 单口均能触发 42 设备完整上云，因此选为当前两路组合。

正式两路压测命令：

```powershell
cd C:\Temp\GatewayTest
py -3 .\dtu_root_run_sender.py COM23 COM36 --scenario topology-all --duration 60 --interval 5 --line-delay 0.02 --warmup-sec 5 --warmup-interval 0.2 --warmup-text 12123213 --post-warmup-delay 8 --hold-open 10 --json-out .\topology_pressure_2way_com23_com36_60s.json
```

串口侧结果：

- `COM23`：11 轮、462 帧、`write_errors=0`、`crc_errors=0`，覆盖 42 个设备 ID 和 31 个 DTU 节点。
- `COM36`：11 轮、462 帧、`write_errors=0`、`crc_errors=0`，覆盖 42 个设备 ID 和 31 个 DTU 节点。
- 合计发送 924 帧，其中 heartbeat 682 帧，外接设备 DATA 242 帧。

板端结果：

- SLE RX 最终计数接近均衡：`12:a2:a3:a4:a5:a2=686`，`12:a2:a3:a4:a5:a3=690`。
- 本轮 `sle_data_app` 启动和压测窗口内出现 4 次 `DISCONNECTED`，原因包含 `0x7` 和 `0x11`，但均能恢复连接。
- gatewayd 压力窗口内出现 13 个 telemetry 发布批次，13 次 `publish success kind=telemetry`。
- 批次大小包括 `84`、`64`、`20`，其中 `64+20` 是发布窗口拆批，合计仍为两路完整拓扑。
- SQLite `telemetry_cache=0`，说明 MQTT 经 WiFi 直连上云成功。
- 错误关键字检查未发现 `CRC error`、`modbus_parse_error`、`invalid frame`、`SLE-FRAME`、`SLE-DATA`、`notify queue dropped`、`publish failed`。

结论：

- 当前可作为两路真实 Root 上云基线的组合是 `COM23+COM36`。
- gatewayd 的 SLE IPC、Modbus 解析、设备映射、MQTT 发布在两路并发全拓扑压力下通过。
- 剩余风险在 SLE 连接稳定性：启动和测试窗口仍有少量 `reason=0x7/0x11` 断连，需要后续从 root 固件广播/连接参数、SLE manager 重连策略继续排查。
