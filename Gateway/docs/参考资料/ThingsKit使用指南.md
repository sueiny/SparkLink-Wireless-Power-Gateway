# ThingsKit 平台完整使用指南

## 1. 平台概述

ThingsKit 是企业级物联网平台，支持百万级设备并发接入。

## 2. 设备接入

### 2.1 支持的协议

| 协议 | 说明 | 适用场景 |
|------|------|----------|
| MQTT | 轻量级消息协议 | 传感器、低带宽设备 |
| HTTP | RESTful API | 云端对接、Web应用 |
| TCP | 原始TCP连接 | 自定义协议设备 |
| UDP | 无连接协议 | 实时性要求高的场景 |
| CoAP | 受限应用协议 | 低功耗物联网设备 |

### 2.2 MQTT身份认证

| 参数 | 值 | 说明 |
|------|-----|------|
| username | AccessToken | 设备创建后自动生成 |
| password | ProjectKey | 项目创建后自动生成 |
| clientId | 空或任意 | 不做限制 |

**注意**：同一设备凭证只能保持一个MQTT连接，新连接会顶掉旧连接。

## 3. 物模型

### 3.1 物模型组成

| 类型 | 说明 | 示例 |
|------|------|------|
| **属性（Property）** | 设备状态数据 | 温度、湿度、电压、电流 |
| **事件（Event）** | 设备主动上报 | 告警、故障、状态变更 |
| **服务（Service）** | 可调用功能 | 重启、配置更新、控制 |

### 3.2 物模型格式

**ThingsKit平台格式**：
```json
{
  "functionType": "properties",
  "functionName": "温度",
  "identifier": "temperature",
  "callType": null,
  "accessMode": "r",
  "eventType": null,
  "functionJson": {
    "dataType": {
      "type": "DOUBLE",
      "specs": {
        "unit": "°C"
      }
    }
  },
  "status": 1
}
```

**关键字段说明**：

| 字段 | 说明 | 必填 |
|------|------|------|
| functionType | properties/events/services | ✅ |
| functionName | 功能名称 | ✅ |
| identifier | 标识符（数据key） | ✅ |
| accessMode | r(只读)/rw(读写) | 属性必填 |
| eventType | ALERT/INFO/ERROR | 事件必填 |
| callType | SYNC/ASYNC | 服务必填 |
| functionJson | 数据定义 | ✅ |
| status | 1=启用 | ✅ |

### 3.3 ⚠️ 重要：物模型必须发布

**所有物模型都需要"发布"才能生效！**

发布步骤：
1. 进入产品详情
2. 点击"编辑物模型"
3. 添加/修改物模型后
4. 点击"发布"按钮

### 3.4 数据类型

| 类型 | 说明 | specs示例 |
|------|------|-----------|
| DOUBLE | 浮点数 | `{"unit": "V"}` |
| INT | 整数 | `{"unit": "count"}` |
| BOOL | 布尔值 | `{"boolOpen": "开", "boolClose": "关"}` |
| TEXT | 字符串 | `{"length": 64}` |
| ENUM | 枚举 | `{"0": "拉闸", "1": "合闸"}` |
| STRUCT | 结构体 | 包含子字段 |

## 4. MQTT Topic

### 4.1 直连设备和网关设备

| 方向 | Topic | 说明 |
|------|-------|------|
| 上行 | `v1/devices/me/telemetry` | 遥测数据 |
| 上行 | `v1/devices/me/attributes` | 属性数据 |
| 上行 | `v1/devices/me/events` | 事件上报 |
| 下行 | `v1/devices/me/rpc/request/+` | 命令下发 |

### 4.2 网关子设备

| 方向 | Topic | 说明 |
|------|-------|------|
| 上行 | `v1/gateway/telemetry` | 子设备遥测（网关代发） |
| 上行 | `v1/devices/me/attributes` | 网关自身属性 |
| 下行 | `v1/gateway/commands/request` | 子设备命令 |

## 5. 数据格式

