# gateway_config 配置说明

> Status: Current configuration reference.
> Authority: `gatewayd/config/gateway_config.json` 字段说明和启动校验规则以本文为入口。
> Superseded by: None.
> Last verified against: `gatewayd/config/gateway_config.json` and `src/config/config_manager.cpp` on 2026-06-28.

本文说明 `gatewayd/config/gateway_config.json` 的字段含义、现场修改方式和启动校验规则。当前正式现场模式是 `topology.source=root_report`：JSON 只保存全量设备名单、数量和设备元数据，不保存 DTU 父子拓扑，也不保存外接设备挂载关系。

## 配置边界

`gateway_config.json` 分三类信息：

| 类别 | 配置块 | 作用 |
| --- | --- | --- |
| 网关运行参数 | `gateway`、`thingskit`、`network`、`publish`、`log`、`sle` | 决定网关身份、MQTT、网络、发布、日志和 IPC。 |
| 动态拓扑策略 | `topology` | 决定 DTU/外设在线状态如何从 root 上报快照得到。 |
| 设备 inventory | `devices[]` | 全量 DTU 和外接设备名单，以及外接设备物模型/Modbus 元数据。 |

正式 `root_report` 模式下：

- DTU 在线和父子关系只来自 ST `0x05`。
- 外接设备挂载关系只来自 ST `0x06`。
- 外接设备在线条件是：`0x06` 存在该设备映射，且映射 DTU 在 `0x05` 中在线。
- `DATA(0x02)` 不参与在线判定。
- root 长时间不上报不会被 gatewayd 超时判离线；状态保持最后一份有效快照，直到下一份有效快照改变。

## gateway

```json
"gateway": {
  "gateway_id": "46dc3ebf25bf4cdb9cd01deb6092b7ef",
  "name": "dtu网关",
  "version": "1.0.0"
}
```

| 字段 | 说明 | 修改建议 |
| --- | --- | --- |
| `gateway_id` | 网关唯一 ID，也是云端识别网关的核心 ID。 | 换网关或换云端设备时修改；不能为空。 |
| `name` | 本地显示名。 | 只影响日志/显示。 |
| `version` | 网关软件版本。 | 发版时同步修改。 |

## thingskit

```json
"thingskit": {
  "protocol": "mqtt",
  "host": "thingskit.aiotcomm.com.cn",
  "port": 11883,
  "client_id": "46dc3ebf25bf4cdb9cd01deb6092b7ef",
  "credential_mode": "access_token",
  "access_token": "...",
  "keepalive": 60,
  "topic_prefix": "v1/devices/me"
}
```

| 字段 | 说明 | 校验/注意 |
| --- | --- | --- |
| `protocol` | 当前使用 MQTT。 | 目前代码按 MQTT 使用。 |
| `host` / `port` | MQTT broker 地址和端口。 | `host` 不能为空；`port` 必须是 `1..65535`。 |
| `client_id` | MQTT client id。 | 通常和 `gateway_id` 一致。 |
| `credential_mode` | 鉴权模式。 | 只允许 `access_token` 或 `mqtt_basic`。 |
| `access_token` | ThingsKit access token。 | `access_token` 模式不能为空。 |
| `basic_client_id/basic_username/basic_password` | MQTT basic 模式字段。 | `mqtt_basic` 模式下 client id 和 username 不能为空。 |
| `keepalive` | MQTT keepalive 秒数。 | 现场一般保持默认。 |
| `topic_prefix` | ThingsKit 设备主题前缀。 | 当前默认 `v1/devices/me`。 |

注意：`access_token` 属于现场凭据，公开文档和提交前要确认是否需要替换为测试 token。

## network

```json
"network": {
  "mode": "auto",
  "cloud_test_host": "thingskit.aiotcomm.com.cn",
  "cloud_test_port": 11883,
  "priority": ["ethernet", "wifi", "cellular"],
  "ethernet": {"enable": true, "ifname": "eth1"},
  "wifi": {"enable": true, "ifname": "wlan0", "ssid": "Sueiny", "password": "...", "country": "CN"},
  "cellular": {"enable": true, "ifname": "ppp0", "module": "L610", "serial_device": "/dev/ttyS1", "baudrate": 115200, "apn": "cmnet"}
}
```

