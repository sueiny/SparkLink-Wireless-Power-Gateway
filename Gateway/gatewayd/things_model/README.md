# ThingsKit 产品物模型

本目录存放 ThingsKit 产品物模型定义。这些文件根据
`config/gateway_config.json` 以及当前 `gatewayd` 遥测/属性上报字段整理。

JSON 结构参考 ThingsKit 物模型导入示例：

```json
{
  "properties": [
    {
      "functionName": "经度",
      "identifier": "longitude",
      "functionType": "properties",
      "accessMode": "rw",
      "specs": {
        "dataType": {
          "type": "DOUBLE",
          "specs": {}
        }
      },
      "extensionDesc": null
    }
  ]
}
```

每个模型文件对应一个产品的物模型定义。产品需要时会包含
`properties`、`events` 和 `services`。

## 文件说明

- `single_phase_meter_model.json`：`METER_001` 到 `METER_007` 使用的单相电表产品物模型。
- `env_sensor_model.json`：`ENV_001` 和 `ENV_002` 使用的温湿度变送器产品物模型。
- `relay_device_model.json`：`RELAY_001` 和 `RELAY_002` 使用的继电器产品物模型。
- `dtu_node_model.json`：`DTU_001` 到 `DTU_011` 使用的 DTU 节点产品物模型。
- `gateway_model.json`：`dtu网关` 使用的 DTU 网关产品物模型。
- `all_product_models.json`：所有产品和设备的索引配置。

## 导入说明

ThingsKit 文档中说明，JSON 导入通常应使用从 ThingsKit TSL 导出的 JSON 文件。
如果平台不能直接接受本目录的文件，可以先在 ThingsKit 中创建并导出一个空物模型，
再把本目录文件中的 `properties` 数组复制到导出的 TSL 结构中。对于定义了事件
或服务的产品，也需要同步复制对应的 `events` 和 `services` 数组。

辅助脚本可以通过 API 创建 ThingsKit 产品和设备。对于由脚本首次创建的产品，
需要先在 ThingsKit 页面手动导入一次物模型，再依赖页面显示结果。完成首次
手动导入后，同一产品后续可以正常使用脚本进行更新或重建。设备创建、设备更新
以及网关关系修复可以由脚本处理。

物模型标识符必须与 `gatewayd` 上报 payload 中 `values` 下的 key 保持一致，
例如：

```json
{
  "deviceId": "METER_002",
  "ts": 1710000000000,
  "values": {
    "voltage": 220.1,
    "current": 3.2,
    "active_power": 676.2,
    "online": true
  }
}
```

## 凭证模式

当前 `gatewayd/config/gateway_config.json` 默认使用 access token 连接 ThingsKit MQTT：
设备 access token 作为 MQTT 用户名，密码为空。配置中仍保留 `mqtt_basic` 旧凭证，
后续需要回退时只需把 `credential_mode` 从 `access_token` 改为 `mqtt_basic`。

## 命令下发边界

当前物模型中保留了服务和事件定义，用于后续测试与联调。板端 `gatewayd` 现阶段
只实现命令下发边界：订阅命令 Topic、解析 `method`、`params` 和目标设备，
按当前物模型判断服务名与参数是否合法，并返回解析结果。

现阶段不会执行实际控制动作，也不会因为解析到服务而主动触发事件上报。例如
`reboot`、`ota_upgrade`、`set_collect_cycle`、`trigger_collect`、`set_relay`
和 `clear_energy` 只会被识别和校验，不会真正重启、升级、改采集周期、拉合闸或清电量。

## 拓扑关系

当前 DTU 测试数据中有三层关系。它们有些地方形状相似，但含义不同。

### DTU 通信拓扑

DTU 拓扑表示通信组网关系。每个 DTU 节点通过 `topology.parent_mac` 和
`topology.child_macs` 上报自己的上级和下级节点。

```text
dtu网关
├── DTU_001
│   ├── DTU_002
│   │   ├── DTU_004
│   │   └── DTU_005
│   └── DTU_003
│       ├── DTU_006
│       └── DTU_007
├── DTU_008
├── DTU_009
├── DTU_010
└── DTU_011
```

### 电表业务拓扑

电表拓扑表示电力/业务层级关系。支表通过 `parent_meter_id` 上报上级电表。

```text
METER_001
├── METER_002
│   ├── METER_004
│   └── METER_005
└── METER_003
    ├── METER_006
    └── METER_007
```

### DTU 到 Modbus 设备映射

当前测试数据中，每个 DTU 节点采集一个下挂 Modbus 设备。设备类型通过
`collect_config.type_1` 上报：

```text
0 = 未定义
1 = 三相电表
2 = 单相电表
3 = 温湿度变送器
4 = 继电器
5 = 其他预留
```

当前映射关系：

```text
DTU_001 ── METER_001
DTU_002 ── METER_002
DTU_003 ── METER_003
DTU_004 ── METER_004
DTU_005 ── METER_005
DTU_006 ── METER_006
DTU_007 ── METER_007
DTU_008 ── ENV_001
DTU_009 ── ENV_002
DTU_010 ── RELAY_001
DTU_011 ── RELAY_002
```
