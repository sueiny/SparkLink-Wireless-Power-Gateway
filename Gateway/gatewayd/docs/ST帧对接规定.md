# ST 帧对接规定

> Status: Current authoritative protocol.
> Authority: 唯一 gateway-root ST 协议权威页；其他 ST 文档只保留实现说明、测试命令或历史背景。
> Superseded by: None.
> Last verified against: `gatewayd/config/gateway_config.json`, `src/codec/sle_frame_parser.cpp`, `src/datasource/sle_data_source.cpp` on 2026-06-28.

本文是 `gatewayd` 与 DTU root 对接的唯一 ST 帧规则文档，给 root 固件和网关侧实现共同使用。

## 1. 强制规则

`gatewayd` 与 root 之间只使用三种 ST 帧：

| frame_type | 名称 | 方向 | 用途 |
| ---: | --- | --- | --- |
| `0x02` | `DATA` | 双向 | 业务数据。上行承载外接设备 Modbus RTU；下行承载控制 Modbus RTU。 |
| `0x05` | `DTU_NETWORK_TOPOLOGY` | root -> gatewayd | root 上报完整 DTU 网络拓扑快照。 |
| `0x06` | `EXTERNAL_DEVICE_MAP` | root -> gatewayd | root 上报外接设备与 DTU 节点的挂载关系。 |

禁止在 gateway-root 对接层使用：

- `0x01 HEARTBEAT`
- `0x03 TOPO_SUMMARY`
- `0x04 DEPTH_UPDATE`

这些帧只允许存在于 DTU SLE Tree 内部协议说明中，不进入 `gatewayd` 主链路。

## 2. 公共 ST 帧头

所有帧共用 13 字节帧头，16-bit 字段均为小端。

| 偏移 | 长度 | 字段 | 说明 |
| ---: | ---: | --- | --- |
| 0 | 2 | magic | 固定 `53 54`，ASCII `ST`。 |
| 2 | 1 | version | 当前固定 `01`。 |
| 3 | 1 | frame_type | 只允许 `02`、`05`、`06`。 |
| 4 | 1 | src_role | root 上行为 `01`，gateway 下行为 `04`。 |
| 5 | 2 | src_node_id | 发送方节点 ID。root 上行为 root ID；gateway 下行为 `0`。 |
| 7 | 2 | dst_node_id | 目标节点 ID。root 上报 gateway 时为 `0`；下行时为目标 DTU。 |
| 9 | 2 | seq | 序号。 |
| 11 | 2 | payload_len | payload 字节数。 |
| 13 | N | payload | 按 `frame_type` 解释。 |

限制：

| 项 | 值 |
| --- | ---: |
| 最大帧长 | 1024 字节 |
| 帧头长度 | 13 字节 |
| 最大 payload | 1011 字节 |

如果 `0x05` 或 `0x06` 文本超过 1011 字节，root 必须等后续分片规则明确后再发送；当前规定不允许隐式截断。当前实现仍使用 ASCII-hex 文本上报，不引入二进制分片。

## 3. `0x02 DATA`

`DATA` 是唯一业务数据帧。

### 3.1 上行 payload

```text
[0-N] modbus_rtu
```

`modbus_rtu` 必须是完整 Modbus RTU 响应，包含从站地址、功能码、数据区和 CRC16-Modbus 小端尾部。

设备识别规则：

- ST 头 `src_node_id` 是产生数据的 DTU 节点 ID。
- `src_node_id` 必须存在于 JSON DTU inventory，且必须出现在当前有效 `0x05` 在线拓扑中。
- gatewayd 通过当前聚合 `0x06` 映射查找该 DTU 唯一挂载的外接设备。
- 外接设备的 `type/product_id/modbus_type/modbus_addr` 固定来自 JSON inventory，不从 DATA payload 读取。
- 若该 DTU 没有 `0x06` 映射、映射外设不在 inventory、或一个 DTU 映射多个外设导致纯 RTU 无法区分，gatewayd 丢弃该 DATA。

### 3.2 下行 payload

```text
[0-N] modbus_rtu
```

`gatewayd` 负责构造完整 Modbus RTU 和完整 ST DATA 帧；IPC meta 中的 root ID 用于选择 root 连接，ST 头 `dst_node_id` 写目标 DTU。root 固件只负责按 `dst_node_id` 在树内自动转发或在 UART/业务侧打印处理。

## 4. `0x05 DTU_NETWORK_TOPOLOGY`

`0x05` payload 是完整 DTU 网络拓扑快照文本，每行一个 DTU 节点。

当前配置可接受的双 root 示例片段：

```text
101
|- 102
|- 103
`- 109

10
|- 11
|- 12
`- 69
```

解析规则：

