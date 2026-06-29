# 本地 AI 实现细节

> Status: Current implementation reference.
> Authority: `gatewayd` 本地 AI 模块边界、配置和验证方式以本文为入口。
> Superseded by: None.
> Last verified against: doc status reviewed on 2026-06-28; recheck `src/ai` before AI code changes.

本文说明 `gatewayd` 当前本地 AI 的实现边界、配置、模型格式、运行链路和验证方式。当前实现不是本地大模型，也不在板端运行 Python/scikit-learn；它是离线状态下运行的轻量线性风险评分模块。

## 1. 定位

本地 AI 只在断网或云不可达时启用，用于给网关离线运行期间补充趋势类风险判断。联网时由云端规则、云端 AI 或平台告警承担分析，板端本地 AI 完全跳过。

当前能力：

- 输入：`gatewayd` 已经标准化后的 `TelemetryData`。
- 模型：PC 侧训练导出的 JSON 线性评分模型，`mode=linear_score`。
- 推理：C++ 在 `gatewayd` 进程内完成。
- 输出：`ai_risk` 事件，进入统一发布队列；离线发布失败后写入 SQLite 缓存，联网恢复后补传。
- 控制：AI 不直接控制设备。本地下行控制仍由离线规则引擎和命令链路负责。

## 2. 模块边界

核心类：

| 模块 | 文件 | 职责 |
| --- | --- | --- |
| `gateway::ai::OfflineAiAnalyzer` | `include/ai/offline_ai_analyzer.h`、`src/ai/offline_ai_analyzer.cpp` | 加载模型、维护滑动窗口、提取特征、输出风险事件。 |
| `PublishManager` | `src/app/publish_manager.cpp` | 维护离线门控，离线时调用规则引擎和 AI，统一入队发布。 |
| `ConfigManager` | `src/config/config_manager.cpp` | 读取并校验 `offline_analysis.ai` 配置。 |

`OfflineAiAnalyzer` 不解析 SLE/ST/Modbus 原始帧，不访问 MQTT，不写 SQLite，也不做下发控制。构造函数只保存配置和 logger，模型文件 I/O 在 `init()` 中执行，符合当前 gatewayd 模块风格。

## 3. 离线门控

本地 AI 受 `PublishManager::offlineAnalysisActive()` 统一控制。以下任一条件满足时先进入原始离线状态：

- `NetworkState.available == false`
- `NetworkState.cloud_reachable == false`
- `MqttCloudClient::isConnected() == false`

原始离线持续超过 `offline_analysis.enter_hold_ms` 后，`offline_analysis_active_` 置为 true，规则引擎和 AI 才会运行。网络恢复后，当前实现用 `offline_analysis.exit_hold_ms` 做退出保持，避免网络抖动导致频繁启停。

在线或未达到离线保持时间时：

- `rule_engine_.evaluate(telemetry, false, now)` 清理规则状态。
- `ai_analyzer_.evaluate(telemetry, false, now)` 清理 AI 历史窗口。
- 不产生本地规则事件、AI 事件或本地控制动作。

## 4. 配置入口

配置位于 `gatewayd/config/gateway_config.json` 的 `offline_analysis.ai`：

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

字段说明：

| 字段 | 当前默认 | 说明 |
| --- | --- | --- |
| `enable` | `false` | 默认关闭；现场验证 AI 时显式打开。 |
| `offline_only` | `true` | 必须为 true，防止联网时运行本地 AI。 |
| `mode` | `linear_score` | 首版只支持线性评分模型。 |
| `model_path` | `/userdata/gateway/models/offline_ai_model.json` | 板端模型 JSON 路径。 |
| `window_ms` | `300000` | 每个设备保留的滑动窗口长度。 |
| `min_samples` | `12` | 设备样本数达到该值后才开始评分。 |
| `cooldown_ms` | `300000` | 同一设备同一风险头的事件冷却时间。 |
| `risk_threshold_medium` | `0.55` | medium 风险阈值。 |
| `risk_threshold_high` | `0.8` | high 风险阈值。 |

校验规则：

- `offline_only` 必须为 `true`。
- `mode` 必须为 `linear_score`。
- `model_path` 不能为空。
- `window_ms`、`min_samples`、`cooldown_ms` 必须为正数。
- `0 < risk_threshold_medium < risk_threshold_high <= 1`。

模型文件不存在或格式错误时，`OfflineAiAnalyzer::init()` 只禁用 AI 并记录日志，不阻断 gatewayd 启动，不影响遥测、规则引擎和下行控制。

## 5. 模型格式

模型由 PC 侧训练脚本导出为 JSON，板端只读取参数并执行线性评分。

关键字段：

| 字段 | 说明 |
| --- | --- |
| `version` | 模型版本，例如 `offline_ai_linear_score_v1`。 |
| `mode` | 当前为 `linear_score`。 |
| `feature_names` | 特征名列表。 |
| `feature_mean` | 每个特征的标准化均值。 |
| `feature_scale` | 每个特征的标准化尺度；接近 0 时按 1 处理。 |
| `heads` | 多风险头配置，每个风险头包含 `device_types`、`weights`、`bias`。 |

评分公式：

```text
score = sigmoid(bias + sum(weight_i * normalized(feature_i)))
normalized(x) = (x - feature_mean) / feature_scale
```

