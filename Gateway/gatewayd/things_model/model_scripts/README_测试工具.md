# ThingsKit 物模型测试工具

## 测试脚本清单

| 脚本 | 功能 | 使用场景 |
|------|------|----------|
| `thingskit_model_sync.py` | 物模型同步工具 | 同步本地物模型到平台 |
| `thingskit_model_test.py` | 物模型格式测试 | 验证数据格式正确性 |
| `thingskit_mqtt_test.py` | MQTT实际测试 | 测试真实数据收发 |

## 使用方法

### 1. 物模型同步

```bash
cd /home/sueiny/rk3506_linux6.1_v1.2.0/app/Gateway/scripts

# 预览模式
python3 thingskit_model_sync.py --dry-run --user 1 --password 'Sztu@123456'

# 正式同步
python3 thingskit_model_sync.py --user 1 --password 'Sztu@123456'
```

### 2. 物模型格式测试

```bash
# 测试所有物模型格式
python3 thingskit_model_test.py
```

输出示例：
```
测试单相电表物模型
[1] 测试遥测数据上传
  发送遥测数据: {"voltage": 220.5, "current": 5.2, ...}
  [PASS] 遥测数据格式 - 格式正确

[2] 测试事件上报
  事件: 过压告警 - 数据: {"voltage": 250.5, "threshold": 242.0}
  [PASS] 事件格式 - 3个事件

[3] 测试服务调用
  服务: 拉合闸控制
    方法: set_relay
    参数: {"state": 1}
    响应: {"result": true}
  [PASS] 服务格式 - 2个服务

✅ 所有测试通过！
```

### 3. MQTT实际测试

```bash
# 安装依赖
pip3 install paho-mqtt

# 运行测试
python3 thingskit_mqtt_test.py
```

输出示例：
```
[OK] MQTT连接成功: thingskit.aiotcomm.com.cn:11883
[OK] 订阅主题: v1/devices/me/attributes
[OK] 订阅主题: v1/gateway/commands/request

测试单相电表: METER_MAIN_001
[发送遥测] 设备: METER_MAIN_001
  主题: v1/gateway/telemetry
  数据: {"voltage": 220.5, "current": 5.2, ...}
[OK] 消息发布成功: mid=4

✅ 所有测试通过！
```

## 测试内容

### 单相电表

| 类型 | 测试项 |
|------|--------|
| 遥测 | voltage, current, active_power, power_factor, frequency, energy, relay_status, meter_role, branch_power_sum, power_loss, loss_rate, online |
| 事件 | 过压告警, 欠压告警, 过流告警 |
| 服务 | 拉合闸控制(set_relay), 电量清零(clear_energy) |

### 温湿度传感器

| 类型 | 测试项 |
|------|--------|
| 遥测 | temperature, humidity, online |
| 事件 | 高温告警, 高湿告警 |

### 继电器

| 类型 | 测试项 |
|------|--------|
| 遥测 | relay_state, control_mode, online |
| 服务 | 开关控制(set_relay), 模式切换(set_mode) |

### 网关

| 类型 | 测试项 |
|------|--------|
| 属性 | network_type, network_ifname, cloud_connected, device_count, cache_count, gateway_version |
| 服务 | 重启(reboot), 固件升级(ota_upgrade) |

### DTU节点

| 类型 | 测试项 |
|------|--------|
| 遥测 | role, mac, name, online, uptime, parent_mac, child_count, child_macs, modbus_count, collect_cycle |
| 事件 | 节点离线, 设备离线, 采集失败, 采集周期变更, 拓扑变更 |
| 服务 | 重启(reboot), 设置采集周期(set_collect_cycle), 触发采集(trigger_collect) |

## 在ThingsKit平台验证

1. 登录 https://thingskit.aiotcomm.com.cn
2. 进入 设备管理 -> 设备
3. 查看设备遥测数据
4. 尝试下发命令

## 命令下发测试

在ThingsKit平台：
1. 选择设备
2. 点击"命令"或"控制"
3. 选择服务（如 set_relay）
4. 输入参数（如 state: 1）
5. 点击发送

脚本会收到并显示命令内容。
