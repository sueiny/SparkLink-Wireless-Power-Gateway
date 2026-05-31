# 命令下发边界 Code Review

> 日期：2026-05-11  
> 关注点：只解析命令，不执行服务，不主动上报事件

## 1. Review 结论

当前板端命令下发能力已经收敛为“边界验证”：

```text
订阅命令 Topic
    ↓
收到 MQTT payload
    ↓
解析 JSON
    ↓
提取 method / params / target
    ↓
按当前物模型校验服务名
    ↓
校验基础参数
    ↓
返回解析结果
```

当前不会执行任何真实动作。

## 2. 当前支持的命令入口

板端订阅：

```text
v1/devices/me/rpc/request/+
v1/gateway/commands/request
```

响应：

```text
v1/devices/me/rpc/response/{request_id}
v1/gateway/commands/response
```

学习点：

- `v1/devices/me/rpc/request/+` 更偏 ThingsBoard/ThingsKit 直连设备 RPC 风格。
- `v1/gateway/commands/request` 是网关命令风格。
- 当前两种入口都解析，但都不执行真实动作。

## 3. 当前解析字段

支持从请求中提取目标设备：

```text
device
deviceId
target
targetDevice
```

这些字段既可以在请求根对象，也可以在 `params` 中。

示例：

```json
{
  "method": "set_collect_cycle",
  "params": {
    "device": "DTU_001",
    "cycle_ms": 5000
  }
}
```

如果不传目标设备，则默认目标为网关自身。

## 4. 当前服务白名单

服务名按当前物模型判断：

| 目标类型 | 允许的服务 |
|----------|------------|
| 网关 | `reboot`、`ota_upgrade` |
| 单相电表 | `set_relay`、`clear_energy` |
| 继电器 | `set_relay`、`set_mode` |
| DTU 节点 | `reboot`、`set_collect_cycle`、`trigger_collect` |
| 温湿度变送器 | 无服务 |

这符合当前模型设计：温湿度只有属性和事件，没有服务。

## 5. 当前参数校验

已做基础校验：

| 服务 | 参数要求 |
|------|----------|
| `ota_upgrade` | `url` 必须存在且为字符串 |
| `set_collect_cycle` | `cycle_ms` 必须为正整数 |
| `set_relay` | `state` 必须为整数 |
| `set_mode` | `mode` 必须为整数 |
| `reboot` | 无特殊参数要求 |
| `trigger_collect` | 无特殊参数要求 |
| `clear_energy` | 无特殊参数要求 |

注意：当前只是基础类型校验，没有做业务范围强校验。例如 `state` 是否只能是 0/1，后续执行层实现时应补上。

## 6. 当前响应语义

当命令合法时，返回：

```json
{
  "result": true,
  "method": "set_collect_cycle",
  "target": "DTU_001",
  "deviceType": "dtu_node",
  "message": "parsed only, command execution is reserved"
}
```

这句话非常重要：

```text
parsed only, command execution is reserved
```

它表示命令只被解析和校验，没有执行。

## 7. 明确不做的内容

当前不做：

1. 不重启网关。
2. 不执行 OTA。
3. 不修改 DTU 采集周期。
4. 不触发真实采集。
5. 不控制电表拉合闸。
6. 不清电量。
7. 不控制继电器。
8. 不因为服务解析成功而上报事件。

## 8. Review 风险

### 风险 1：平台可能把 `result=true` 理解为执行成功

当前 `result=true` 表示命令解析合法。为了降低误解，响应里已经加入：

```text
parsed only, command execution is reserved
```

如果后续平台侧要求严格区分，可以把字段扩展为：

```json
{
  "parsed": true,
  "executed": false,
  "result": true
}
```

### 风险 2：命令协议字段名可能变化

当前兼容了 `device/deviceId/target/targetDevice`。如果平台最终确定固定字段，应在后续收窄，避免协议长期过宽。

### 风险 3：服务执行层不要直接塞进 `GatewayApp`

后续如果要真正执行命令，建议新增轻量执行层，例如：

```text
CommandRouter
    负责把 method + target + params 分发给对应处理函数
```

`GatewayApp` 保持编排职责，不直接写重启、OTA、Modbus 写寄存器等底层逻辑。

