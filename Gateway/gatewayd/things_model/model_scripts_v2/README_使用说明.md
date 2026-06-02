# ThingsKit 自动化工具使用说明

## 产品配置

| 产品名称 | 产品类型 | 物模型文件 |
|----------|----------|-----------|
| 单相电表 | 网关子设备 | single_phase_meter_model.json |
| 温湿度变送器 | 网关子设备 | env_sensor_model.json |
| 继电器 | 网关子设备 | relay_device_model.json |
| DTU节点 | 网关子设备 | dtu_node_model.json |
| DTU网关v1 | 网关设备 | - |

## 设备配置

| 设备名称 | 设备类型 | 配置文件 | 网关关系 |
|----------|----------|----------|----------|
| dtu网关 | GATEWAY | DTU网关v1 | 网关 |
| DTU_001 ~ DTU_011 | SENSOR | DTU节点 | 子设备 |
| METER_001 ~ METER_007 | SENSOR | 单相电表 | 子设备 |
| ENV_001 | SENSOR | 温湿度变送器 | 子设备 |
| ENV_002 | SENSOR | 温湿度变送器 | 子设备 |
| RELAY_001 | SENSOR | 继电器 | 子设备 |
| RELAY_002 | SENSOR | 继电器 | 子设备 |

## 产品与物模型边界

脚本可以通过 API 自动创建产品和设备，也可以刷新/重建已有产品配置和设备关系。

但是，如果产品是由脚本首次创建的，它的物模型内容不能直接视为已在 ThingsKit 页面中完成正常导入和发布，页面显示可能不完整或不符合预期。这个产品需要先在 ThingsKit 页面手动导入一次物模型；手动导入完成后，再由脚本对该产品进行后续修改、刷新或重建，平台页面才能正常显示和使用这些物模型内容。

推荐流程：

1. 修改本目录下的 `*_model.json`。
2. 如果产品不存在，可以先用脚本创建产品和设备。
3. 对脚本新建的产品，在 ThingsKit 页面手动导入一次物模型。
4. 手动导入后，再使用脚本刷新/重建设备、修改设备配置和修复网关关系。
5. 后续已完成手动导入的产品，可以继续用脚本做产品配置刷新和设备重建。
6. 运行模拟数据脚本验证遥测数据。

也就是说：脚本能创建产品和设备；脚本新建产品的首次物模型导入需要人工完成；完成首次人工导入后，脚本再修改该产品才会正常显示。

## 使用方法

```bash
cd /home/sueiny/rk3506_linux6.1_v1.2.0/app/Gateway/gatewayd/things_model/model_scripts

# 完整测试
python3 thingskit_tool.py test

# 刷新/重建设备配置；脚本新建产品需先手动导入一次物模型
python3 thingskit_tool.py sync

# 修复设备配置
python3 thingskit_tool.py fix

# 查看设备列表
python3 thingskit_tool.py devices

# 发送数据
python3 thingskit_tool.py send METER_001 '{"voltage":220.5}'

# 查询数据
python3 thingskit_tool.py get METER_001
```

## 物模型格式要求

### 关键字段
- `status`: 1（不是null）
- `functionJson.dataType.specs`: 必须正确设置
  - BOOL类型: `{"boolOpen": "开", "boolClose": "关"}`
  - DOUBLE类型: `{"unit": "V"}`
  - TEXT类型: `{"length": 64}`

### 示例
```json
{
  "functionType": "properties",
  "functionName": "拉合闸状态",
  "identifier": "relay_status",
  "callType": null,
  "accessMode": "r",
  "eventType": null,
  "functionJson": {
    "dataType": {
      "type": "BOOL",
      "specs": {
        "boolOpen": "合闸",
        "boolClose": "拉闸"
      }
    }
  },
  "status": 1
}
```

## API格式

### HTTP API上传遥测数据
```
POST /api/plugins/telemetry/DEVICE/{deviceId}/timeseries/any
Authorization: Bearer {token}
Content-Type: application/json

{"values": {"voltage": 220.5, "current": 5.2}}
```

### MQTT Topic
| 方向 | Topic | 说明 |
|------|-------|------|
| 上行 | v1/gateway/telemetry | 遥测数据（网关代发子设备） |
| 上行 | v1/devices/me/attributes | 网关属性 |
| 下行 | v1/gateway/commands/request | 命令下发 |

## 注意事项

1. 网关设备（DTU网关v1）是GATEWAY类型
2. 子设备（单相电表、温湿度变送器、继电器、DTU节点）是SENSOR类型
3. 子设备必须关联到网关设备
4. 脚本可以创建产品和设备，但脚本新建产品需要先在页面手动导入一次物模型
5. 已手动导入过物模型的产品，后续可以用脚本刷新/修改/重建配置
6. 物模型格式必须符合平台要求，特别是BOOL类型的specs
