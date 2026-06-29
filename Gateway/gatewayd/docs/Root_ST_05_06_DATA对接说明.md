# Root ST 05/06/DATA 对接说明

> Status: Current integration guide.
> Authority: Root 固件和 PC 模拟脚本的组帧、发送顺序、拒绝规则和验收日志以本文为入口。
> Superseded by: [ST帧对接规定.md](ST帧对接规定.md) for canonical ST field definitions.
> Last verified against: `gatewayd/config/gateway_config.json` and `sle_data_app/test/dtu_root_run_sender.py` on 2026-06-28.

本文面向编写 DTU root 固件或 PC 模拟脚本的人，说明 root 需要向 `gatewayd` 上报哪些 ST 帧、payload 如何组织、哪些情况会被网关拒绝。

核心约束：root 与 gatewayd 之间只使用三类 ST 帧：

| frame_type | 名称 | 方向 | 作用 |
| ---: | --- | --- | --- |
| `0x05` | `DTU_NETWORK_TOPOLOGY` | root -> gatewayd | 上报本 root 管辖的完整 DTU 拓扑快照。 |
| `0x06` | `EXTERNAL_DEVICE_MAP` | root -> gatewayd | 上报外接设备挂载在哪个 DTU 上。 |
| `0x02` | `DATA` | 双向 | 上行外设 Modbus 数据；下行控制 Modbus 数据。 |

禁止把 `0x01/0x03/0x04` 作为 gateway-root 主链路帧发给 `gatewayd`。

## 公共帧头

ST 帧头固定 13 字节，16-bit 字段全部小端。

| 偏移 | 长度 | 字段 | root 上报要求 |
| ---: | ---: | --- | --- |
| 0 | 2 | `magic` | 固定 `53 54`，ASCII `ST`。 |
| 2 | 1 | `version` | 固定 `01`。 |
| 3 | 1 | `frame_type` | 只允许 `02/05/06`。 |
| 4 | 1 | `src_role` | root 固定 `01`。 |
| 5 | 2 | `src_node_id` | 当前 root 节点 ID，必须存在于 `gateway_config.json` 的 DTU `node_id` inventory。当前配置示例为 `101` 或 `10`。 |
| 7 | 2 | `dst_node_id` | 上报 gateway 固定 `0`。 |
| 9 | 2 | `seq` | 递增序号，便于排查。 |
| 11 | 2 | `payload_len` | payload 字节数。 |
| 13 | N | `payload` | 按帧类型解释。 |

限制：

| 项 | 值 |
| --- | ---: |
| 最大 ST 帧长 | 1024 字节 |
| 帧头长度 | 13 字节 |
| 最大 payload | 1011 字节 |

root 必须保证实际 payload 长度与 `payload_len` 一致，不允许截断。

## 发送顺序

root 连接成功后推荐顺序：

1. 发送本 root 的完整 `0x05 DTU_NETWORK_TOPOLOGY`。
2. 发送本 root 的完整 `0x06 EXTERNAL_DEVICE_MAP`。
3. 周期性或事件触发发送外设 `0x02 DATA`。

拓扑或挂载关系变化时，不发增量，重新发送完整 `0x05` 和完整 `0x06`。

如果某 root 当前没有外接设备，可以发送 payload 长度为 0 的 `0x06` 空快照。

## 0x05 DTU_NETWORK_TOPOLOGY

`0x05` payload 是 ASCII 文本，每行一个 DTU 节点，用树形缩进表达父子关系。

示例：

```text
101
|- 102
|- 103
`- 109
```

### 文本规则

- 第一行必须是 root 节点 ID。
- 第一行 root ID 必须等于 ST 头 `src_node_id`。
- 每个非空行的行尾数字是 `node_id`。
- `|- ` 和 `` `- `` 表示树枝。
- 每一级缩进使用 3 个字符宽度，常见形式为 `|  ` 或三个空格。
- 同一帧内 `node_id` 不允许重复。
- 每个 `node_id` 必须存在于 `gateway_config.json` 的 DTU inventory。

gatewayd 会把该 root 的旧 `0x05` 快照整体替换为新快照，然后聚合所有 root 最新快照判断 DTU 在线状态。

### 拒绝条件

出现以下情况，gatewayd 拒绝整帧，并保留上一份有效快照：

- root 行与 ST 头 `src_node_id` 不一致。
- payload 为空或无法解析。
- 同一帧内 DTU 重复。
- 出现 `gateway_config.json` 中不存在的 DTU。
- 不同 root 的最新快照中出现同一个 DTU。
- 聚合 DTU 数量大于 `topology.expected_dtu_count`。

### 两路 root 示例