| 字段 | 说明 | 校验/注意 |
| --- | --- | --- |
| `mode` | 网络模式。 | 只允许 `auto/ethernet/wifi/cellular`。当前配置为 `auto`。 |
| `priority` | `auto` 模式下选网优先级。 | 每项只允许 `ethernet/wifi/cellular`。 |
| `cloud_test_host/cloud_test_port` | 云端 TCP 探测目标。 | host 不能为空；port 必须是 `1..65535`。 |
| `ethernet.enable/ifname` | 以太网配置。 | 当前启用 `eth1`，在 `auto` 优先级中排第一。 |
| `wifi.enable/ifname/ssid/password/country` | Wi-Fi 配置。 | 当前启用 `wlan0`，SSID 为现场值。 |
| `cellular.*` | 4G/蜂窝配置。 | 当前启用，作为 `auto` 兜底路径。 |

网络模块只负责选网和路由/DNS 诊断；MQTT 是否连接成功最终仍由 `thingskit` 配置和云端连通性决定。

## publish

```json
"publish": {
  "interval_ms": 5000,
  "gateway_status_interval_ms": 10000,
  "cache_ttl_ms": 604800000,
  "enable_cache": true
}
```

| 字段 | 说明 | 校验/注意 |
| --- | --- | --- |
| `interval_ms` | 遥测批量发布周期。 | 必须大于 0。 |
| `gateway_status_interval_ms` | 网关状态发布周期。 | 必须大于 0。 |
| `cache_ttl_ms` | SQLite 缓存保留时间。 | 必须大于 0。 |
| `enable_cache` | MQTT 不可达时是否缓存。 | 建议现场保持 `true`。 |

发布前会按 `device_id` 合并同一窗口内的遥测，避免 `0x05/0x06/DATA` 同时产生重复设备记录。

## log

```json
"log": {
  "dir": "/userdata/gateway/data/log",
  "level": "info"
}
```

| 字段 | 说明 |
| --- | --- |
| `dir` | 板端日志目录。 |
| `level` | 日志等级，当前常用 `info`。 |

常用查看命令：

```bash
adb shell "tail -f /userdata/gateway/data/log/gateway.log"
```

## mock

```json
"mock": {
  "voltage_base": 220.0,
  "frequency_base": 50.0,
  "temperature_base": 28.0,
  "humidity_base": 60.0
}
```

`mock` 只用于 MockDataSource 或内部测试数据生成。正式 SLE/root_report 链路下，遥测值来自 root 上报的 ST `DATA(0x02)`。

## sle

```json
"sle": {
  "enable": true,
  "data_socket": "/var/run/gateway/sle_data.sock",
  "cmd_socket": "/var/run/gateway/sle_cmd.sock"
}
```

| 字段 | 说明 |
| --- | --- |
| `enable` | `true` 时使用 SLE IPC 数据源；`false` 时使用 mock 数据源。 |
| `data_socket` | `sle_data_app -> gatewayd` 上行数据 IPC socket。 |
| `cmd_socket` | `gatewayd -> sle_data_app` 下行命令 IPC socket。 |

当前配置没有 `sle.roots`。root 身份来自运行期 SLE 连接元数据和 `0x05/0x06` 上报，DTU/外设数量约束由 `topology.expected_*` 和 `devices[]` inventory 提供。

gatewayd 的 SLE data IPC 是抽象 Unix socket，调试工具可传 `/var/run/gateway/sle_data.sock`，但工具必须按 abstract namespace 连接。

## topology

```json
"topology": {
  "source": "root_report",
  "expected_dtu_count": 69,
  "expected_external_device_count": 9,
  "online_policy": {
    "dtu_from_topology_snapshot": true,
    "external_from_device_map": true,
    "external_inherits_dtu_online": true,
    "emit_online_change": true,
    "missing_dtu_online": false,
    "missing_external_online": false
  },
  "static_json": {
    "enable_for_test": false
  },
  "dynamic": {
    "required_frames": ["dtu_topology", "external_map"],
    "startup_timeout_ms": 30000,
    "persist_path": "/userdata/gateway/data/dynamic_topology.json",
    "allow_fallback_to_static": false
  }
}
```

### 正式模式

正式现场固定使用：

```json
"source": "root_report",
"static_json": {"enable_for_test": false},
"dynamic": {"allow_fallback_to_static": false}
```

启动校验要求：

