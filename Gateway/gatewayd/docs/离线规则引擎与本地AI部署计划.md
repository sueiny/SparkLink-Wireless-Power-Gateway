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
- 规则引擎只返回事件和本地控制动作，不直接发布 MQTT，不写 SQLite，不直接执行 IPC。
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

## 4. 配置支持状态

当前规则引擎配置已经接入 `gatewayd/config/gateway_config.json`，入口是根节点下的 `offline_analysis` 段。启动时 `ConfigManager::load()` 会把该段解析到 `AppConfig::offline_analysis`，`PublishManager` 构造 `OfflineRuleEngine` 时把完整 `AppConfig` 引用传入规则引擎。

因此当前支持：

- 通过 JSON 调整是否启用离线分析、断网进入/退出保持时间、规则冷却时间。
- 通过 JSON 调整单相电表、温湿度、DTU 的默认阈值。
- 通过 JSON 对指定 `device_id` 做阈值覆盖。
- 通过 JSON 配置过流联动继电器列表。
- 通过 JSON 配置本地 AI 线性风险评分模型，默认关闭。

当前不支持：

- 运行期热更新配置。修改 `gateway_config.json` 后需要重启 `gatewayd` 才会生效。
- 在线状态下执行本地规则或本地 AI。`offline_only=false` 会被启动校验拒绝。
- root 上报规则阈值自动覆盖本地配置。当前阈值来源仍是 `gateway_config.json`。

