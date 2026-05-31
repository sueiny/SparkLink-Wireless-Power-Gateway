# Node-RED 流程编排

## 概述

ThingsKit内置Node-RED流程编排引擎，通过拖拽节点实现数据处理、设备联动、API集成。支持自定义节点开发，便于私有协议扩展。

## 节点类型

### 输入节点（Input）

| 节点 | 说明 |
|------|------|
| mqtt in | 接收MQTT消息 |
| http in | 接收HTTP请求 |
| tcp in | 接收TCP数据 |
| udp in | 接收UDP数据 |
| serial in | 接收串口数据 |
| inject | 手动/定时触发 |

### 功能节点（Function）

| 节点 | 说明 |
|------|------|
| function | JavaScript函数处理 |
| switch | 条件分支 |
| change | 消息内容修改 |
| split | 消息拆分 |
| join | 消息合并 |
| delay | 延时处理 |
| batch | 批量处理 |
| range | 数值范围转换 |
| template | 模板渲染 |

### 输出节点（Output）

| 节点 | 说明 |
|------|------|
| mqtt out | 发送MQTT消息 |
| http response | HTTP响应 |
| tcp out | 发送TCP数据 |
| udp out | 发送UDP数据 |
| serial out | 发送串口数据 |
| debug | 调试输出 |
| file | 写入文件 |

### 设备节点

| 节点 | 说明 |
|------|------|
| device in | 接收设备数据 |
| device out | 下发设备指令 |
| device control | 设备控制 |
| device event | 设备事件 |

## 常用流程示例

### 示例1：设备数据处理与转发
```json
[
  {
    "id": "mqtt_in",
    "type": "mqtt in",
    "topic": "devices/+/telemetry",
    "qos": 1
  },
  {
    "id": "process",
    "type": "function",
    "func": "msg.payload.temperature = msg.payload.temperature * 1.8 + 32; return msg;"
  },
  {
    "id": "switch",
    "type": "switch",
    "property": "payload.temperature",
    "rules": [
      {"t": "gt", "v": 100}
    ]
  },
  {
    "id": "alarm",
    "type": "mqtt out",
    "topic": "alarms/temperature"
  }
]
```

### 示例2：定时数据采集
```json
[
  {
    "id": "inject",
    "type": "inject",
    "payload": "",
    "repeat": 5,
    "crontab": ""
  },
  {
    "id": "http_request",
    "type": "http request",
    "method": "GET",
    "url": "http://sensor.local/api/data"
  },
  {
    "id": "parse",
    "type": "json"
  },
  {
    "id": "transform",
    "type": "function",
    "func": "return { payload: { temperature: msg.payload.temp, humidity: msg.payload.hum } };"
  },
  {
    "id": "mqtt_out",
    "type": "mqtt out",
    "topic": "devices/sensor_001/telemetry"
  }
]
```

### 示例3：多设备联动
```json
[
  {
    "id": "trigger",
    "type": "device in",
    "deviceId": "motion_sensor_001",
    "event": "motion_detected"
  },
  {
    "id": "delay",
    "type": "delay",
    "pauseType": "delay",
    "timeout": "1",
    "timeoutUnit": "seconds"
  },
  {
    "id": "light_on",
    "type": "device out",
    "deviceId": "light_001",
    "command": "set",
    "params": {"switch": true, "brightness": 100}
  },
  {
    "id": "timer",
    "type": "delay",
    "pauseType": "delay",
    "timeout": "60",
    "timeoutUnit": "seconds"
  },
  {
    "id": "light_off",
    "type": "device out",
    "deviceId": "light_001",
    "command": "set",
    "params": {"switch": false}
  }
]
```

### 示例4：数据存储
```json
[
  {
    "id": "mqtt_in",
    "type": "mqtt in",
    "topic": "devices/#"
  },
  {
    "id": "parse",
    "type": "json"
  },
  {
    "id": "transform",
    "type": "function",
    "func": "return { payload: [msg.payload.deviceId, msg.payload.ts, msg.payload.values] };"
  },
  {
    "id": "database",
    "type": "mysql",
    "mydb": "db_config",
    "name": "iot_data",
    "query": "INSERT INTO telemetry (device_id, timestamp, data) VALUES (?, ?, ?)"
  }
]
```

## 自定义节点开发

### 节点结构
```javascript
module.exports = function(RED) {
  function CustomNode(config) {
    RED.nodes.createNode(this, config);
    var node = this;
    
    // 配置参数
    node.deviceId = config.deviceId;
    node.interval = config.interval;
    
    // 消息处理
    node.on('input', function(msg) {
      // 处理逻辑
      msg.payload = processData(msg.payload);
      node.send(msg);
    });
    
    // 关闭处理
    node.on('close', function() {
      // 清理资源
    });
  }
  
  RED.nodes.registerType("custom-node", CustomNode);
}
```

### 节点HTML定义
```html
<script type="text/javascript">
  RED.nodes.registerType('custom-node', {
    category: 'function',
    color: '#a6bbcf',
    defaults: {
      name: {value: ""},
      deviceId: {value: ""}
    },
    inputs: 1,
    outputs: 1,
    label: function() {
      return this.name || "custom-node";
    }
  });
</script>

<script type="text/html" data-template-name="custom-node">
  <div class="form-row">
    <label for="node-input-name">Name</label>
    <input type="text" id="node-input-name" placeholder="Name">
  </div>
  <div class="form-row">
    <label for="node-input-deviceId">Device ID</label>
    <input type="text" id="node-input-deviceId" placeholder="Device ID">
  </div>
</script>
```

## 调试与监控

### 调试节点
- 使用debug节点查看消息内容
- 支持输出到侧边栏
- 支持输出到控制台

### 流程状态
- 节点运行状态指示
- 消息计数统计
- 错误日志查看

### 性能监控
- 流程执行时间
- 节点处理延迟
- 消息队列长度