### 5.1 直连设备遥测数据

**格式A（服务器时间）**：
```json
{
  "temperature": 25.5,
  "humidity": 60.2
}
```

**格式B（客户端时间，推荐数据补录）**：
```json
{
  "ts": 1710000000000,
  "values": {
    "temperature": 25.5,
    "humidity": 60.2
  }
}
```

### 5.2 网关子设备遥测数据

**格式A（服务器时间）**：
```json
{
  "METER_001": [
    {
      "voltage": 220.5,
      "current": 5.2
    }
  ],
  "METER_002": [
    {
      "voltage": 219.8,
      "current": 3.1
    }
  ]
}
```

**格式B（客户端时间）**：
```json
{
  "METER_001": [
    {
      "ts": 1710000000000,
      "values": {
        "voltage": 220.5,
        "current": 5.2
      }
    }
  ]
}
```

**注意**：key是子设备名称，value是数组格式。

### 5.3 属性数据

```json
{
  "network_type": "ethernet",
  "network_ifname": "eth0",
  "cloud_connected": true
}
```

## 6. 设备类型

| 类型 | 说明 | 适用场景 |
|------|------|----------|
| GATEWAY | 网关设备 | 汇聚子设备数据 |
| SENSOR | 传感器设备 | 网关子设备 |
| DIRECT_CONNECTION | 直连设备 | 独立连接平台 |

## 7. 网关子设备配置

### 7.1 创建流程

1. 创建网关产品（deviceType=GATEWAY）
2. 创建网关子设备产品（deviceType=SENSOR）
3. 在产品下创建设备
4. 建立子设备关系

### 7.2 重要注意事项

- 网关子设备与网关设备的**接入协议必须一致**
- 网关子设备数据通过网关的MQTT连接上报
- 子设备名称必须与MQTT数据中的key一致

### 7.3 建立子设备关系

```
POST /api/relation
Authorization: Bearer {token}

{
  "from": {"id": "网关ID", "entityType": "DEVICE"},
  "to": {"id": "子设备ID", "entityType": "DEVICE"},
  "type": "Contains",
  "typeGroup": "COMMON"
}
```

## 8. HTTP API

### 8.1 认证

```
POST /api/auth/login
Content-Type: application/json

{
  "username": "admin",
  "password": "password"
}

Response:
{
  "token": "eyJhbGciOiJIUzUxMiJ9..."
}
```

### 8.2 发送遥测数据

```
POST /api/plugins/telemetry/DEVICE/{deviceId}/timeseries/any
Authorization: Bearer {token}
Content-Type: application/json

{
  "values": {
    "voltage": 220.5,
    "current": 5.2
  }
}
```

### 8.3 查询遥测数据

```
GET /api/plugins/telemetry/DEVICE/{deviceId}/values/timeseries?keys=voltage,current
Authorization: Bearer {token}
```

## 9. 常见问题

### Q1: 物模型数据不显示
- **检查物模型是否已发布**（最重要！）
- 检查identifier是否与数据key一致
- 检查数据格式是否正确

### Q2: 设备显示离线
- 检查MQTT连接是否正常
- 检查keepalive设置
- 检查网络是否稳定

### Q3: 子设备不显示在网关下
- 检查gatewayId是否设置
- 检查设备关系是否创建
- 检查是否通过网关发送数据
- 检查接入协议是否一致

### Q4: 数据上报成功但不显示
- 检查物模型是否发布
- 检查数据key是否与identifier一致
- 检查数据类型是否匹配

## 10. 参考资源

- 官网: https://www.thingskit.com
- 文档: https://www.thingskit.com/docs
- API: https://thingskit.aiotcomm.com.cn/api

## 11. 项目脚本

```bash
# 同步物模型
python3 thingskit_tool.py sync

# 修复设备配置
python3 thingskit_tool.py fix

# 循环测试
python3 thingskit_tree_test.py 5

# 查看设备
python3 thingskit_tool.py devices
```