- `expected_dtu_count` 必须大于 0，并且等于 JSON 中 DTU inventory 数量。
- `expected_external_device_count` 必须大于 0，并且等于 JSON 中外接设备 inventory 数量。
- `online_policy.dtu_from_topology_snapshot` 必须为 `true`。
- `online_policy.external_from_device_map` 必须为 `true`。
- `online_policy.external_inherits_dtu_online` 必须为 `true`。
- `online_policy.emit_online_change` 必须为 `true`。
- `static_json.enable_for_test` 必须为 `false`。
- `dynamic.allow_fallback_to_static` 必须为 `false`。
- DTU 条目不允许出现 `parent_id/child_ids`。
- 外接设备条目不允许出现 `dtu_id/dtu_node_id`。

### 在线判定

| 对象 | 在线条件 | 离线条件 |
| --- | --- | --- |
| DTU | 出现在所有 root 最新有效 `0x05` 快照聚合结果中。 | JSON DTU inventory 中存在，但当前聚合 `0x05` 未出现。 |
| 外接设备 | 出现在所有 root 最新有效 `0x06` 映射中，且映射 DTU 在 `0x05` 中在线。 | `0x06` 缺失映射，或映射 DTU 不在线。 |

离线原因：

| reason | 含义 |
| --- | --- |
| `snapshot_present` | 快照存在，且在线条件满足。 |
| `snapshot_missing` | JSON inventory 中存在，但快照缺失。 |
| `parent_dtu_offline` | 外设映射存在，但挂载 DTU 不在 05 在线拓扑中。 |

### 测试模式

只有内部测试时才允许：

```json
"topology": {
  "source": "static_json",
  "static_json": {
    "enable_for_test": true
  }
}
```

测试模式可使用 JSON 中的静态拓扑和挂载关系；不要用于正式现场。

## offline_analysis

```json
"offline_analysis": {
  "enable": true,
  "offline_only": true,
  "enter_hold_ms": 10000,
  "exit_hold_ms": 30000,
  "rule_engine": {...},
  "offline_control": {...},
  "ai": {...}
}
```

整体规则：

- `offline_only` 必须为 `true`。
- 规则引擎、本地 AI、离线自动控制只在云不可达/离线门控 active 后执行。
- 在线状态下跳过本地规则和 AI。

### rule_engine

```json
"rule_engine": {
  "enable": true,
  "cooldown_ms": 60000,
  "defaults": {
    "single_phase_meter": {
      "nominal_voltage_v": 220.0,
      "over_voltage_v": 235.4,
      "under_voltage_v": 198.0,
      "frequency_low_hz": 49.8,
      "frequency_high_hz": 50.2,
      "rated_current_a": 60.0,
      "over_current_ratio": 1.1,
      "hold_ms": 10000
    },
    "env_sensor": {
      "high_temperature_c": 55.0,
      "high_humidity_rh": 90.0,
      "hold_ms": 30000
    },
    "dtu_node": {
      "offline_timeout_ms": 60000
    }
  },
  "device_overrides": {
    "METER_001": {
      "rated_current_a": 80.0,
      "over_current_ratio": 1.2
    }
  }
}
```

校验规则：

- `cooldown_ms`、各类 `hold_ms` 必须大于 0。
- 电压、电流、频率、温湿度阈值必须为正数。
- `under_voltage_v < over_voltage_v`。
- `frequency_low_hz < frequency_high_hz`。
- `device_overrides` 中未填写的字段表示不覆盖默认值。

配置优先级：

```text
device_overrides > 设备类型 defaults > 代码内保底默认值
```

### offline_control

```json
"offline_control": {
  "enable": true,
  "offline_only": true,
  "relay_close_on_recovery": true,
  "relay_devices": ["RELAY_001"]
}
```

| 字段 | 说明 |
| --- | --- |
| `enable` | 是否允许规则触发离线本地控制。 |
| `offline_only` | 必须为 `true`。 |
| `relay_close_on_recovery` | 继电器模拟场景恢复时是否闭合。 |
| `relay_devices` | 允许联动控制的继电器设备 ID。必须是 relay 类型。 |

Meter 当前策略是异常时自动拉闸，不自动合闸；relay 用于断开/闭合演示。

### ai

```json
"ai": {
  "enable": false,
  "offline_only": true,
  "mode": "linear_score",
  "model_path": "/userdata/gateway/models/offline_ai_model.json",
  "window_ms": 300000,
  "min_samples": 12,
  "cooldown_ms": 300000,
  "risk_threshold_medium": 0.55,
  "risk_threshold_high": 0.8
}
```

校验规则：

- `offline_only` 必须为 `true`。
- `mode` 当前只允许 `linear_score`。
- `model_path` 不能为空。
- `window_ms/min_samples/cooldown_ms` 必须大于 0。
- 风险阈值必须满足 `0 < medium < high <= 1`。

