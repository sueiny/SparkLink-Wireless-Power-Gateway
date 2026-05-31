# ThingsKit 上传 Payload Code Review

> 日期：2026-05-11  
> 关注点：`MockDataSource`、`ThingsKitMapper`、`GatewayApp` 上传路径

## 1. Review 结论

当前上传格式已经从早期单设备直连格式，调整为符合当前 ThingsKit 网关测试方式的格式：

| 数据 | Topic | Payload 形态 |
|------|-------|--------------|
| 网关属性 | `v1/devices/me/attributes` | 平铺属性对象 |
| 网关遥测 | `v1/devices/me/telemetry` | 平铺属性对象 |
| 子设备遥测 | `v1/gateway/telemetry` | `deviceName: [{ts, values}]` |

这个调整是必要的。因为现在 `dtu网关` 使用自己的凭证登录，meter/env/relay/DTU 节点作为网关子设备由网关代发数据。

## 2. 学习点：为什么子设备 payload 要带 `ts/values`

当前子设备上报格式：

```json
{
  "METER_001": [
    {
      "ts": 1710000000000,
      "values": {
        "voltage": 220.1,
        "current": 3.2
      }
    }
  ]
}
```

这样做的好处：

1. 外层 key 是平台中的子设备名称。
2. 每个设备可以一次上报多条时间序列。
3. `values` 中只放物模型属性标识符。
4. 与测试脚本 `thingskit_tree_test.py` 的行为一致。

## 3. 各产品 payload 对齐情况

### 3.1 网关

当前字段：

```text
network_type
network_ifname
cloud_connected
device_count
cache_count
gateway_version
```

这些字段与 `gateway_model.json` 属性一致。

### 3.2 单相电表

当前字段：

```text
voltage
current
active_power
power_factor
frequency
energy
relay_status
meter_role
parent_meter_id
branch_power_sum
power_loss
loss_rate
online
```

Review 判断：

- `relay_status` 使用 BOOL，符合当前物模型。
- `meter_role` 使用 BOOL，`true` 表示总表，`false` 表示支表。
- `meter_loss` 等旧字段已去除。
- 支表也上报 `branch_power_sum/power_loss/loss_rate = 0`，避免同产品页面字段为空。

### 3.3 温湿度变送器

当前字段：

```text
temperature
humidity
online
```

与 `env_sensor_model.json` 一致。

### 3.4 继电器

当前字段：

```text
relay_state
control_mode
online
```

与 `relay_device_model.json` 一致。

### 3.5 DTU 节点

当前字段：

```text
role
mac
name
online
uptime
topology
collect_config
```

其中 `topology` 和 `collect_config` 是 STRUCT：

```json
{
  "topology": {
    "parent_mac": "00:11:22:33:44:55",
    "child_count": 2,
    "child_macs": "AA:BB:CC:DD:02:01,AA:BB:CC:DD:03:01"
  },
  "collect_config": {
    "modbus_count": 1,
    "collect_cycle": 5000,
    "addr_1": 1,
    "type_1": 2
  }
}
```

Review 判断：

- 新模型中 DTU 不再使用旧的扁平字段 `parent_mac/child_count/modbus_count/collect_cycle`。
- 这些内容已经收进 `topology` 和 `collect_config`。
- `type_1` 已按 meter/env/relay 区分。

## 4. 已验证内容

已完成：

```bash
cmake --build app/Gateway/gatewayd/build-cmake --parallel
python3 app/Gateway/gatewayd/things_model/model_scripts/thingskit_tree_test.py 1
```

验证结果：

- `gatewayd` 编译通过。
- MQTT access token 连接成功。
- 成功上传 1 轮测试数据。
- 设备数为 22 台。

## 5. 仍需注意

1. 当前数据源仍是 `MockDataSource`，不是 DTU/Modbus 真实数据。
2. 事件没有主动上报。
3. 服务没有真实执行。
4. ThingsKit 页面能否展示字段，仍取决于平台侧产品物模型是否已手动导入并发布。