当前配置中，`DTU_001..DTU_009` 的 `node_id` 为 `101..109`，`DTU_010..DTU_069` 的 `node_id` 为 `10..69`。因此 `0x05/0x06/DATA` 里的 DTU 数字必须使用 `node_id`，不是 `device_id` 后缀。

root 101 示例：

```text
101
|- 102
|- 103
|- 104
|- 105
|- 106
|- 107
|- 108
`- 109
```

root 10 示例：

```text
10
|- 11
|- 12
...
`- 69
```

## 0x06 EXTERNAL_DEVICE_MAP

`0x06` payload 是 ASCII 文本，每行一个外接设备挂载关系。

格式：

```text
DTU_<node_id>-<external_device_id>
```

示例：

```text
DTU_101-METER_001
DTU_102-METER_002
DTU_103-METER_003
DTU_104-METER_004
DTU_105-METER_005
DTU_106-METER_006
DTU_107-METER_007
DTU_108-ENV_001
DTU_109-RELAY_001
```

### 文本规则

- `DTU_101` 表示 `node_id=101`；数字部分必须是当前配置里的 `node_id`，不是 `device_id` 后缀。
- `external_device_id` 必须存在于 `gateway_config.json` 外接设备 inventory。
- `DTU_<node_id>` 必须存在于 `gateway_config.json` DTU inventory。
- 同一个 DTU 可以挂多个外接设备，即多行使用同一个 `DTU_xxx`。
- 同一个外接设备不能在同一帧内重复，也不能跨 root 重复。

gatewayd 会把该 root 的旧 `0x06` 映射整体替换为新映射，然后聚合所有 root 最新映射判断外接设备在线状态。

### 在线判定

外接设备在线只由 `0x06` 映射和 `0x05` 拓扑共同决定：

```text
外设在线 = 06 中存在该外设映射 && 映射 DTU 在 05 聚合拓扑中在线
```

DATA 不改变在线状态。停止某外设 DATA，但 05/06 仍存在时，该外设仍保持在线。

### 离线原因

| reason | 含义 |
| --- | --- |
| `snapshot_present` | 06 有映射，且对应 DTU 在 05 中在线。 |
| `snapshot_missing` | JSON inventory 中存在该外设，但当前聚合 06 没有该外设。 |
| `parent_dtu_offline` | 06 有映射，但对应 DTU 不在当前聚合 05 在线拓扑中。 |

### 拒绝条件

出现以下情况，gatewayd 拒绝整帧，并保留上一份有效映射：

- 行格式不是 `DTU_xxx-DEVICE_ID`。
- `DTU_` 前缀缺失。
- DTU ID 不是正整数。
- 外接设备 ID 不在 JSON inventory 中。
- DTU ID 不在 JSON DTU inventory 中。
- 同一帧内外接设备重复。
- 不同 root 最新 `0x06` 中外接设备重复。
- 聚合外接设备数量大于 `topology.expected_external_device_count`。

## 0x02 DATA

`DATA` payload 是二进制：

```text
[0-N] modbus_rtu
```

| 字段 | 说明 |
| --- | --- |
| `modbus_rtu` | 完整 Modbus RTU 帧，包含 CRC16-Modbus 小端尾部。 |

### 上行 DATA

root 上报外接设备数据时：

- ST 头 `src_node_id` 填产生数据的 DTU 节点 ID，不是一定填 root ID。
- ST 头 `dst_node_id` 填 `0`。
- `modbus_rtu` 必须能被 gatewayd 的 Modbus parser 解析。

gatewayd 根据当前 `0x06` 动态映射查找 `src_node_id` 对应的外接设备，再从 JSON inventory 读取该外设的 `modbus_type/modbus_addr/type` 元数据。若没有 06 映射、DTU 不在名单、DTU 不在当前 05 在线拓扑，或同一 DTU 映射多个外设导致纯 RTU 无法区分，DATA 会被丢弃，不会自动生成 DTU 设备或作为 DTU 遥测上报。

### 下行 DATA

gatewayd 下发控制时：

- ST 头 `src_role=04`，`src_node_id=0`。
- `dst_node_id` 为目标外设当前挂载的 DTU 节点。
- payload 是纯 `modbus_rtu`。
- root 不需要理解 meter/relay 业务含义，只负责按 `dst_node_id` 自动转发完整 ST DATA 帧或打印到 COM 供验收。

当前下行动作：

| 设备 | 功能 | Modbus |
| --- | --- | --- |
| meter | 拉闸 | 功能码 `0x10`，写寄存器 `0x0010=0xAAAA`。 |
| relay | 断开/闭合 | 功能码 `0x05`，线圈默认 `0x0000`，`0000` 断开，`FF00` 闭合。 |

## 组帧伪代码