模型文件缺失时 AI 应禁用并记录错误，不能影响遥测、规则、下行控制链路。

## devices

`devices[]` 同时保存外接设备 inventory 和 DTU inventory。

### 外接设备条目

```json
{
  "device_id": "METER_001",
  "product_id": "single_phase_meter",
  "name": "METER_001",
  "type": "single_phase_meter",
  "station_id": 1,
  "modbus_addr": 1,
  "modbus_type": 2
}
```

| 字段 | 说明 |
| --- | --- |
| `device_id` | 外设唯一 ID，必须唯一。 |
| `product_id` | 对应物模型产品 ID。 |
| `name` | 显示名。 |
| `type` | 设备类型：`single_phase_meter/env_sensor/relay_device`。 |
| `station_id` | 现场编号或演示编号。 |
| `modbus_addr` | Modbus 从站地址。 |
| `modbus_type` | 外设解析类型：`2` 电表，`3` 温湿度，`4` 继电器。DATA payload 本身不携带该字段。 |

正式 `root_report` 下禁止字段：

- `dtu_id`
- `dtu_node_id`

### DTU 条目

```json
{
  "device_id": "DTU_010",
  "type": "dtu_node",
  "node_id": 10
}
```

| 字段 | 说明 |
| --- | --- |
| `device_id` | DTU 设备 ID，建议固定 `DTU_%03d`。 |
| `type` | 固定 `dtu_node`。 |
| `node_id` | SLE/DTU 节点 ID，必须能与 ST `0x05/0x06` 中的 DTU ID 对应。 |

当前配置中 DTU inventory 为 69 个：`DTU_001..DTU_009` 的 `node_id` 为 `101..109`，`DTU_010..DTU_069` 的 `node_id` 为 `10..69`。文档和测试包不要假设 `DTU_001` 一定对应 `node_id=1`。

正式 `root_report` 下禁止字段：

- `parent_id`
- `child_ids`

这些关系只能由 root 上报的 ST `0x05` 决定。

## 常见修改

### 改 Wi-Fi

只改：

```json
"wifi": {
  "ssid": "现场SSID",
  "password": "现场密码"
}
```

### 改 DTU 总数

需要同步修改：

- `topology.expected_dtu_count`
- `devices[]` 中 `type=dtu_node` 的条目数量
- root 固件或 PC 脚本上报的 `0x05` 快照内容

三者不一致时，要么 gatewayd 启动失败，要么快照被判定不完整/拒绝。

### 改外接设备数量

需要同步修改：

- `topology.expected_external_device_count`
- `devices[]` 中外接设备条目数量
- root 上报的 `0x06` 外设映射

### 改外接设备挂在哪个 DTU

正式模式不要改 JSON。应由 root 的 `0x06` 上报新映射，例如：

```text
DTU_007-METER_007
```

### 改 DTU 父子拓扑

正式模式不要改 JSON。应由 root 的 `0x05` 上报新完整拓扑。

## 启动校验失败排查

常见错误和处理：

| 错误 | 原因 | 处理 |
| --- | --- | --- |
| `topology.source must be root_report/static_json` | 拓扑源写错。 | 改为 `root_report` 或 `static_json`。 |
| `expected_dtu_count must match configured DTU inventory count` | 期望 DTU 数和 JSON DTU 条目数不一致。 | 同步数量。 |
| `expected_external_device_count must match configured external inventory count` | 期望外设数和 JSON 外设条目数不一致。 | 同步数量。 |
| `forbids static DTU topology fields` | `root_report` 下 DTU 条目仍有 `parent_id/child_ids`。 | 删除这些字段。 |
| `forbids static external device mapping fields` | `root_report` 下外设条目仍有 `dtu_id/dtu_node_id`。 | 删除这些字段。 |
| `snapshot flags must be true` | 正式模式在线策略被关掉。 | 恢复四个 snapshot 策略为 `true`。 |
| `allow_fallback_to_static must be false` | 正式模式仍允许回退静态拓扑。 | 改为 `false`。 |

## 验证命令

构建：

```bash
bash app/Gateway/.claude/skills/run-gateway/driver.sh build-gw
```

板端查看配置是否被加载：

```bash
adb shell "tail -80 /userdata/gateway/data/log/gateway.log"
```

看到类似日志表示 inventory 加载成功：

```text
[SLE-DS] 69 DTU, 9 devices, socket=/var/run/gateway/sle_data.sock
```
