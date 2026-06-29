# 云到 DTU 上下行链路与包格式

> Status: Current link-flow reference.
> Authority: 云端到 `gatewayd`、IPC、`sle_data_app`、DTU root 的上下行链路说明以本文为入口。
> Superseded by: [ST帧对接规定.md](ST帧对接规定.md) for ST protocol fields; [gatewayd与sle_data_app通信机制.md](gatewayd与sle_data_app通信机制.md) for IPC details.
> Last verified against: `gatewayd/config/gateway_config.json`, command downlink source, and SLE IPC docs on 2026-06-28.

本文记录当前 `gatewayd`、IPC、`sle_data_app`、DTU root 之间的真实上下行流程、测试包格式，以及 root 初始化/拓扑上报阶段 gatewayd 能识别的数据格式。

边界固定如下：

- `gatewayd` 负责云端 MQTT、物模型、设备映射、Modbus 解析/封装、ST 帧封装、离线规则和命令结果。
- `sle_data_app` 只负责 SLE 连接管理、SLE notify 上行转发、IPC raw ST 下行写入，不解析业务命令，不构造 Modbus。
- DTU root 的树状转发策略不由 gatewayd 关心。gatewayd 只需要把完整 ST 帧和 Modbus 包打包正确，并把 raw ST 交给 `sle_data_app` 写到目标 root 连接。

## 1. 总体链路

### 1.1 上行链路

```text
DTU root / UART0 / 485
  -> SLE notify
  -> sle_data_app notify_printer
  -> sle_data_app data IPC client
  -> gatewayd SleDataSource / IpcReceiver
  -> SLE ST parser
  -> Modbus parser
  -> TelemetryData
  -> PublishManager
  -> ThingsKit MQTT / SQLite cache
```

当前数据 IPC socket：

```text
config: /var/run/gateway/sle_data.sock
actual abstract socket: @var/run/gateway/sle_data.sock
```

### 1.2 下行链路

```text
ThingsKit RPC / gateway command topic
  -> gatewayd MqttCloudClient
  -> CommandRouter
  -> CommandValidator
  -> CommandExecutor
  -> gatewayd 构造 Modbus RTU
  -> gatewayd 构造完整 ST DATA
  -> gatewayd command IPC client
  -> sle_data_app ipc_cmd_receiver
  -> sle_cmd_handler raw ST bridge
  -> sle_manager_write_st_frame()
  -> DTU root SLE
  -> DTU root UART0 打印 / 485 透传
```

当前命令 IPC socket：

```text
config: /var/run/gateway/sle_cmd.sock
actual abstract socket: @/var/run/gateway/sle_cmd.sock
```

注意：数据 socket 和命令 socket 都是 abstract Unix socket，但命名规则不同。数据通道去掉开头 `/`，命令通道保留开头 `/`。

## 2. 云端 MQTT 格式

### 2.1 遥测上行 topic

`gatewayd` 代子设备批量上报使用：

```text
v1/gateway/telemetry
```

payload 由 `ThingsKitCodec::buildGatewaySubDeviceTelemetryPayload()` 生成：

```json
{
  "METER_001": [
    {
      "ts": 1710000000000,
      "values": {
        "voltage": 220.3,
        "current": 5.2,
        "active_power": 1145,
        "power_factor": 0.96,
        "frequency": 50.01,
        "energy": 1010.5,
        "relay_status": 1,
        "online": 1,
        "dtu_id": 1
      }
    }
  ]
}
```

网关自身属性上报使用：

```text
v1/devices/me/attributes
```

### 2.2 命令下行 topic

当前支持两类命令入口：

| topic | 用途 | response topic |
| --- | --- | --- |
| `v1/devices/me/rpc/request/{request_id}` | ThingsKit RPC | `v1/devices/me/rpc/response/{request_id}` |
| `v1/gateway/commands/request` | 网关命令入口 | `v1/gateway/commands/response` |

`set_relay` 示例：

```json
{
  "requestId": "test-relay-open-001",
  "method": "set_relay",
  "deviceName": "RELAY_001",
  "params": {
    "state": 0,
    "relay_channel": 0
  }
}
```

也兼容精简属性命令：

```json
{
  "deviceName": "RELAY_001",
  "relay_state": 0
}
```

命令响应 payload：

