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