当前支持的风险头：

| 风险头 | 目标设备类型 | 典型含义 |
| --- | --- | --- |
| `meter_voltage_risk` | `single_phase_meter` | 过压、欠压、电压趋势异常。 |
| `meter_current_risk` | `single_phase_meter` | 过流、冲击电流、功率因数下降。 |
| `meter_energy_risk` | `single_phase_meter` | 电量冻结、突跳或异常变化。 |
| `env_risk` | `env_sensor` | 高温、高湿、传感器卡死。 |
| `dtu_stability_risk` | `dtu_node` | 心跳抖动、缺包、疑似离线。 |
| `relay_state_risk` | `relay_device` | 继电器状态异常。 |

## 6. 特征

`OfflineAiAnalyzer::buildFeatures()` 当前生成以下特征：

```text
voltage, current, active_power, power_factor, frequency, energy,
temperature, humidity, relay_state, online,
voltage_deviation_ratio, frequency_deviation_hz, power_factor_drop,
voltage_slope_per_min, current_slope_per_min,
temperature_slope_per_min, humidity_slope_per_min,
energy_delta, energy_freeze,
sample_gap_ms, sample_gap_ratio
```

说明：

- 基础遥测来自 `TelemetryData.numeric_values`、`integer_values`、`bool_values`。
- 趋势特征基于每个设备的滑动窗口首末样本计算。
- `sample_gap_ms` 和 `sample_gap_ratio` 用于识别 DTU 抖动、缺包和链路异常。
- `dtu_stability_risk` 会基于 DTU 节点历史单独评估。

## 7. 事件输出

AI 事件通过 `PublishManager::enqueueAiEvents()` 进入统一发布队列：

- topic：`cloud_client_.eventsTopic()`
- kind：`PublishMessageKind::RuleEvent`
- event：`ai_risk`

事件 details 包含：

```json
{
  "source": "offline_ai",
  "offline": true,
  "model_version": "offline_ai_linear_score_v1",
  "mode": "linear_score",
  "risk_type": "meter_current_risk",
  "risk_score": 0.87,
  "risk_level": "high",
  "window_ms": 300000,
  "sample_count": 12,
  "features": {
    "current": 93.2,
    "current_slope_per_min": 8.1
  }
}
```

MQTT 离线时，该事件会像普通遥测一样写入 `telemetry_cache`，联网恢复后由发布线程补传。

## 8. 数据集与训练工具

PC 侧工具放在 `sle_data_app/test`，因为它们直接生成可回放到 DTU root 串口的 ST 帧：

| 工具 | 作用 |
| --- | --- |
| `offline_ai_dataset.py` | 按当前拓扑生成训练 CSV、标签 JSONL 和可回放 ST 帧。 |
| `offline_ai_train.py` | 训练并导出 `offline_ai_model.json`。 |
| `dtu_root_run_sender.py` | Windows COM 或 Linux 串口回放 ST 帧，支持两路 root。 |

生成数据集：

```bash
python3 app/Gateway/sle_data_app/test/offline_ai_dataset.py \
  --hours 24 \
  --interval-sec 300 \
  --out-dir /tmp/gateway_ai_dataset
```

训练模型：

```bash
py -3 app/Gateway/sle_data_app/test/offline_ai_train.py \
  /tmp/gateway_ai_dataset/training.csv \
  --out /tmp/gateway_ai_dataset/offline_ai_model.json
```

部署模型：

```bash
adb shell "mkdir -p /userdata/gateway/models"
adb push /tmp/gateway_ai_dataset/offline_ai_model.json \
  /userdata/gateway/models/offline_ai_model.json
```

两路 root 回放示例：

```bash
py -3 dtu_root_run_sender.py COM19 COM23 \
  --port-root COM19=1 \
  --port-root COM23=10 \
  --scenario-file D:\tmp\gateway_ai_dataset\replay.jsonl \
  --duration 300 \
  --interval 5 \
  --line-delay 0.02 \
  --warmup-sec 5 \
  --warmup-interval 0.2 \
  --warmup-text 12123213 \
  --post-warmup-delay 8 \
  --hold-open 10
```

注意：当前 `dtu_root_run_sender.py` 内置 root 限制仍是旧 `1/10`，且内置拓扑未同步 `DTU_001..DTU_009 = node_id 101..109`；按当前配置回放前需要先同步脚本或使用自定义 replay。

## 9. 验证方式

构建和部署：

```bash
cd app/Gateway
bash .claude/skills/run-gateway/driver.sh build-gw
bash .claude/skills/run-gateway/driver.sh push
```

板端日志：

```bash
adb shell "grep -E 'AI|ai_risk|offline analysis|message cached|cache flush' /userdata/gateway/data/log/gateway.log | tail -120"
adb shell "/userdata/gateway/bin/sqlite3 /userdata/gateway/data/gateway.db 'SELECT COUNT(*) FROM telemetry_cache;'"
```

验收重点：

- 在线状态下不产生 `ai_risk`。
- 离线超过 `enter_hold_ms` 后，异常场景产生对应 `ai_risk`。
- 模型文件缺失只禁用 AI，不影响 gatewayd 主链路。
- AI 事件离线缓存、联网后补传。
- 规则引擎、meter 拉闸、relay 下行和 SLE 上下行链路不受影响。