## 5. 配置结构

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
  "offline_control": {
    "enable": true,
    "offline_only": true,
    "relay_close_on_recovery": true,
    "relay_devices": ["RELAY_001", "RELAY_002"]
  },
  "ai": {
    "enable": false,
    "offline_only": true,
    "mode": "linear_score",
    "model_path": "/userdata/gateway/models/offline_ai_model.json",
    "window_ms": 300000,
    "min_samples": 12,
    "cooldown_ms": 300000,
    "risk_threshold_medium": 0.55,
    "risk_threshold_high": 0.8
  }
}
```

优先级固定为：

1. `device_overrides`
2. 设备类型默认配置
3. 代码内保底默认值

`ConfigManager::validate()` 会拒绝负阈值、零阈值、上下限反转、非法 hold/cooldown，以及 `offline_only=false` 或 `ai.enable=true`。

### 5.1 顶层字段

| 字段 | 类型 | 当前默认值 | 说明 |
|------|------|------------|------|
| `offline_analysis.enable` | bool | `true` | 总开关；关闭后规则和离线控制都不执行。 |
| `offline_analysis.offline_only` | bool | `true` | 必须为 `true`；保证规则和 AI 只在断网/云不可达时运行。 |
| `offline_analysis.enter_hold_ms` | int | `10000` | 网络/云/MQTT 任一失败持续多久后进入本地离线分析。 |
| `offline_analysis.exit_hold_ms` | int | `30000` | 网络、云、MQTT 全部恢复持续多久后退出本地离线分析；也用于异常恢复保持判断。 |
| `offline_analysis.rule_engine.enable` | bool | `true` | 规则引擎开关。 |
| `offline_analysis.rule_engine.cooldown_ms` | int | `60000` | 单个 `device_id + rule_id` 触发后的冷却时间，避免重复刷屏。 |
| `offline_analysis.ai.enable` | bool | `false` | 本地 AI 开关；默认关闭，启用后只在离线门控 active 时运行。 |
| `offline_analysis.ai.mode` | string | `linear_score` | 首版只支持线性风险评分模型。 |
| `offline_analysis.ai.model_path` | string | `/userdata/gateway/models/offline_ai_model.json` | PC 侧训练后部署到板端的模型 JSON。 |
| `offline_analysis.ai.window_ms` | int | `300000` | AI 特征滑动窗口。 |
| `offline_analysis.ai.min_samples` | int | `12` | 单设备窗口内达到该样本数后才评估。 |
| `offline_analysis.ai.cooldown_ms` | int | `300000` | 同一设备同一风险头事件冷却时间。 |
| `offline_analysis.ai.risk_threshold_medium` | double | `0.55` | 中风险触发阈值。 |
| `offline_analysis.ai.risk_threshold_high` | double | `0.8` | 高风险触发阈值。 |

### 5.2 电表默认阈值

配置路径：`offline_analysis.rule_engine.defaults.single_phase_meter`

| 字段 | 默认值 | 触发/用途 |
|------|--------|-----------|
| `nominal_voltage_v` | `220.0` | 额定电压，当前主要用于配置说明和后续扩展。 |
| `over_voltage_v` | `235.4` | `voltage > over_voltage_v` 触发 `over_voltage`。 |
| `under_voltage_v` | `198.0` | `voltage < under_voltage_v` 触发 `under_voltage`。 |
| `frequency_low_hz` | `49.8` | `frequency < frequency_low_hz` 触发 `frequency_deviation`。 |
| `frequency_high_hz` | `50.2` | `frequency > frequency_high_hz` 触发 `frequency_deviation`。 |
| `rated_current_a` | `60.0` | 额定电流，用于计算过流阈值。 |
| `over_current_ratio` | `1.1` | 过流倍率，阈值为 `rated_current_a * over_current_ratio`。 |
| `hold_ms` | `10000` | 电表异常持续时间门槛。 |

### 5.3 温湿度默认阈值

配置路径：`offline_analysis.rule_engine.defaults.env_sensor`

| 字段 | 默认值 | 触发/用途 |
|------|--------|-----------|
| `high_temperature_c` | `55.0` | `temperature >= high_temperature_c` 触发 `high_temperature`。 |
| `high_humidity_rh` | `90.0` | `humidity >= high_humidity_rh` 触发 `high_humidity`。 |
| `hold_ms` | `30000` | 温湿度异常持续时间门槛。 |

### 5.4 DTU 默认阈值

配置路径：`offline_analysis.rule_engine.defaults.dtu_node`

| 字段 | 默认值 | 触发/用途 |
|------|--------|-----------|
| `offline_timeout_ms` | `60000` | 某个 DTU 超过该时间没有心跳或相关 telemetry 时触发 `node_offline`。 |

### 5.5 设备级覆盖

配置路径：`offline_analysis.rule_engine.device_overrides`

覆盖键必须是 `gateway_config.json` 中存在的 `device_id`，例如：

```json
"device_overrides": {
  "METER_001": {
    "rated_current_a": 80.0,
    "over_current_ratio": 1.2,
    "hold_ms": 5000
  },
  "ENV_001": {
    "high_temperature_c": 50.0,
    "high_humidity_rh": 85.0
  },
  "DTU_001": {
    "offline_timeout_ms": 120000
  }
}
```

覆盖规则：

- 未写某字段时继承设备类型默认值。
- 写出的覆盖值必须为正数；显式写 `0` 或负数会被启动校验拒绝。
- 电表覆盖后仍会校验 `under_voltage_v < over_voltage_v`、`frequency_low_hz < frequency_high_hz`。
- `device_id` 不存在会启动失败。

### 5.6 离线自动控制配置

配置路径：`offline_analysis.offline_control`

| 字段 | 类型 | 当前默认值 | 说明 |
|------|------|------------|------|
| `enable` | bool | `true` | 离线规则自动控制总开关。 |
| `offline_only` | bool | `true` | 必须为 `true`；在线时不允许本地自动控制。 |
| `relay_close_on_recovery` | bool | `true` | 过流恢复后是否闭合联动继电器。电表本体不自动合闸。 |
| `relay_devices` | string[] | 空 | 过流时联动断开的继电器设备列表，必须都是 `relay_device`。 |

## 6. 首版规则

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

## 7. 事件格式

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

离线期间事件会先进入 `PublishManager` 发布队列。由于云不可达，发布失败后会复用 SQLite `telemetry_cache` 缓存；联网恢复后由缓存补传流程发送。

## 8. 离线自动控制边界

离线控制只在 `offline_analysis.offline_control.enable=true` 且断网门控已经进入 active 后执行。

当前控制策略：

- `over_current` 持续达到 `hold_ms` 后，规则引擎输出本地 `set_relay` 动作。
- 异常电表只做拉闸：`state=0`，不自动合闸恢复。
- `offline_control.relay_devices` 中的继电器用于模拟联动，过流时断开，过流恢复并持续达到 `exit_hold_ms` 后可闭合。
- 在线时不执行本地规则控制，仍由云端/平台侧规则处理。

职责边界：

- `gatewayd` 持有业务、物模型、设备配置、Modbus RTU 和 SLE `ST DATA` 封装。
- `sle_data_app` 只管理 SLE 连接和 write handle，并把 gatewayd 传来的 raw ST frame 写入 SLE。
- IPC 下行使用 `CMD_METHOD_RAW_ST_DOWNLINK`，参数包含 `root_id`、Root 传输 `dst_node_id` 和完整 `st_frame`，不再传业务 JSON 给 C 侧解析。
- gatewayd 只负责封完整 `ST DATA + Modbus RTU` 包。下行 ST 帧头的 `dst_node_id` 指向 Root；Root 之后如何树状转发不由 gatewayd 处理。

控制帧规则：

| 目标 | Modbus | 语义 |
|------|--------|------|
| 电表 | `0x10` 写寄存器 `0x0010` | `0xAAAA=拉闸`，`0x5555=合闸`；当前只自动拉闸。 |
| 继电器 | `0x05` 写线圈 `0x0000` | `0x0000=断开`，`0xFF00=闭合`。 |

## 9. 当前代码路径

配置和运行路径：

```text
gateway_config.json
  -> ConfigManager::load()
  -> AppConfig::offline_analysis
  -> PublishManager::offlineAnalysisActive()
  -> OfflineRuleEngine::evaluate()
  -> RuleEvent / OfflineControlAction
