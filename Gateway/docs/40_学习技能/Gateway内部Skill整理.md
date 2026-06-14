---
type: note
area: learning
tags:
  - gateway/learning
  - gateway/skill
---

# Gateway 内部 Skill 整理

## 项目中用在哪里

Gateway 目录内有两套内部 skill/工作流说明：

- `.opencode/skills/embedded-gateway-dev`：面向 `gatewayd` C++17 嵌入式网关开发、架构边界、CMake 交叉编译、MQTT/SQLite/网络/状态模块。
- `.claude/skills/run-gateway`：面向板端构建、推送、测试，核心入口是 `.claude/skills/run-gateway/driver.sh`。
- `~/.codex/skills/gateway-runtime`：已把上述两套项目 skill 合并为 Codex 用户级 skill，供 Codex 在后续 Gateway 开发、运行、监听、命令下发验证和 code review 中直接触发使用。

这两套 skill 不替代源码审查，但可以作为本项目的工程约束和验证路径。

## Codex gateway-runtime 要点

- 触发场景：`app/Gateway` 代码修改、板端构建部署、MQTT/ThingsKit 命令下发、SQLite 缓存、SLE IPC、Gateway 文档/review 同步。
- 固定路径：repo root 为 `/home/sueiny/rk3506_linux6.1_v1.2.0`，项目 root 为 `app/Gateway`，板端 root 为 `/userdata/gateway`。
- 工作流：先读 `driver.sh`、当前配置、命令下发模块、SLE IPC 模块，再运行构建/推送/监听/测试。
- 命令下发验证：保持 `gateway.log` 实时监听，依次确认 MQTT 连接、订阅 topic、收到 payload、命令校验、路由执行、设备发送和响应发布。
- 已知坑：`gatewayd` 使用抽象 Unix Socket；旧 `ipc_send` 使用普通路径 socket 时，`connect: No such file or directory` 不等同于运行期 SLE IPC 故障。

## embedded-gateway-dev 要点

- 代码风格：命名空间 `gateway::module`，类名 PascalCase，函数 camelCase，成员变量 `snake_case_`。
- 架构边界：`MqttCloudClient` 只负责 MQTT 协议；`ThingsKitCodec` 只做 JSON 编码；`NetManager` 只管网络；`CacheStore`/`DeviceStateStore` 只管 SQLite 状态。
- 资源管理：构造函数只保存配置，实际 I/O 放到 `init()`/`connect()`；资源类删除拷贝；锁使用 RAII。
- CMake：C++17、显式列源文件、支持 `GATEWAYD_MOSQUITTO_ROOT` 和 `GATEWAYD_SQLITE_ROOT`，面向 RK3506 armhf sysroot。
- 性能审查重点：MQTT 回调不能做重活，线程间只入队；SQLite 写入需要控制批量和备份频率；日志要避免阻塞热路径。

## run-gateway 要点

常用命令在 `app/Gateway` 下执行：

```bash
bash .claude/skills/run-gateway/driver.sh build-sle
bash .claude/skills/run-gateway/driver.sh build-gw
bash .claude/skills/run-gateway/driver.sh push
bash .claude/skills/run-gateway/driver.sh test
bash .claude/skills/run-gateway/driver.sh full
```

前置条件：

- ARM 交叉编译器位于 SDK `prebuilts/gcc/.../arm-none-linux-gnueabihf-*`。
- Buildroot sysroot 需要有 mosquitto 和 SQLite。
- 板端路径统一使用 `/userdata/gateway`。
- ADB 设备在线后才能执行 push/test。

## 已知注意事项

- `gatewayd` 数据 IPC 使用抽象 Unix Socket，日志中显示为 `@var/run/gateway/sle_data.sock`。
- `sle_data_app` 的 `ipc_sender` 与 gatewayd 抽象 socket 语义一致。
- `gatewayd/test/ipc_send.c` 已兼容普通文件系统 socket 和抽象 socket；`driver.sh test` 继续传 `/var/run/gateway/sle_data.sock` 时，工具会在普通连接失败后自动尝试抽象 socket。
- `driver.sh test` 会清理旧 gateway 日志并等待当前 gatewayd 的 data socket 监听成功，再检查 MQTT 和发送测试 payload，避免旧日志误判。
- `driver.sh full` 能完成构建、推送、启动 gatewayd、等待 socket、等待 MQTT 和发送 SLE IPC 测试帧。
- 板端无 Python，二进制测试数据应在 host 端生成后推送。

## 推荐使用顺序

1. 先读工程文档和 skill，确认模块边界。
2. 做静态 code review，优先看线程、队列、IPC、SQLite、MQTT。
3. 用 `driver.sh full` 做上板验证。
4. 若 `ipc_send` 仍失败，先检查 gatewayd 是否已监听 socket，再改用运行期一致的 `sle_data_app` mock 数据链路确认 gatewayd 收包和上云。
5. 把验证结果归档到 CodeReview 和上板测试记录。
