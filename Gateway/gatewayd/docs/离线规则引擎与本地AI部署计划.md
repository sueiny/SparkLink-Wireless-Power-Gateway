# 离线规则引擎与本地 AI 部署计划

本文记录 `gatewayd` 本地规则引擎和后续本地 AI 小模型的部署边界。当前版本先实现离线规则引擎，AI 只保留配置入口，不启动模型进程。

## 1. 运行边界

规则引擎和本地 AI 都只在断网或云不可达时启用。云端在线时由平台侧规则引擎负责告警，网关本地不重复判断。

断网进入条件：

- `NetworkState.available=false`
- 或 `NetworkState.cloud_reachable=false`
- 或 MQTT `cloud_connected=false`
- 以上任一状态持续达到 `offline_analysis.enter_hold_ms`

断网退出条件：

- 网络可用、云端可达、MQTT 已连接三者同时满足
- 且持续达到 `offline_analysis.exit_hold_ms`

## 2. 工程约束

实现必须遵守 Gateway 工程风格：

- 模块命名为 `gateway::rules`，类名 `OfflineRuleEngine`。
- 函数使用 `camelCase`，成员变量使用 `snake_case_`。
- 构造函数只保存配置引用，不做 I/O。
- `OfflineRuleEngine::evaluate()` 只消费标准化后的 `TelemetryData`，不解析 SLE/Modbus 原始帧。
- 规则引擎只返回事件，不直接发布 MQTT，不写 SQLite。
- MQTT 回调线程、SLE IPC 线程、命令线程都不执行规则判断。
- 规则事件统一进入 `PublishManager` 的发布队列，失败后复用 SQLite 缓存补传。

## 3. 阈值来源

电压和频率默认值来自《供电营业规则》供电质量条款：

- 额定频率为交流 50Hz。
- 正常状况下，装机容量 300 万千瓦以上电网的频率允许偏差为 `+-0.2Hz`，不足 300 万千瓦为 `+-0.5Hz`。
- 220V 单相供电电压允许偏差为额定值 `+7%/-10%`，即 `235.4V/198.0V`。
- 非正常状况下，用户受电端电压最大允许偏差不应超过额定值 `+-10%`。

参考资料：

- 国家发展改革委：`https://www.ndrc.gov.cn/xxgk/zcfb/fzggwl/202403/P020240315319888305936.pdf`

过流、温度、湿度没有在本项目中硬编码为南网统一值。它们取决于现场 CT、断路器、柜体、传感器和安装环境，必须通过 `gateway_config.json` 配置。

## 4. 配置结构

配置入口位于 `gatewayd/config/gateway_config.json`：

```json
"offline_analysis": {
  "enable": true,
  "offline_only": true,
  "enter_hold_ms": 10000,
  "exit_hold_ms": 30000,
  "rule_engine": {
    "enable": true,
    "cooldown_ms": 60000,
    "defaults": {
      "single_phase_meter": {
        "nominal_voltage_v": 220.0,
        "over_voltage_v": 235.4,
        "under_voltage_v": 198.0,
        "frequency_low_hz": 49.8,
        "frequency_high_hz": 50.2,
        "rated_current_a": 60.0,
        "over_current_ratio": 1.1,
        "hold_ms": 10000
      },
      "env_sensor": {
        "high_temperature_c": 55.0,
        "high_humidity_rh": 90.0,
        "hold_ms": 30000
      },
      "dtu_node": {
        "offline_timeout_ms": 60000
      }
    },
    "device_overrides": {
      "METER_001": {
        "rated_current_a": 80.0,
        "over_current_ratio": 1.2
      }
    }
  },
  "ai": {
    "enable": false
  }
}
```

优先级固定为：

1. `device_overrides`
2. 设备类型默认配置
3. 代码内保底默认值

`ConfigManager::validate()` 会拒绝负阈值、零阈值、上下限反转、非法 hold/cooldown，以及 `offline_only=false` 或 `ai.enable=true`。

## 5. 首版规则

| 设备 | 规则 | 触发条件 | 持续时间 | 事件 |
|------|------|----------|----------|------|
| 单相电表 | 过压 | `voltage > over_voltage_v` | `hold_ms` | `over_voltage` |
| 单相电表 | 欠压 | `voltage < under_voltage_v` | `hold_ms` | `under_voltage` |
| 单相电表 | 频率偏差 | `frequency < frequency_low_hz` 或 `frequency > frequency_high_hz` | `hold_ms` | `frequency_deviation` |
| 单相电表 | 过流 | `current > rated_current_a * over_current_ratio` | `hold_ms` | `over_current` |
| 温湿度 | 高温 | `temperature >= high_temperature_c` | `hold_ms` | `high_temperature` |
| 温湿度 | 高湿 | `humidity >= high_humidity_rh` | `hold_ms` | `high_humidity` |
| DTU | 离线 | 无心跳/无 DTU telemetry 超过 `offline_timeout_ms` | 立即进入冷却判断 | `node_offline` |

每条规则按 `device_id + rule_id` 维护状态。触发后进入 `cooldown_ms` 冷却，避免断网期间重复刷屏。恢复正常持续 `exit_hold_ms` 后清除 active 状态。

## 6. 事件格式

规则事件使用现有 ThingsKit event payload：

```json
{
  "deviceId": "METER_001",
  "event": "over_voltage",
  "severity": "warning",
  "message": "voltage 236.000V exceeds 235.400V",
  "details": {
    "rule_id": "over_voltage",
    "source": "offline_rule_engine",
    "offline": true,
    "value": 236.0,
    "threshold": 235.4,
    "unit": "V",
    "duration_ms": 10000
  }
}
```

事件 topic 由 `MqttCloudClient` 的 `eventsTopic()` 提供，保持和 `thingskit.topic_prefix` 一致。

## 7. 后续 AI 边界

本地 AI 小模型只在断网期间运行，且第一阶段不允许直接控制设备。后续 `ai-daemon` 只能消费规则引擎或 `TelemetryData` 派生特征，输出 `risk_score`、`risk_level`、`risk_type`、`reason`，再由 `gatewayd` 统一发布事件。

## 8. 验证命令

```bash
bash .claude/skills/run-gateway/driver.sh build-gw
bash .claude/skills/run-gateway/driver.sh push
bash .claude/skills/run-gateway/driver.sh test-real-listen
adb shell "grep -E 'RULE|rule_event|offline analysis|message cached' /userdata/gateway/data/log/gateway.log | tail -100"
adb shell "/userdata/gateway/bin/sqlite3 /userdata/gateway/data/gateway.db 'SELECT topic,payload FROM telemetry_cache ORDER BY id DESC LIMIT 5;'"
```

验收重点：

- MQTT 在线时注入异常数据，本地规则不触发。
- MQTT 离线超过 `enter_hold_ms` 后注入异常数据，本地规则触发一次并进入冷却。
- 恢复 MQTT 后，离线期间缓存的规则事件能补传。