```json
{
  "requestId": "test-relay-open-001",
  "targetDeviceId": "RELAY_001",
  "method": "set_relay",
  "success": true,
  "code": "OK",
  "message": "raw ST forwarded",
  "ts": 1710000000000,
  "data": {
    "result": 1,
    "message": "raw ST forwarded",
    "root_id": 101,
    "dst_node_id": 109,
    "st_len": 21
  },
  "deviceName": "RELAY_001"
}
```

## 3. SLE ST 帧格式

ST 帧是 gatewayd 与 DTU root 之间的运行期业务帧。上行由 root 或脚本发送，下行由 gatewayd 构造完整帧后交给 `sle_data_app` 转发。

强约束：gatewayd 与 root 对接层只使用 `DATA(0x02)`、`DTU_NETWORK_TOPOLOGY(0x05)`、`EXTERNAL_DEVICE_MAP(0x06)` 三种 ST 帧。`HEARTBEAT(0x01)`、`TOPO_SUMMARY(0x03)`、`DEPTH_UPDATE(0x04)` 只作为 DTU Tree 内部协议或旧版说明，不进入后续 gateway-root 主链路。完整规定见 `ST帧对接规定.md`。

### 3.1 固定帧头

帧头固定 13 字节，小端字段：

| 偏移 | 长度 | 字段 | 当前值/说明 |
| --- | ---: | --- | --- |
| 0 | 2 | magic | `53 54`，ASCII `ST` |
| 2 | 1 | version | `01` |
| 3 | 1 | frame_type | gateway-root 主链路只允许 `02=DATA`、`05=DTU_NETWORK_TOPOLOGY`、`06=EXTERNAL_DEVICE_MAP` |
| 4 | 1 | src_role | `01=root`，`02=relay`，`03=leaf`，`04=gateway` |
| 5 | 2 | src_node_id | 发送方 node id，小端 |
| 7 | 2 | dst_node_id | 目标 node id，小端；gateway 为 `0` |
| 9 | 2 | seq | 序号，小端 |
| 11 | 2 | payload_len | payload 字节数，小端 |
| 13 | N | payload | 按 `frame_type` 解释 |

长度限制：

- `SLE_FRAME_MAX_LEN = 1024`
- `SLE_FRAME_MAX_PAYLOAD = 1011`

### 3.2 DATA payload

用于外接设备遥测上行，也用于控制下行。

```text
[0-N] modbus_rtu
```

DATA payload 是完整 Modbus RTU，不再携带 `modbus_type` 和 `modbus_len`。gatewayd 上行解析时通过 ST `src_node_id` 查当前 0x06 映射，再从配置 inventory 读取 `modbus_type`：

| 值 | 设备类型 | 当前映射 |
| --- | --- | --- |
| `02` | 单相电表 | `METER_*` |
| `03` | 温湿度变送器 | `ENV_*` |
| `04` | 继电器 | `RELAY_*` |

gatewayd 上行识别规则：

- `src_node_id` 先作为 DTU node id。
- 当前实现使用 `0x06` 动态外设映射查找外接设备；`gateway_config.json` 中的外设 `dtu_id/dtu_node_id` 只属于旧静态测试模式，不作为 `root_report` 正式路径依据。
- 找到外接设备后按 `modbus_type + modbus_rtu` 解析属性。
- 找不到外接设备但找到 DTU 时，作为 DTU 节点遥测。

### 3.3 `0x05/0x06` 动态拓扑 payload

`0x05` payload 为完整 DTU 网络拓扑文本，`0x06` payload 为 `DTU_xxx-DEVICE_xxx` 外接设备映射文本。详细规则见 `ST帧对接规定.md`，实现计划见 `ST动态拓扑上报帧05_06设计与改造计划.md`。

## 4. Modbus 上行响应格式

所有 Modbus RTU 帧尾都是 CRC16-Modbus，小端发送：`crc_low, crc_high`。

### 4.1 单相电表 `modbus_type=02`

功能码：`0x04`，输入寄存器响应。

```text
[0] slave_addr
[1] function = 04
[2] byte_count = 16
[3-18] data，8 个寄存器
[19-20] crc
```

寄存器解释：

| 寄存器 | 字节 | 字段 | 缩放 |
| --- | --- | --- | --- |
| 0 | 0-1 | voltage | `raw * 0.1 V` |
| 1 | 2-3 | current | `raw * 0.01 A` |
| 2 | 4-5 | active_power | `raw W` |
| 3 | 6-7 | power_factor | `raw * 0.001` |
| 4 | 8-9 | frequency | `raw * 0.01 Hz` |
| 5-6 | 10-13 | energy | `raw_u32 * 0.01 kWh` |
| 7 | 14-15 | relay_status | `0x55=1`，其它为 `0` |

