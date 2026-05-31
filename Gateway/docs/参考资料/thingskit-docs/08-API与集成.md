# API与集成

## OpenAPI概述

ThingsKit提供开放的RESTful API，支持第三方系统集成。

## 认证方式

### Token认证
```
Authorization: Bearer {access_token}
```

### 获取Token
```
POST /api/auth/login
Content-Type: application/json

{
  "username": "admin",
  "password": "****"
}

Response:
{
  "token": "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9...",
  "refreshToken": "..."
}
```

## 设备管理API

### 获取设备列表
```
GET /api/v1/devices?page=1&pageSize=10&productId=xxx
```

### 获取设备详情
```
GET /api/v1/devices/{deviceId}
```

### 创建设备
```
POST /api/v1/devices
Content-Type: application/json

{
  "name": "sensor_001",
  "productId": "temp_sensor",
  "groupId": "group_001",
  "tags": {
    "location": "building_a",
    "floor": "3"
  }
}
```

### 删除设备
```
DELETE /api/v1/devices/{deviceId}
```

### 设备控制
```
POST /api/v1/devices/{deviceId}/commands
Content-Type: application/json

{
  "method": "set",
  "params": {
    "switch": true,
    "brightness": 80
  }
}
```

## 数据查询API

### 查询设备遥测数据
```
GET /api/v1/devices/{deviceId}/telemetry?keys=temperature,humidity&startTs=1699000000000&endTs=1699086400000&interval=3600&agg=AVG
```

### 查询设备属性
```
GET /api/v1/devices/{deviceId}/attributes
```

### 设置设备属性
```
POST /api/v1/devices/{deviceId}/attributes
Content-Type: application/json

{
  "reportInterval": 5000,
  "threshold": 35
}
```

## 规则引擎API

### 获取规则列表
```
GET /api/v1/rules?page=1&pageSize=10
```

### 创建规则
```
POST /api/v1/rules
Content-Type: application/json

{
  "name": "温度告警规则",
  "trigger": {...},
  "condition": {...},
  "action": [...]
}
```

### 启用/禁用规则
```
POST /api/v1/rules/{ruleId}/enable
POST /api/v1/rules/{ruleId}/disable
```

## 告警管理API

### 查询告警
```
GET /api/v1/alarms?status=active&severity=warning&page=1&pageSize=10
```

### 确认告警
```
POST /api/v1/alarms/{alarmId}/ack
Content-Type: application/json

{
  "comment": "已处理"
}
```

### 清除告警
```
POST /api/v1/alarms/{alarmId}/clear
```

## 用户与权限API

### 用户管理
```
GET    /api/v1/users          # 获取用户列表
POST   /api/v1/users          # 创建用户
PUT    /api/v1/users/{userId} # 更新用户
DELETE /api/v1/users/{userId} # 删除用户
```

### 角色管理
```
GET    /api/v1/roles          # 获取角色列表
POST   /api/v1/roles          # 创建角色
PUT    /api/v1/roles/{roleId} # 更新角色
```

### 权限分配
```
POST /api/v1/users/{userId}/roles
Content-Type: application/json

{
  "roles": ["admin", "operator"]
}
```

## Webhook集成

### 配置Webhook
```json
{
  "name": "告警通知Webhook",
  "url": "https://api.example.com/webhook",
  "method": "POST",
  "headers": {
    "Authorization": "Bearer xxx"
  },
  "events": ["alarm.created", "alarm.cleared", "device.online", "device.offline"]
}
```

### Webhook数据格式
```json
{
  "event": "alarm.created",
  "timestamp": 1699000000000,
  "data": {
    "alarmId": "alarm_001",
    "deviceId": "sensor_001",
    "severity": "warning",
    "message": "温度超标"
  }
}
```

## 第三方平台集成

### MQTT桥接
```json
{
  "name": "AWS IoT Bridge",
  "broker": "a1234567890-ats.iot.us-east-1.amazonaws.com",
  "port": 8883,
  "tls": true,
  "topics": [
    {"local": "devices/#", "remote": "thingskit/devices/#"}
  ]
}
```

### 阿里云IoT集成
```json
{
  "platform": "aliyun",
  "productKey": "xxx",
  "deviceName": "xxx",
  "deviceSecret": "xxx",
  "region": "cn-shanghai"
}
```

### 华为IoT集成
```json
{
  "platform": "huawei",
  "deviceId": "xxx",
  "nodeId": "xxx",
  "endpoint": "iot-mqtts.cn-north-4.myhuaweicloud.com"
}
```

## SDK

### Java SDK
```xml
<dependency>
  <groupId>com.thingskit</groupId>
  <artifactId>thingskit-client</artifactId>
  <version>2.0.0</version>
</dependency>
```

### Python SDK
```bash
pip install thingskit-client
```

### JavaScript SDK
```bash
npm install thingskit-client
```

## 错误码

| 错误码 | 说明 |
|--------|------|
| 200 | 成功 |
| 400 | 请求参数错误 |
| 401 | 未授权 |
| 403 | 权限不足 |
| 404 | 资源不存在 |
| 500 | 服务器内部错误 |