```c
// 16-bit little endian writer
void put_u16le(uint8_t *p, uint16_t v) {
    p[0] = v & 0xff;
    p[1] = (v >> 8) & 0xff;
}

size_t build_st_frame(uint8_t *out,
                      uint8_t frame_type,
                      uint8_t src_role,
                      uint16_t src_node_id,
                      uint16_t dst_node_id,
                      uint16_t seq,
                      const uint8_t *payload,
                      uint16_t payload_len) {
    out[0] = 'S';
    out[1] = 'T';
    out[2] = 0x01;
    out[3] = frame_type;
    out[4] = src_role;
    put_u16le(out + 5, src_node_id);
    put_u16le(out + 7, dst_node_id);
    put_u16le(out + 9, seq);
    put_u16le(out + 11, payload_len);
    memcpy(out + 13, payload, payload_len);
    return 13 + payload_len;
}
```

root 上报时：

```c
build_st_frame(buf, 0x05, 0x01, root_id, 0, seq, topo_text, topo_len);
build_st_frame(buf, 0x06, 0x01, root_id, 0, seq, map_text, map_len);
build_st_frame(buf, 0x02, 0x01, dtu_id, 0, seq, data_payload, data_payload_len);
```

## PC 脚本模拟

PC 脚本可模拟 root 上报，但当前 `dtu_root_run_sender.py` 内置 `topology-all` 仍按旧 `1..9/10..69` 节点生成，和当前配置中 `DTU_001..DTU_009 = node_id 101..109` 不一致。用于当前配置验收前，需要先同步脚本测试数据或使用自定义 replay/scenario 输入。

```powershell
py -3 app/Gateway/sle_data_app/test/dtu_root_run_sender.py COM19 COM23 ^
  --port-root COM19=1 ^
  --port-root COM23=10 ^
  --scenario topology-all ^
  --duration 60 ^
  --interval 5
```

`topology-all` 会按顺序发送：

1. 旧脚本 root 1 和 root 10 的 `0x05`。
2. 旧脚本 root 1 和 root 10 的 `0x06`。
3. 9 个外接设备的 `0x02 DATA`。

## gatewayd IPC 注入测试

如果绕过 `sle_data_app`，直接用 `gatewayd/test/ipc_send` 注入到 gatewayd 的 SLE data IPC，需要注意 IPC 文件格式不是裸 ST 帧，而是：

```text
uint16_le frame_len + ST_FRAME
```

裸 ST 帧直接发给 `/var/run/gateway/sle_data.sock` 会被 gatewayd 当成非法长度，例如 `invalid frame length: 21587`。

发送命令示例：

```bash
adb shell "/userdata/gateway/test/ipc_send /var/run/gateway/sle_data.sock /userdata/gateway/test/topology_cases/gateway_topology_full.bin"
```

## 网关侧验收日志

正常完整快照：

```text
[SLE-TOPO] ST 0x05 accepted root=101, observed_dtu=9, expected_dtu=69, complete=0
[SLE-TOPO] ST 0x06 accepted root=101, observed_external=9, online_external=9, expected_external=9, complete=1
[SLE-TOPO] ST 0x05 accepted root=10, observed_dtu=69, expected_dtu=69, complete=1
[SLE-TOPO] ST 0x06 accepted root=10, observed_external=9, online_external=9, expected_external=9, complete=1
[MQTT] telemetry batch coalesced raw=... unique=78
```

缺少 DTU：

```text
[SLE-TOPO] external online changed device_id=METER_007, online=0, dtu_id=107, reason=parent_dtu_offline
```

缺少外设映射：

```text
[SLE-TOPO] external online changed device_id=METER_007, online=0, dtu_id=0, reason=snapshot_missing
```

非法上报：

```text
[SLE-TOPO] ST 0x05 parse failed: topology references unknown DTU node_id=99
[SLE-TOPO] ST 0x06 references unknown external device_id=METER_999
[SLE-TOPO] ST 0x06 references unknown DTU node dtu_id=99, device_id=METER_001
[SLE-TOPO] ST 0x06 parse failed: duplicate external device id: METER_001
[SLE-TOPO] ST 0x06 duplicate external device across root snapshots device_id=METER_001
```

## 对接自检清单

- `src_node_id` 是否与 `0x05` payload 第一行 root ID 一致。
- `0x05` 是否上报完整快照，不是增量。
- `0x06` 是否上报完整映射，不是增量。
- 05/06 中的 DTU 是否全部存在于 `gateway_config.json` DTU inventory。
- 06 中的外接设备是否全部存在于 `gateway_config.json` 外设 inventory。
- 同一个外接设备是否只出现一次。
- ST 帧总长是否不超过 1024 字节。
- `DATA` 是否包含完整 Modbus RTU 和 CRC。
- 直接打 gatewayd IPC 时是否加了 2 字节长度前缀。