当前测试帧，`METER_001`：

```text
ST:
535401020101000000200017000215010410089B0208047903C3138800018DA800008A3E

payload:
02 15 010410089B0208047903C3138800018DA800008A3E

modbus_rtu:
01 04 10 08 9B 02 08 04 79 03 C3 13 88 00 01 8D A8 00 00 8A 3E
```

### 4.2 温湿度 `modbus_type=03`

功能码：`0x03`，保持寄存器响应。

```text
[0] slave_addr
[1] function = 03
[2] byte_count = 4
[3-4] humidity，raw * 0.1 %RH
[5-6] temperature，int16 raw * 0.1 degC
[7-8] crc
```

当前测试帧，`ENV_001`：

```text
ST:
53540102030800000027000B000309010304025F012E4A15

payload:
03 09 010304025F012E4A15

modbus_rtu:
01 03 04 02 5F 01 2E 4A 15
```

### 4.3 继电器状态 `modbus_type=04`

功能码：`0x01`，线圈状态响应。

```text
[0] slave_addr
[1] function = 01
[2] byte_count
[3] coil bits，bit0 -> relay_state
[4-5] crc
```

当前测试帧，`RELAY_001`：

```text
ST:
53540102030A000000290008000406010101005188

payload:
04 06 010101005188

modbus_rtu:
01 01 01 00 51 88
```

## 5. IPC 包格式

### 5.1 数据上行 IPC

数据 IPC 是 `sle_data_app -> gatewayd`，socket 为 `@var/run/gateway/sle_data.sock`。

每帧：

```text
[0-1] frame_len，小端
[2-N] frame_body，完整 ST 帧原始字节
```

限制：

- `frame_len` 不能为 0。
- `frame_len <= 1024`。
- `sle_data_app` 可一次 `writev` 连续发送最多 64 帧；gatewayd 仍按一帧一帧读取。
- `notify_printer` 对 UART/SLE 收到的 ASCII HEX 形态会先解码为二进制 ST，再进入 IPC。

测试 UART 写入格式：

```text
<dst_node_id> <ST_FRAME_HEX>\r\n
```

示例：

```text
0 535401020101000000200017000215010410089B0208047903C3138800018DA800008A3E\r\n
```

这里 `0` 表示目标 gateway 节点。`sle_data_app` 会把后面的十六进制文本还原为 ST 原始字节。

### 5.2 命令下行 IPC

命令 IPC 是 `gatewayd -> sle_data_app -> gatewayd`，socket 为 `@/var/run/gateway/sle_cmd.sock`。

请求帧外层：

```text
[0-1] frame_len，小端
[2-N] frame_body
```

请求帧体：

```text
[0]   frame_type = 01
[1-2] seq，小端
[3]   dtu_id，业务目标 DTU ID
[4]   method，RAW_ST_DOWNLINK = 100
[5-6] param_len，小端
[7-N] param_data
```

`RAW_ST_DOWNLINK` 参数：

```text
[0-1] root_id，小端，用于选择 SLE root 连接
[2-3] dst_node_id，小端，必须等于 ST 帧头 dst_node_id
[4-5] st_frame_len，小端
[6-N] st_frame，完整 ST 帧，最大 1024 字节
```

响应帧体：

```text
[0]   frame_type = 02
[1-2] seq，小端，与请求匹配
[3]   result_code，0=OK，1=FAILED，2=TIMEOUT，3=UNSUPPORTED
[4-5] data_len，小端
[6-N] data，JSON 文本
```

成功响应示例：

```json
{"result":1,"message":"raw ST forwarded","root_id":1,"dst_node_id":1,"st_len":26}
```

## 6. 下行控制包格式

### 6.1 ST 下行头

下行 ST 由 gatewayd 构造：

| 字段 | 值 |
| --- | --- |
| magic | `53 54` |
| version | `01` |
| frame_type | `02`，DATA |
| src_role | `04`，gateway |
| src_node_id | `00 00` |
| dst_node_id | 目标外设当前挂载的 DTU |
| payload | `modbus_rtu` |