```

关键源码：

| 文件 | 作用 |
|------|------|
| `include/config/config_manager.h` | 定义 `OfflineAnalysisConfig`、`RuleEngineConfig`、阈值结构体。 |
| `src/config/config_manager.cpp` | 解析 JSON，填默认值，校验阈值和设备引用。 |
| `src/rules/offline_rule_engine.cpp` | 规则状态、持续时间、冷却、事件和离线控制动作计算。 |
| `src/app/publish_manager.cpp` | 判断离线门控，调用规则引擎，发布/缓存事件，入队本地控制命令。 |
| `src/command/command_executor.cpp` | 把 `set_relay` 转成 Modbus RTU + ST DATA，并走 raw ST IPC 下发。 |

## 10. 本地 AI V2 边界

本地 AI 小模型只在断网期间运行，且不允许直接控制设备。当前 V2 目标不是本地 LLM，也不是独立 `ai-daemon`，而是 `gatewayd` 内部的轻量线性风险评分：

- PC 侧生成训练数据并训练多标签 Logistic Regression。
- 板端只加载 JSON 模型参数，使用 C++ 推理，不依赖 Python/scikit-learn。
- AI 输入为 `TelemetryData` 与滑动窗口派生特征，不解析 SLE/Modbus 原始帧。
- AI 输出 `ai_risk` 事件，字段包含 `risk_score`、`risk_level`、`risk_type`、`features`、`source=offline_ai`、`offline=true`。
- AI 事件走 `PublishManager` 统一发布队列；MQTT 离线时复用 SQLite 缓存补传。

配置入口已经扩展为：

```json
"ai": {
  "enable": false,
  "offline_only": true,
  "mode": "linear_score",
  "model_path": "/userdata/gateway/models/offline_ai_model.json",
  "window_ms": 300000,
  "min_samples": 12,
  "cooldown_ms": 300000,
  "risk_threshold_medium": 0.55,
  "risk_threshold_high": 0.8
}
```

启用约束：

- `offline_only` 必须为 `true`。
- `mode` 首版只允许 `linear_score`。
- `window_ms`、`min_samples`、`cooldown_ms` 必须为正数。
- `0 < risk_threshold_medium < risk_threshold_high <= 1`。
- 模型文件缺失时只禁用 AI 并打印日志，不能影响遥测、规则、下行控制。

两路 root 数据集和回放流程见 `本地AI数据集与两路Root联调.md`。

## 11. 验证命令

```bash
bash .claude/skills/run-gateway/driver.sh build-gw
bash .claude/skills/run-gateway/driver.sh push
bash .claude/skills/run-gateway/driver.sh test-real-listen
adb shell "grep -E 'RULE|rule_event|offline analysis|message cached' /userdata/gateway/data/log/gateway.log | tail -100"
adb shell "grep -E 'offline control|CMD_METHOD_RAW_ST|cmd sent|cmd response received|ST-TX' /userdata/gateway/data/log/gateway.log /tmp/sle_data_app.out | tail -120"
adb shell "/userdata/gateway/bin/sqlite3 /userdata/gateway/data/gateway.db 'SELECT topic,payload FROM telemetry_cache ORDER BY id DESC LIMIT 5;'"
```

验收重点：

- MQTT 在线时注入异常数据，本地规则不触发。
- MQTT 离线超过 `enter_hold_ms` 后注入异常数据，本地规则触发一次并进入冷却。
- 恢复 MQTT 后，离线期间缓存的规则事件能补传。
- DTU/root COM 侧可用 `py -3 sle_data_app/test/dtu_downlink_command_tester.py COM19 COM23 COM36 --expect meter-trip --expect-root-id 1` 验证下行 ST 和 Modbus CRC；relay 联动也按 root 传输目标验收，例如 `--expect relay-open --expect-root-id 1`。