- 第一行是 root 节点。
- 每个非空行的行尾数字是 `node_id`。
- 缩进和 `|-` / `` `- `` 表示父子关系。
- 同一帧内 `node_id` 不允许重复。
- `0x05` 是完整快照，不是增量更新。

网关行为：

- 解析成功后，用该快照替换对应 root 的 DTU 动态拓扑。
- gatewayd 按 root 保存最新 `0x05` 快照，并聚合所有 root 最新快照。
- JSON 中的 DTU inventory 是全量名单和白名单；正式 `root_report` 配置只允许 `device_id/type/node_id`，不允许填写 `parent_id/child_ids`。
- DTU 父子关系只能来自最新有效 `0x05` 快照；配置文件中的静态父子关系只允许 `static_json` 内部测试模式使用。
- 出现在当前聚合 `0x05` 快照中的 DTU 判定为在线。
- JSON inventory 中存在、但未出现在当前聚合 `0x05` 快照中的 DTU 判定为离线。
- 聚合数量大于 `topology.expected_dtu_count` 时，拒绝本次快照并保留旧状态。
- `0x05` payload 第一行 root 必须与 ST 头 `src_node_id` 一致。
- `0x05` 出现 JSON DTU inventory 未登记的 `node_id` 时拒绝整帧。
- 不同 root 最新快照中出现重复 DTU 时拒绝导致重复的本次快照。
- root 长时间不上报不会触发 gatewayd 超时离线；在线状态保持上一份有效快照，直到下一份有效 `0x05` 改变状态。

## 5. `0x06 EXTERNAL_DEVICE_MAP`

`0x06` payload 是外接设备与 DTU 节点的挂载关系，每行一个键值对。

示例：

```text
DTU_101-METER_001
DTU_108-ENV_001
DTU_109-RELAY_001
```

解析规则：

```text
DTU_<node_id>-<external_device_id>
```

| 字段 | 说明 |
| --- | --- |
| `DTU_<node_id>` | 挂载外接设备的 DTU 节点，数字部分必须是 `gateway_config.json` 中的 `node_id`，不是 `device_id` 后缀。 |
| `external_device_id` | 外接设备 ID，必须存在于 `gateway_config.json` 的外设 inventory。 |

网关行为：

- gatewayd 按 root 保存最新 `0x06` 快照，并聚合所有 root 最新外设映射。
- JSON 中的外接设备 inventory 是全量名单和元数据白名单，包括 `type/product_id/modbus_type/modbus_addr`。
- 正式 `root_report` 配置不允许在外接设备条目中填写 `dtu_id/dtu_node_id`；外设挂载 DTU 只能来自最新有效 `0x06` 映射。
- `0x06` 只提供运行期挂载关系：哪个外接设备当前挂在哪个 DTU 节点。
- 外接设备在线条件固定为：该设备出现在当前聚合 `0x06` 映射中，且映射 DTU 出现在当前聚合 `0x05` 快照中。
- JSON inventory 中存在、但当前聚合 `0x06` 没有映射的外接设备判定为离线，`reason=snapshot_missing`。
- 当前聚合 `0x06` 有映射、但映射 DTU 不在线的外接设备判定为离线，`reason=parent_dtu_offline`。
- 当前聚合 `0x06` 有映射且映射 DTU 在线的外接设备判定为在线，`reason=snapshot_present`。
- 聚合数量大于 `topology.expected_external_device_count` 时，拒绝本次映射并保留旧状态。
- `0x06` 上报未知外设、未知 DTU 或重复外设时拒绝整帧，并保留上一份有效映射。
- 同一个 DTU 可以在 `0x06` 中出现多行，用于挂载多个外接设备；同一个外接设备不能重复出现。
- `DATA(0x02)` 不会改变外接设备在线状态；停止 DATA 但 05/06 仍存在时，该外接设备保持在线。

## 6. 在线判定

网关通过 `gateway_config.json` 提前知道全量 inventory 和期望总量：

```json
"topology": {
  "source": "root_report",
  "expected_dtu_count": 69,
  "expected_external_device_count": 9,
  "online_policy": {
    "dtu_from_topology_snapshot": true,
    "external_from_device_map": true,
    "external_inherits_dtu_online": true,
    "emit_online_change": true
  },
  "static_json": {
    "enable_for_test": false
  }
}
```

`devices[]` 和 DTU 条目提供全量名单、数量校验和设备元数据；`root_report` 正式模式不使用 JSON 的静态父子关系或外设挂载关系判断在线，并且启动校验会拒绝这些静态关系字段。

正式配置示例：

```json
{
  "devices": [
    {
      "device_id": "METER_001",
      "product_id": "single_phase_meter",
      "name": "METER_001",
      "type": "single_phase_meter",
      "station_id": 1,
      "modbus_addr": 1,
      "modbus_type": 2
    },
    {
      "device_id": "DTU_010",
      "type": "dtu_node",
      "node_id": 10
    }
  ]
}
```

当前配置中 DTU inventory 为 69 个：`DTU_001..DTU_009` 的 `node_id` 为 `101..109`，`DTU_010..DTU_069` 的 `node_id` 为 `10..69`。

注意：`0x05/0x06/DATA` 使用的是 DTU `node_id`。例如当前配置中设备 `DTU_001` 的 `node_id=101`，因此 `0x06` 应写 `DTU_101-METER_001`，不是 `DTU_001-METER_001`。

判定规则：

| 对象 | 在线条件 | 离线条件 |
| --- | --- | --- |
| DTU 节点 | 出现在所有 root 最新有效 `0x05` 快照的聚合结果中。 | JSON DTU inventory 中存在，但未出现在当前聚合 `0x05` 快照中。 |
| 外接设备 | 出现在所有 root 最新有效 `0x06` 映射中，且对应 DTU 出现在当前聚合 `0x05` 快照中。 | JSON 外设 inventory 中存在，但 06 缺失映射，或 06 映射的 DTU 不在线。 |

离线原因：

| reason | 对象 | 含义 |
| --- | --- | --- |
| `snapshot_present` | DTU/外设 | DTU 出现在 05；外设出现在 06 且对应 DTU 在线。 |
| `snapshot_missing` | DTU/外设 | JSON inventory 中存在，但当前 05 或 06 没有出现。 |
| `parent_dtu_offline` | 外设 | 06 有映射，但该外设挂载的 DTU 不在当前 05 在线拓扑中。 |

完整性和错误处理：

- `root_report` 模式下，JSON DTU 数量必须等于 `expected_dtu_count`，JSON 外设数量必须等于 `expected_external_device_count`。
- `root_report` 模式下，DTU 条目出现 `parent_id/child_ids` 或外设条目出现 `dtu_id/dtu_node_id` 时启动失败。
- `0x05` 或 `0x06` 聚合数量大于期望数量时拒绝导致超量的本次帧。
- `0x05` 出现未知 DTU 时拒绝整帧。
- `0x06` 出现未知外设、未知 DTU 或重复外设时拒绝整帧。
- 在线状态只在收到新的有效 `0x05/0x06` 快照时变化。
- 不做时间超时推断；不使用心跳、DATA 或 MQTT 状态判断 DTU/外接设备在线。
- 同一发布窗口内若 `0x05/0x06/DATA` 产生同一 `device_id` 多条遥测，gatewayd 发布前按 `device_id` 合并，后到字段覆盖先到字段。

## 7. 启动顺序

root 与 gatewayd 建立连接后应按顺序发送：

1. 各 root 发送自己的 `0x05 DTU_NETWORK_TOPOLOGY`。
2. 各 root 发送自己的 `0x06 EXTERNAL_DEVICE_MAP`。没有外接设备的 root 可以发送空 payload。
3. 周期性或事件触发的 `0x02 DATA`

后续拓扑变化时，root 重新发送完整 `0x05` 和完整 `0x06`，不发送增量。

## 8. 静态 JSON 模式

JSON 中的设备条目分两类使用：

- 正式 `root_report`：全量 inventory、数量校验、设备元数据白名单。
- 内部 `static_json`：允许直接使用 JSON 的静态拓扑和挂载关系做测试。

正式模式使用：

```json
"topology": {
  "source": "root_report",
  "static_json": {
    "enable_for_test": false
  }
}
```

只有内部无 root 或回归测试时，才允许切换：

```json
"topology": {
  "source": "static_json",
  "static_json": {
    "enable_for_test": true
  }
}
```

## 9. PC 脚本模拟

PC 脚本可模拟 root 上报，但当前 `dtu_root_run_sender.py` 的内置 `topology-all` 仍按旧 `1..9/10..69` 节点生成，和当前配置中 `DTU_001..DTU_009 = node_id 101..109` 不一致。用于当前配置验收前，需要先同步脚本测试数据或使用自定义 replay/scenario 输入。

```powershell
py -3 dtu_root_run_sender.py COM19 COM23 ^
  --port-root COM19=1 ^
  --port-root COM23=10 ^
  --scenario topology-all ^
  --duration 60 ^
  --interval 5
```

旧脚本 `topology-all` 发送顺序：

1. 每个 root 发送 `0x05` 拓扑快照。
2. 每个 root 发送 `0x06` 外设映射。
3. 发送 9 个外接设备的 `0x02 DATA`。

## 10. 版本边界

当前实现状态：

- `ConfigManager` 已加入 `topology` 配置解析和校验。
- `SleDataSource` 已支持 `0x05/0x06` 文本解析和动态在线判定。
- `dtu_root_run_sender.py topology-all` 已支持用 PC 脚本模拟 `0x05/0x06/0x02`。

后续待补：

- 动态拓扑持久化到 `topology.dynamic.persist_path`。
- `root_report` 模式下，已接入的真实下行控制通过当前 `0x05/0x06` 动态拓扑查询目标 DTU/root；未真实化的云命令仍按命令执行器支持范围处理。