当前 builder 使用 IPC meta `root_id` 选择 SLE root 连接，同时 ST `dst_node_id=target_dtu_id`。root 固件按目标 DTU 自动转发，gatewayd 不在 DATA payload 内额外携带设备类型或长度。

### 6.2 Meter 拉闸/合闸

目标：`METER_*`，`modbus_type=02`。

Modbus RTU：

```text
[addr] 10 00 10 00 01 02 [value_hi] [value_lo] [crc_lo] [crc_hi]
```

| state | value | 含义 |
| --- | --- | --- |
| `0` | `AA AA` | 拉闸 |
| `1` | `55 55` | 合闸 |

当前配置下 meter 拉闸 ST 示例：

```text
53540102040000650000000B0001100010000102AAAA5A1F
```

拆解：

```text
ST header: 53 54 01 02 04 00 00 65 00 00 00 0B 00
payload:   01 10 00 10 00 01 02 AA AA 5A 1F
RTU:       01 10 00 10 00 01 02 AA AA 5A 1F
```

### 6.3 Relay 断开/闭合

目标：`RELAY_*`，`modbus_type=04`。

Modbus RTU：

```text
[addr] 05 00 [channel] [value_hi] [value_lo] [crc_lo] [crc_hi]
```

| state | value | 含义 |
| --- | --- | --- |
| `0` | `00 00` | 断开 |
| `1` | `FF 00` | 闭合 |

当前协议不再为 relay 下行补齐 ST payload；payload 长度就是 Modbus RTU 长度。

当前配置下 relay 断开 ST 示例：

```text
535401020400006D0001700800010500000000CDCA
```

拆解：

```text
ST header: 53 54 01 02 04 00 00 6D 00 01 70 08 00
payload:   01 05 00 00 00 00 CD CA
RTU:       01 05 00 00 00 00 CD CA
```

当前配置下 relay 闭合 ST 示例：

```text
535401020400006D000270080001050000FF008C3A
```

拆解：

```text
ST header: 53 54 01 02 04 00 00 09 00 02 70 08 00
payload:   01 05 00 00 FF 00 8C 3A
RTU:       01 05 00 00 FF 00 8C 3A
```

## 7. 初始化与拓扑信息

### 7.1 当前拓扑配置

当前 `gateway_config.json` 已增加正式拓扑总量配置：

```json
"topology": {
  "source": "root_report",
  "expected_dtu_count": 69,
  "expected_external_device_count": 9
}
```

正式配置只表达 inventory；拓扑和外设挂载只来自 root 上报的 0x05/0x06 快照：

| root | 范围 | 说明 |
| --- | --- | --- |
| `DTU_001` | `DTU_001..DTU_009` | 外接设备树 |
| `DTU_010` | `DTU_010..DTU_069` | 纯 DTU 树 |

外接设备绑定：

| 设备 | DTU | modbus_type | modbus_addr |
| --- | --- | --- | --- |
| `METER_001..METER_007` | `DTU_001..DTU_007` | `02` | `01` |
| `ENV_001` | `DTU_008` | `03` | `01` |
| `RELAY_001` | `DTU_009` | `04` | `01` |

### 7.2 root 初始化上报的当前可用方式

后续 gateway-root 主链路初始化只使用两类拓扑上行：

| 上行类型 | frame_type | 是否产生遥测 | 用途 |
| --- | --- | --- | --- |
| DTU 网络拓扑 | `05` | 否 | 解析完整 DTU 父子关系。 |
| 外接设备映射 | `06` | 否 | 解析 DTU 与外接设备挂载关系。 |
| DATA | `02` | 是 | 外接设备数据，借 `src_node_id` 绑定到 DTU。 |

旧版压测脚本 `topology-all` 曾使用：

- 29 条 HEARTBEAT 覆盖当前配置中的 DTU 节点。
- 9 条 DATA 覆盖 `METER_001..METER_007`、`ENV_001`、`RELAY_001`。

按新规则，后续应改为先发 `0x05`、再发 `0x06`，之后只发 `0x02 DATA`。

### 7.3 topology-all 测试集合

Windows 侧命令：

```bash
py -3 dtu_root_run_sender.py COM19 COM23 COM36 \
  --scenario topology-all \
  --duration 60 \
  --interval 5 \
  --line-delay 0.02 \
  --warmup-sec 5 \
  --warmup-interval 0.2 \
  --warmup-text 12123213 \
  --post-warmup-delay 8 \
  --hold-open 10
```

