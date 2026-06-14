---
type: plan
area: code-review
tags:
  - gateway/code-review
  - gateway/refactor-plan
---

# Gateway 可维护性整改路线

日期：2026-06-13  
目标：在不破坏已验证上行/下行链路的前提下，逐步提高 Gateway 的可读性、可维护性和测试可信度。

## 当前基线

- `gatewayd` 与 `sle_data_app` 可以在板端同时运行。
- mock 数据可经 `SLE-IPC batch collected` 进入 gatewayd 并发布 `v1/gateway/telemetry`。
- `METER_*` 和 `RELAY_*` 的 `set_relay` 下发已验证：MQTT request、命令解析、IPC 发送、IPC 响应、MQTT response 全链路成功。
- `env_sensor` 当前无 service；`gateway`、`dtu_node` 有 `reboot`，但本轮未重点测试。

## 2026-06-14 执行记录

- 已用 git 建立 Gateway 基线提交：`82add7cf5 archive Gateway project baseline`。
- `gatewayd/test/ipc_send.c` 已兼容抽象 Unix Socket：普通路径连接失败后自动尝试抽象 socket，保持原命令行参数不变。
- 命令 IPC 参数已增加入口保护：`params.dump()` 超过 `IPC_CMD_MAX_PARAM_LEN` 时直接返回 `PARAM_TOO_LARGE`，不会进入固定栈缓冲区。
- 命令链路日志已补 `request_id`：覆盖 command received/result/response enqueued、IPC send/response、command_response publish/retry/drop。
- `PublishManager` 和 `notify_printer` 已补充职责边界注释；设备 service 对照表已补入 `Gateway模块说明书`。
- 板端回归已通过 `build-ipc`、`push`、`test`：`ipc_send` 通过 stripped abstract socket 连入 gatewayd，日志出现 `SLE-IPC batch collected 11 devices`。
- 直接 MQTT client 模拟下发受 ThingsKit ACL/路由限制，gatewayd 未收到测试 request；保留平台页面下发作为命令链路最终验收入口。

## P0：先修测试误判和安全边界

| 项目 | 影响模块 | 预期收益 | 回归测试 |
| --- | --- | --- | --- |
| 让 `ipc_send` 支持抽象 Unix Socket，或让 `driver.sh test` 使用 `sle_data_app` mock 链路。 | `gatewayd/test`、`.claude/skills/run-gateway` | 全流程测试结果可信，不再因 socket 类型误报失败。 | 已修复并通过板端 `driver.sh test`，确认出现 `SLE-IPC batch collected 11 devices`。 |
| 在命令 IPC 参数入口拒绝超长 `params`。 | `gatewayd/command`、`gatewayd/datasource` | 避免异常 payload 造成栈缓冲区风险。 | 已修复，待下发超过 `IPC_CMD_MAX_PARAM_LEN` 的 params，确认回 `PARAM_TOO_LARGE` 且进程不崩。 |
| 给命令日志补 request id 贯穿字段。 | `gatewayd/app`、`gatewayd/command` | 重复下发、失败回包、IPC 超时可按单条命令定位。 | 已修复；直接 MQTT client 模拟下发被平台 ACL 限制，待页面/平台下发时用 request id grep 全生命周期。 |

## P1：低风险可读性重构

| 项目 | 影响模块 | 预期收益 | 回归测试 |
| --- | --- | --- | --- |
| 为 `PublishManager` 增加内部段落注释，分清实时发布、缓存补传、命令 response、订阅恢复。 | `gatewayd/app` | 降低后续修改发布逻辑的误伤概率。 | MQTT 在线、断网缓存恢复、命令回包三类场景均正常。 |
| 整理命令 payload 示例和设备 service 对照表。 | `gatewayd/things_model`、`docs/20_技术沉淀` | 测试人员知道哪些设备能下发哪些命令。 | 对 meter、relay、env、gateway、dtu 各发送一类命令，结果符合物模型。 |
| 给 `sle_multi_client.c`、`notify_printer.c` 添加模块级注释和关键函数说明。 | `sle_data_app` | 真实 SLE 接入前降低阅读成本。 | mock-only 模式启动和上报不变。 |
| 统一日志字段命名：`topic`、`request_id`、`target`、`method`、`result`。 | `gatewayd`、`sle_data_app` | 日志可检索性提升，便于板端排查。 | grep 单个 request id 能关联 MQTT、CMD、IPC、response。 |

## P2：中风险结构优化

| 项目 | 影响模块 | 预期收益 | 回归测试 |
| --- | --- | --- | --- |
| `notify_printer` 增加时间窗口 flush。 | `sle_data_app` | 低流量设备不再依赖满 64 帧才上送。 | 单设备/少设备 mock 数据在限定时间内到达 gatewayd。 |
| 缓存补传做限速和日志降噪。 | `gatewayd/app`、`gatewayd/storage` | 大缓存积压时不拖慢实时遥测和命令响应。 | 构造缓存积压，恢复网络后实时 publish 和补传都正常。 |
| `DeviceStateStore` 从全表重写改为单设备 upsert。 | `gatewayd/state`、`gatewayd/storage` | 命令高频场景降低 SQLite 写放大。 | 连续下发 relay 命令，状态叠加和数据库内容正确。 |
| 日志 info/debug 缓冲或异步化，warn/error 立即 flush。 | `gatewayd/common` | 降低高频 I/O 对业务线程的影响。 | 高负载上报下日志完整，进程退出前日志可落盘。 |

## P3：长期演进

| 项目 | 影响模块 | 预期收益 | 回归测试 |
| --- | --- | --- | --- |
| 将 `sle_multi_client.c` 按扫描、连接、notify、错误处理拆分。 | `sle_data_app` | 真实 SLE SDK 维护边界清晰。 | 真实设备扫描、连接、notify 和退出流程均正常。 |
| 将 `PublishManager` 的缓存补传抽成独立 helper。 | `gatewayd/app`、`gatewayd/storage` | 发布主循环更短，职责更清楚。 | 原有 telemetry、gateway_status、command_response 行为不变。 |
| 增加板端命令回归脚本。 | `gatewayd/test`、`.claude/skills/run-gateway` | 每次改命令链路都能自动验证。 | 自动下发 meter/relay/gateway/dtu 支持命令，校验 response code。 |

## 执行顺序建议

1. 先做 P0，解决测试可信度和内存边界。
2. 再做 P1，只做注释、日志字段、示例文档和小范围函数整理。
3. P2 每次只改一个运行期策略，并配套板端回归。
4. P3 等真实 SLE 接入前再做，避免提前重构带来额外不确定性。

## 验收标准

- 文档能从 `00-Gateway文档总览` 和 `CodeReview 索引` 进入。
- 任一整改项都有对应模块、收益和回归测试。
- 不改变当前已验证链路：mock 上行、MQTT 上云、meter/relay 命令下发。
- 重复下发先作为云端/页面侧待确认现象记录，不在 gatewayd 内默认做去重。
