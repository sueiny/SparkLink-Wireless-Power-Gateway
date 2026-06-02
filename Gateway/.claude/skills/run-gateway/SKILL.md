---
name: run-gateway
description: Build, deploy, and test the RK3506 Gateway (sle_data_app + gatewayd) on the board via adb
---

# Gateway (sle_data_app + gatewayd)

RK3506 网关项目：sle_data_app 负责 SLE 星闪数据采集透传，gatewayd 负责 Modbus 解析、ThingsKit MQTT 上云。

## Prerequisites

```bash
# ARM 交叉编译工具链（已含在 SDK 中）
ls prebuilts/gcc/linux-x86/arm/gcc-arm-10.3-2021.07-x86_64-arm-none-linux-gnueabihf/bin/arm-none-linux-gnueabihf-gcc

# buildroot sysroot（需要先构建 sqlite 和 mosquitto）
cd buildroot
PATH="/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin" make sqlite mosquitto
```

## Build & Deploy

使用驱动脚本一键构建和部署：

```bash
cd app/Gateway
bash .claude/skills/run-gateway/driver.sh build-sle     # 编译 sle_data_app
bash .claude/skills/run-gateway/driver.sh build-gw      # 编译 gatewayd
bash .claude/skills/run-gateway/driver.sh push           # 推送到板端
bash .claude/skills/run-gateway/driver.sh test           # 发送测试数据
bash .claude/skills/run-gateway/driver.sh full           # 全流程：build + push + test
```

## Run (agent path)

```bash
# 1. 编译并推送
bash .claude/skills/run-gateway/driver.sh full

# 2. 检查板端日志
adb shell "tail -20 /userdata/gateway/data/log/gateway.log"

# 3. 检查 MQTT 连接状态
adb shell "grep 'cloud_connected' /userdata/gateway/data/log/gateway.log | tail -3"

# 4. 检查缓存的遥测数据
adb shell "/userdata/gateway/bin/sqlite3 /userdata/gateway/data/gateway.db 'SELECT payload FROM telemetry_cache ORDER BY id DESC LIMIT 1'"
```

## Run (human path)

板端手动操作：

```bash
# 启动 gatewayd（SLE 模式）
adb shell "killall gatewayd; nohup /userdata/gateway/bin/gatewayd --config /userdata/gateway/config/gateway_config.json &"

# 发送测试数据
adb shell "/userdata/gateway/test/ipc_send /var/run/gateway/sle_data.sock /userdata/gateway/test/test_payload.bin"
```

## Gotchas

1. **PATH 含空格**：buildroot 拒绝含空格的 PATH，编译前必须 `export PATH="/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"`

2. **buildroot 缺依赖**：首次编译 gatewayd 需要先构建 sqlite 和 mosquitto 包，否则 CMake 找不到 libmosquitto

3. **buildroot 下载失败**：镜像可能 404，需手动下载 `make-4.3.tar.lz` 到 `buildroot/dl/make/`

4. **dhcpcd 路由冲突**：eth2 和 wlan0 同时 UP 时，dhcpcd 可能让 eth2 成为默认路由，导致 MQTT 连不上。gatewayd 的 NetManager 会在 connect 前临时切换路由

5. **mosquitto 2.0.18 API 变化**：sysroot 的 mosquitto.h 已包含 `mosquitto_message` 等声明，不要在代码中重复定义

6. **ipc_protocol.h 位置**：sle_data_app 不需要协议定义，只做透传。协议定义全部在 gatewayd 的 `include/codec/sle_frame_parser.h`

7. **板端无 Python**：测试数据在 host 端用 Python 生成二进制文件，推送到板端用 `ipc_send` 工具发送

## Troubleshooting

| 症状 | 原因 | 修复 |
|------|------|------|
| `libmosquitto not found` | buildroot 未构建 mosquitto | `cd buildroot && make mosquitto` |
| `common/ipc_protocol.h: No such file` | include 路径错误 | 已改为 `#include "ipc_protocol.h"` |
| `struct mosquitto_message redefinition` | 本地定义与 sysroot 冲突 | 删除 mqtt_cloud_client.cpp 中的本地定义 |
| `Connection timed out` (MQTT) | 默认路由走 eth2 | `ip route change default via 192.168.50.1 dev wlan0 metric 1008` |
| `connect: No such file or directory` (socket) | gatewayd 未启动或 socket 被清理 | 重启 gatewayd |
| `No such file: /proc/net/route` | 路由文件不存在 | 检查内核配置 |