单轮发送集合：

| 类别 | 数量 | 说明 |
| --- | ---: | --- |
| DTU_NETWORK_TOPOLOGY | 1 | `0x05` 完整 DTU 树文本 |
| EXTERNAL_DEVICE_MAP | 1 | `0x06` 外接设备映射文本 |
| DATA meter | 7 | METER_001..METER_007 |
| DATA env | 1 | ENV_001 |
| DATA relay | 1 | RELAY_001 |
| 合计 | 11 | 每个串口首轮 11 帧，后续周期通常只发 DATA |

### 7.4 root 上传外接设备清单的扩展建议

如果后续要求 root 在初始化时主动上传“DTU + 外接设备”完整清单，固定使用 `0x05` 文本树和 `0x06` 文本键值对，不再新增其他 gateway-root 拓扑帧。

当前代码未实现该扩展。实现完成前仍临时兼容 `gateway_config.json` 静态表；正式目标是以 `topology.source=root_report` 和 `0x05/0x06` 为准。

## 8. 测试与验收

### 8.1 启动真实链路监听

```bash
cd /home/sueiny/rk3506_linux6.1_v1.2.0/app/Gateway
bash .claude/skills/run-gateway/driver.sh test-real-listen
```

关键日志：

```bash
adb shell "tail -f /tmp/sle_app.log"
adb shell "tail -f /userdata/gateway/data/log/gateway.log"
```

上行通过标准：

- `sle_data_app` 出现 `[SLE][RX]`。
- ASCII HEX 输入时出现 `[SLE][DECODED]`。
- `gatewayd` 出现 `SLE-IPC batch collected N devices`。
- MQTT 出现 `publish success kind=telemetry`。
- `telemetry_cache` 不持续增长。

### 8.2 上行 dry-run 检查

```bash
python3 app/Gateway/sle_data_app/test/dtu_root_run_sender.py \
  --scenario topology-all \
  --dry-run \
  --preview-count 1
```

检查字段：

- `frame_hex` 必须以 `5354` 开头。
- `frame_type` 为 `2`、`5` 或 `6`。
- DATA 帧 `modbus_crc_ok=true`。
- `src_node` 和 `device_id/dtu_id` 与 `0x05/0x06` 动态拓扑对齐；实现完成前可临时对照 `gateway_config.json` 静态测试表。

### 8.3 下行 COM 验收

监听下行：

```bash
py -3 dtu_root_run_sender.py COM19 COM23 COM36 \
  --scenario meter-001 \
  --duration 20 \
  --interval 1 \
  --line-delay 0.02 \
  --warmup-sec 5 \
  --warmup-interval 0.2 \
  --warmup-text 12123213 \
  --post-warmup-delay 3 \
  --hold-open 40 \
  --no-heartbeat \
  --expect-downlink relay-open \
  --expect-dtu-id 109 \
  --expect-channel 0 \
  --stop-on-downlink-match
```

通过标准：

- `sle_data_app` 返回 `raw ST forwarded`。
- 日志出现 `[CMD][ST-RX] root_id=101 dst_node_id=109 st_len=21`。
- 日志出现 `[CMD][ST-TX] ... ret=0`。
- COM 侧解析到完整 ST，Modbus RTU CRC 正确。

## 9. 快速定位表

| 现象 | 优先检查 |
| --- | --- |
| SLE 有日志但 gatewayd 无数据 | `notify_printer` 是否把 ASCII HEX 解码为 ST；数据 IPC 是否连接 |
| gatewayd 收到但不上云 | `parseSleFrameHeader`、`parseSleDataPayload`、Modbus CRC、MQTT 连接 |
| 外接设备变成 DTU 节点 | `0x06` 是否包含该 DTU 的外设映射；静态测试模式下再查 `gateway_config.json devices[].dtu_id` |
| `0x05/0x06` 不产生 MQTT | 动态拓扑帧只更新运行期拓扑和外设映射，不直接产生遥测 |
| 下行 IPC 成功但 COM 无包 | root READY 连接、write handle、ST `dst_node_id` 和 IPC meta `root_id` 是否匹配当前 `0x05/0x06` 动态路由；当前协议不做 relay payload padding |
| `sle_data_app` 打印 unsupported method | gatewayd 未使用 `RAW_ST_DOWNLINK=100`，或旧二进制未更新 |
