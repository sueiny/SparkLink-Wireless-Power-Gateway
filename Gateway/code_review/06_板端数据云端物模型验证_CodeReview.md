# 板端数据云端物模型验证 Code Review

> 日期：2026-05-11  
> 目标：验证板端生成的数据发布到 ThingsKit 后，是否能落到对应物模型字段。

## 1. Review 结论

本阶段验证结论：当前属性数据与云端物模型字段对齐，关键设备回查通过。
并且已经通过 `make push` 部署到 RK3506 板端，由板端 `gatewayd` 实机主循环生成并发布数据。

已经确认：

| 产品 | 设备样例 | 结果 |
|------|----------|------|
| DTU 网关 | `dtu网关` | 通过 |
| 单相电表 | `METER_001`、`METER_002` | 通过 |
| 温湿度变送器 | `ENV_001` | 通过 |
| 继电器 | `RELAY_001` | 通过 |
| DTU 节点 | `DTU_001`、`DTU_008`、`DTU_010` | 通过 |

需要特别说明：PC 本机无法直接运行 `gatewayd` 板端二进制，因为该文件是 ARM hard-float ELF。
实际运行验证通过 ADB 在板端完成。验证方式分为三层：

1. 本地按 `MockDataSource` 与 `ThingsKitMapper` 的字段规则做物模型字段对照。
2. 使用同一套配置和同一套数据结构的测试脚本发布到 ThingsKit，并通过 HTTP API 回查云端最新遥测。
3. 使用 `make push` 推送板端程序后，通过 `adb shell` 运行板端 `gatewayd` 主循环并回查云端。

## 2. 本机无法直接运行板端二进制的原因

检测结果：

```text
gatewayd: ELF 32-bit LSB executable, ARM, EABI5, hard-float ABI
```

当前 PC 测试环境是 x86 Linux，没有可用 `qemu-arm`，所以不能在 PC 上直接执行：

```bash
app/Gateway/gatewayd/build-cmake/gatewayd
```

因此 PC 侧不直接运行板端二进制；板端运行通过 ADB 完成。

板端部署命令：

```bash
make -C app/Gateway/gatewayd push MOSQUITTO_ROOT=/tmp/gatewayd-real-mosq-lib
```

板端 smoke test：

```bash
adb shell '/userdata/gateway/bin/gatewayd --config /userdata/gateway/config/gateway_config.json --mqtt-test'
```

结果：

```text
MQTT connected
publish success topic=v1/devices/me/telemetry
publish success topic=v1/devices/me/attributes
```

板端主循环短测：

```bash
adb shell 'timeout 18s /userdata/gateway/bin/gatewayd --config /userdata/gateway/config/gateway_config.json'
```

结果：

```text
publish success topic=v1/gateway/telemetry
gateway sub-device telemetry published, devices=22
publish success topic=v1/devices/me/attributes
publish success topic=v1/devices/me/telemetry
```

## 3. 字段级校验

本地校验覆盖 22 个子设备：

```text
11 个 DTU
7 个单相电表
2 个温湿度变送器
2 个继电器
```

校验内容：

1. 每个设备生成的 payload key 必须存在于对应产品物模型。
2. 不允许出现旧字段或多余字段。
3. DTU 的 `topology` STRUCT 内部字段必须完整。
4. DTU 的 `collect_config` STRUCT 内部字段必须完整。

校验结果：

```text
payload_model_check=PASS
```

## 4. 云端上传验证

执行脚本上传：

```bash
python3 app/Gateway/gatewayd/things_model/model_scripts/thingskit_tree_test.py 1
```

结果：

```text
[OK] MQTT连接成功
[OK] 测试完成
```

随后通过 ThingsKit HTTP API 回查关键设备最新遥测，字段检查通过：

```text
cloud_key_check=PASS
```

随后执行板端上传并再次回查云端，字段检查同样通过：

```text
cloud_key_check=PASS
```

板端缓存检查：

```text
/userdata/gateway/data/cache/telemetry_pending.jsonl: 0 行
```

说明本次板端上报没有进入失败缓存。

## 5. 关键回查结果

### 5.1 DTU 网关

云端最新值包含：

```text
network_type
network_ifname
cloud_connected
device_count
cache_count
gateway_version
```

示例结果：

```json
{
  "network_type": "wifi",
  "network_ifname": "wlan0",
  "cloud_connected": "true",
  "device_count": "22",
  "cache_count": "0",
  "gateway_version": "1.0.0"
}
```

### 5.2 单相电表

`METER_001` 能看到总表字段：

```text
voltage
current
active_power
meter_role = true
relay_status = true
branch_power_sum
power_loss
loss_rate
online
```

`METER_002` 能看到支表字段：

```text
voltage
current
active_power
meter_role = false
parent_meter_id = METER_001
online
```

### 5.3 温湿度变送器

`ENV_001` 能看到：

```text
temperature
humidity
online
```

### 5.4 继电器

`RELAY_001` 能看到：

```text
relay_state
control_mode
online
```

### 5.5 DTU 节点

`DTU_001` 能看到电表类型：

```json
"collect_config": {
  "modbus_count": 1,
  "collect_cycle": 5000,
  "addr_1": 1,
  "type_1": 2
}
```

`DTU_008` 能看到环境类型：

```json
"collect_config": {
  "modbus_count": 1,
  "collect_cycle": 5000,
  "addr_1": 1,
  "type_1": 3
}
```

`DTU_010` 能看到继电器类型：

```json
"collect_config": {
  "modbus_count": 1,
  "collect_cycle": 5000,
  "addr_1": 1,
  "type_1": 4
}
```

这说明之前“所有 DTU 都显示单相电表”的问题已经修正。

## 6. 本次发现并修正的问题

### 问题：测试脚本网关网络类型和接口可能错配

原测试脚本分别随机生成：

```text
network_type
network_ifname
```

这可能产生：

```text
network_type = ethernet
network_ifname = wlan0
```

这种组合不符合真实板端行为。

修正方式：

```text
ethernet -> eth0
wifi -> wlan0
```

现在脚本成对生成网络类型和接口，重新上传后云端显示：

```json
{
  "network_type": "ethernet",
  "network_ifname": "eth0"
}
```

## 7. 当前边界

本次只验证属性数据上报与物模型字段对应关系。

没有验证：

1. 服务真实执行。
2. 事件主动上报。
3. 真实 DTU/WS73 数据链路。
4. 真实 Modbus 采集。
5. 长时间运行稳定性和断网补传压力测试。

这些内容属于后续阶段。
