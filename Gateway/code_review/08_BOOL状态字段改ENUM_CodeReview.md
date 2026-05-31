# BOOL 状态字段改 ENUM Code Review

> 日期：2026-05-12  
> 背景：ThingsKit 页面中 BOOL 字段显示不稳定，收到 `true` 但页面仍显示离线。

## 1. 结论

已将物模型中所有 BOOL 相关字段统一改为 ENUM，并同步修改板端 `gatewayd` 和测试脚本的上报值。

统一约定：

```text
0 = 否 / 关 / 离线 / 失败 / 支表 / 断开 / 拉闸
1 = 是 / 开 / 在线 / 成功 / 总表 / 已连接 / 合闸
```

## 2. 修改范围

### 2.1 物模型

以下产品已移除 BOOL：

| 产品 | 字段 |
|------|------|
| DTU 网关 | `cloud_connected`、服务 `result` |
| 单相电表 | `relay_status`、`meter_role`、`online`、服务 `state/result` |
| 温湿度变送器 | `online` |
| 继电器 | `online`、服务 `result` |
| DTU 节点 | `online`、服务 `result` |

平台侧同步后确认：

```text
DTU网关v1      NO_BOOL
单相电表       NO_BOOL
温湿度变送器   NO_BOOL
继电器         NO_BOOL
DTU节点        NO_BOOL
```

### 2.2 板端 gatewayd

新增/使用整数上报通道：

```cpp
integer_values
```

状态字段不再通过 `bool_values` 发送，而是统一发送 0/1。

示例：

```json
{
  "online": 1,
  "role": 0,
  "uptime": 15
}
```

### 2.3 测试脚本

已同步当前模型脚本中的测试 payload：

```text
online: 1
cloud_connected: 1
relay_status: 1
meter_role: 0/1
result: 0/1
```

## 3. 平台同步

执行同步：

```bash
python3 app/Gateway/gatewayd/things_model/model_scripts/thingskit_model_sync.py \
  --gateway-config app/Gateway/gatewayd/config/gateway_config.json \
  --model-dir app/Gateway/gatewayd/things_model \
  --user 1 \
  --password 'Sztu@123456'
```

结果：

```text
同步完成: 5/5 个产品成功
```

## 4. 板端验证

已重新编译并推送：

```bash
make -C app/Gateway/gatewayd push MOSQUITTO_ROOT=/tmp/gatewayd-real-mosq-lib
```

板端运行后日志确认：

```json
{
  "deviceId": "DTU_001",
  "values": {
    "online": 1,
    "role": 0,
    "uptime": 15
  }
}
```

## 5. 云端回查

回查结果：

```json
{
  "dtu网关": {
    "cloud_connected": "1"
  },
  "DTU_001": {
    "role": "0",
    "online": "1",
    "uptime": "15"
  },
  "DTU_002": {
    "role": "1",
    "online": "1",
    "uptime": "15"
  },
  "METER_001": {
    "meter_role": "1",
    "online": "1",
    "relay_status": "1"
  },
  "METER_002": {
    "meter_role": "0",
    "online": "1",
    "relay_status": "1"
  },
  "ENV_001": {
    "online": "1"
  },
  "RELAY_001": {
    "online": "1",
    "relay_state": "0",
    "control_mode": "0"
  }
}
```

## 6. 后续注意

1. 后续新增状态类字段时默认使用 ENUM，不再使用 BOOL。
2. 如果文档或脚本仍写 `true/false`，需要改为 0/1。
3. 服务执行真正落地时，服务返回 `result` 也应返回 0/1。

