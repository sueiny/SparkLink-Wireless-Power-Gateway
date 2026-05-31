# DTU 节点显示异常排查 Code Review

> 日期：2026-05-12  
> 问题：DTU 节点 `role` 收到值但页面不正常显示，`online=true` 但页面显示离线。

## 1. 排查结论

本次确认了一个板端和测试脚本之间的实际出入：

| 字段 | 测试脚本 | 修复前板端 | 修复后板端 |
|------|----------|------------|------------|
| `role` | `0` / `1` 整数 | `0.0` / `1.0` 浮点 | `0` / `1` 整数 |
| `uptime` | 整数 | 浮点 | 整数 |
| `relay_state` | 整数 | 浮点 | 整数 |
| `control_mode` | 整数 | 浮点 | 整数 |
| `online` | `true` 布尔 | `true` 布尔 | `true` 布尔 |

`role` 是 ThingsKit ENUM，`uptime` 是 INT。修复前板端统一用 `double` 承载数值，导致 ENUM/INT 上报成 `0.0/1.0/2450.0`，页面可能无法按枚举项匹配显示。

## 2. 修复内容

新增整数值通道：

```cpp
std::map<std::string, int64_t> integer_values;
```

`ThingsKitMapper` 在生成 JSON 时先写入 `integer_values`，确保整数仍然是 JSON number 的整数形态。

已调整字段：

```text
DTU.role
DTU.uptime
Relay.relay_state
Relay.control_mode
```

## 3. 板端实测结果

已重新编译并推送板端：

```bash
make -C app/Gateway/gatewayd push MOSQUITTO_ROOT=/tmp/gatewayd-real-mosq-lib
```

运行板端主循环后，日志中 DTU payload 已变为：

```json
{
  "role": 0,
  "uptime": 15,
  "online": true
}
```

云端 HTTP API 回查：

```json
{
  "role": "0",
  "uptime": "15",
  "online": "true"
}
```

说明 `role` 和 `uptime` 已经不再是 `0.0/15.0`。

## 4. 关于 online 显示离线

本次排查确认：

1. 本地 `dtu_node_model.json` 中 `online` 是 BOOL。
2. 平台上 `DTU节点` 产品的 `online` 物模型也是 BOOL。
3. 平台物模型中 `boolOpen=在线`、`boolClose=离线`，没有写反。
4. 测试脚本和板端发送的都是 JSON 布尔值 `true`。
5. 云端 HTTP API 回查到的最新值也是 `"true"`。

因此，如果 ThingsKit 页面仍把 DTU 节点显示为离线，当前更像是平台页面展示层、物模型导入缓存或该组件对 BOOL 值的显示映射问题，而不是板端 payload 与脚本不一致。

后续可做的验证：

1. 在 ThingsKit 页面重新进入 DTU 节点产品，确认 `online` 字段的 BOOL 映射是否已刷新。
2. 手动重新导入一次 DTU 节点物模型后再看页面展示。
3. 如果页面组件确实不识别 BOOL 的 `true/false`，再考虑把 DTU 节点 `online` 改成 ENUM：`0=离线`、`1=在线`。这属于物模型变更，需要平台侧重新导入确认。

## 5. 当前状态

已修复：

- `role` 枚举显示异常的板端发送类型问题。
- `uptime` INT 类型发送为浮点的问题。
- 继电器枚举字段发送为浮点的问题。

仍需平台侧确认：

- `online=true` 但页面显示离线的 BOOL 展示问题。

