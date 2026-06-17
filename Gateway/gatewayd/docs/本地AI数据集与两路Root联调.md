# 本地 AI 数据集与两路 Root 联调

本文记录 V2 本地 AI 的数据集生成、模型训练、两路 DTU root 回放和板端验证方式。AI 只在断网/云不可达时运行，联网时由云端规则或云端 AI 负责分析。

## 1. 目标

首版目标不是本地大模型，而是轻量、可解释、可训练的多标签风险评分模型：

- PC 侧生成电力现场日常和突发场景数据。
- PC 侧训练 Logistic Regression 风险模型。
- 板端 `gatewayd` 只加载 JSON 参数并用 C++ 推理。
- AI 只输出 `ai_risk` 事件，不直接控制设备。

风险头：

| 风险头 | 设备 | 说明 |
| --- | --- | --- |
| `meter_voltage_risk` | `single_phase_meter` | 过压、欠压、电压趋势异常。 |
| `meter_current_risk` | `single_phase_meter` | 过流、冲击电流、功率因数下降。 |
| `meter_energy_risk` | `single_phase_meter` | 电量冻结、突跳或倒退。 |
| `env_risk` | `env_sensor` | 高温、高湿、传感器卡死。 |
| `dtu_stability_risk` | `dtu_node` | DTU 心跳抖动、缺包、疑似离线。 |
| `relay_state_risk` | `relay_device` | 继电器状态异常。 |

## 2. 两路 Root 拓扑

按 `docs/00_项目说明/设备拓扑图.md`：

| Root | 串口默认 | 范围 | 内容 |
| --- | --- | --- | --- |
| `DTU_001` | `COM19` | `DTU_001..DTU_011` | 11 个外接设备：7 个电表、2 个温湿度、2 个继电器。 |
| `DTU_012` | `COM23` | `DTU_012..DTU_031` | SLE 中继树节点心跳和链路稳定性场景。 |

`COM36` 本阶段不参与；如果现场接线变化，只调整 `--port-root` 参数。

## 3. 数据集生成

脚本：

```bash
python3 app/Gateway/sle_data_app/test/offline_ai_dataset.py \
  --hours 24 \
  --interval-sec 300 \
  --out-dir /tmp/gateway_ai_dataset
```

输出：

| 文件 | 用途 |
| --- | --- |
| `training.csv` | 训练输入，包含特征列和每个风险头的 label。 |
| `labels.jsonl` | 带场景、设备、原始值、特征、标签的可审查样本。 |
| `replay.jsonl` | 可被 `dtu_root_run_sender.py --scenario-file` 回放的 ST 帧。 |

覆盖场景：

```text
normal, daily_peak, voltage_sag, voltage_swell,
over_current_gradual, current_surge, power_factor_drop,
energy_jump_or_freeze, high_temp, high_humidity,
sensor_stuck, relay_state_anomaly, dtu_offline, dtu_jitter
```

## 4. 模型训练

PC 侧优先使用 scikit-learn：

```bash
py -3 -m pip install scikit-learn
py -3 app/Gateway/sle_data_app/test/offline_ai_train.py \
  /tmp/gateway_ai_dataset/training.csv \
  --out /tmp/gateway_ai_dataset/offline_ai_model.json
```

开发机没有 scikit-learn 时，脚本会使用内置 Logistic fallback，输出格式保持一致。严格要求 scikit-learn 时增加：

```bash
--require-sklearn
```

模型 JSON 关键字段：

| 字段 | 说明 |
| --- | --- |
| `version` | 模型版本，例如 `offline_ai_linear_score_v1`。 |
| `feature_names` | 特征顺序。 |
| `feature_mean` / `feature_scale` | 标准化参数。 |
| `heads` | 每个风险头的 `device_types`、`weights`、`bias`。 |

部署到板端：

```bash
adb shell "mkdir -p /userdata/gateway/models"
adb push /tmp/gateway_ai_dataset/offline_ai_model.json \
  /userdata/gateway/models/offline_ai_model.json
```

## 5. gatewayd 配置

配置入口是 `gateway_config.json` 的 `offline_analysis.ai`：

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

当前默认 `enable=false`。启用时约束：

- `offline_only` 必须为 `true`。
- `mode` 首版只允许 `linear_score`。
- `window_ms`、`min_samples`、`cooldown_ms` 必须为正数。
- `0 < risk_threshold_medium < risk_threshold_high <= 1`。
- 模型文件缺失时只禁用 AI 并记录日志，不影响遥测、规则、下行控制。

## 6. 两路 Root 回放

Dry-run 检查两路 root 拆分：

```bash
python3 app/Gateway/sle_data_app/test/dtu_root_run_sender.py \
  --dry-run \
  --scenario topology-all \
  --root-id 1 \
  --preview-count 1

python3 app/Gateway/sle_data_app/test/dtu_root_run_sender.py \
  --dry-run \
  --scenario topology-all \
  --root-id 12 \
  --preview-count 1
```

Windows 实际回放：

```bash
py -3 dtu_root_run_sender.py COM19 COM23 \
  --port-root COM19=1 \
  --port-root COM23=12 \
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

参数含义：

- `COM19=1`：只发送 `DTU_001` root 子树。
- `COM23=12`：只发送 `DTU_012` root 子树。
- `--scenario-file`：使用数据集生成的 ST 帧，不再用固定 `topology-all` 值。

## 7. 板端验证

构建和推送：

```bash
cd app/Gateway
bash .claude/skills/run-gateway/driver.sh build-gw
bash .claude/skills/run-gateway/driver.sh push
```

启动真实监听：

```bash
bash .claude/skills/run-gateway/driver.sh test-real-listen
```

日志检查：

```bash
adb shell "grep -E 'AI|ai_risk|offline analysis|SLE-IPC|publish success kind=telemetry|message cached' /userdata/gateway/data/log/gateway.log | tail -120"
adb shell "/userdata/gateway/bin/sqlite3 /userdata/gateway/data/gateway.db 'SELECT topic,payload FROM telemetry_cache ORDER BY id DESC LIMIT 5;'"
```

验收标准：

- 在线状态下异常数据不触发本地 AI。
- 离线超过 `enter_hold_ms` 后，异常场景触发 `ai_risk`。
- `ai_risk` 事件包含 `risk_score`、`risk_level`、`risk_type`、`features`、`source=offline_ai`。
- MQTT 离线时 AI 事件进入 SQLite 缓存，联网恢复后补传。
- 现有规则引擎、meter 拉闸、relay 下行、ST 转发不受影响。
