# sle_data_app 架构分析与改造计划

**版本**: v1.0
**日期**: 2026-05-24
**目标**: 分析 sle_data_app 当前架构缺陷，明确哪些设计需要精简，为接入 gatewayd 做准备

---

## 一、当前架构总览

```
sle_data_app/
├── src/
│   ├── main.c                  -- 进程入口、信号等待、维护线程
│   ├── sle_multi_client.c      -- SLE 协议栈管理（1344行，核心文件）
│   ├── server_connections.c    -- 连接表管理（582行）
│   ├── notify_printer.c        -- 日志输出队列（248行）
│   └── sle_app_config.c        -- JSON 配置解析（327行）
├── inc/
│   ├── sle_multi_client.h      -- 4 个公开函数
│   ├── server_connections.h    -- 47 个公开函数
│   ├── notify_printer.h        -- 3 个公开函数
│   └── sle_app_config.h        -- 配置结构体 + 30 个默认值宏
└── Makefile
```

**总代码量**：~2500 行 C 代码

**进程模型**：3 个线程
- main 线程：sigwait 阻塞等待退出信号
- maintenance 线程：每 1 秒调用 `sle_manager_tick()`
- notify_log 线程：消费 notify 队列，写 stderr + 日志文件

**SDK 回调**：运行在 SLE SDK 内部线程，不在上述 3 个线程内

---

## 二、缺点分析

### 2.1 【严重】sle_multi_client.c 上帝文件问题

**现状**：`sle_multi_client.c` 一个文件承担了 5 个独立职责：

| 职责 | 代码行数 | 应独立为 |
|------|---------|---------|
| SLE 协议栈生命周期（init/deinit/tick） | ~100 行 | sle_lifecycle.c |
| 扫描管理（start/stop/restart/candidate） | ~200 行 | scan_manager.c |
| 连接状态机（connect/pair/discover/ready） | ~400 行 | connection_fsm.c |
| SDK 回调注册与分发 | ~350 行 | sle_callbacks.c |
| 候选调度器（candidate cache + 选择算法） | ~150 行 | candidate_scheduler.c |

**问题**：
- 1344 行的单文件，任何改动都有引入回归的风险
- SDK 回调函数和业务逻辑混在一起，无法单独测试候选调度算法
- 全局变量 15 个（`g_config`, `g_server_connections`, `g_seek_cbk`, `g_connect_cbk`, `g_ssapc_cbk`, `g_client_app_uuid`, `g_client_id`, `g_sle_enabled`, `g_running`, `g_scan_active`, `g_has_pending_connect`, `g_pending_connect_index`, `g_pending_connect_addr`, `g_last_reported_active_count`, `g_candidates`, `g_first_connect_start_ms`, `g_core_lock`, `g_scan_restart_lock`, `g_scan_restart_pending`），全部通过文件作用域隐式耦合

### 2.2 【严重】过度依赖全局状态，无法单元测试

**现状**：所有模块间通信通过文件作用域全局变量：

```c
static sle_app_config_t g_config;              // 配置副本
static sle_server_connections_t g_server_connections;  // 连接表
static volatile uint8_t g_sle_enabled;         // 协议栈状态
static volatile bool g_running;                // 进程运行标志
static volatile bool g_scan_active;            // 扫描状态
static bool g_has_pending_connect;             // 待连接标志
static int g_pending_connect_index;            // 待连接索引
static sle_addr_t g_pending_connect_addr;      // 待连接地址
static sle_connect_candidate_t g_candidates[8]; // 候选缓存
// ... 还有 10 个
```

**问题**：
- 无法 mock SLE SDK 来测试候选调度逻辑
- 无法在不启动真实 SLE 硬件的情况下测试连接状态机
- `g_config` 在 init 时拷贝一份，但无法在运行时热更新

### 2.3 【中等】server_connections.h 暴露过多内部细节

**现状**：头文件声明了 47 个函数，其中：

| 类别 | 函数数 | 是否真正需要外部调用 |
|------|--------|-------------------|
| 状态标记（mark_*） | 7 个 | 仅 sle_multi_client.c 调用 |
| 属性设置（set_*） | 8 个 | 仅 sle_multi_client.c 调用 |
| 查询（find_*/get_*） | 6 个 | 需要 |
| 初始化/销毁 | 2 个 | 需要 |
| 调试（dump/get_stats） | 2 个 | 可选 |
| 快速重连 | 3 个 | 仅 sle_multi_client.c 调用 |
| 超时管理 | 3 个 | 仅 sle_multi_client.c 调用 |

**问题**：
- 47 个函数的头文件对 IPC 模块（未来调用方）完全不必要
- 外部模块可以绕过状态机直接修改连接状态，破坏不变量
- 应该隐藏为 `.c` 文件内部函数，只暴露必要的查询接口

### 2.4 【中等】notify_printer 的双重角色

**现状**：`notify_printer.c` 同时承担两个职责：
1. **队列管理**：线程安全的 bounded queue（生产者-消费者模式）
2. **日志格式化**：hex + ASCII 格式化输出到 stderr 和文件

**问题**：
- 当需要增加 IPC 发送时，必须修改这个"日志打印器"，语义矛盾
- 队列和格式化应分离：队列是通用基础设施，格式化和输出是具体消费者

### 2.5 【轻微】配置解析器脆弱

**现状**：`sle_app_config.c` 使用 `strstr` + `strtoul` 做 JSON 解析：

```c
static void load_uint16(const char *text, const char *key, uint16_t *value)
{
    const char *p = strstr(text, key);  // 找到 key
    p = strchr(p, ':');                  // 找冒号
    unsigned long parsed = strtoul(p + 1, NULL, 0);  // 解析数字
}
```

**问题**：
- 不处理嵌套对象、数组、字符串中包含 key 名等情况
- `"max_connections"` 会匹配到 `"target_active_connections"` 之后的任何包含该子串的位置（虽然当前 JSON 结构碰巧没触发）
- `load_uint8` 用 `SLE_DATA_APP_MAX_CONNECTIONS` 作为上限校验，但这不是通用的 uint8 上限（应该是 255）
- 缺少注释字段的跳过（JSON 中有注释会导致解析错误）

### 2.6 【轻微】时间函数重复实现

`now_ms()` 在 `sle_multi_client.c` 和 `server_connections.c` 中各实现了一份：

```c
// sle_multi_client.c
static uint64_t now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)tv.tv_usec / 1000ULL;
}

// server_connections.c
static uint64_t get_current_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000 + (uint64_t)tv.tv_usec / 1000;
}
```

功能完全相同，命名不同，应提取为共用工具函数。

### 2.7 【轻微】日志系统不统一

- `main.c` 用 `fprintf(stderr, "[SLE][STATUS] ...")`
- `sle_multi_client.c` 用 `printf("[SLE][ERROR] ...")` 和 `SLE_VERBOSE()` 宏
- `notify_printer.c` 用 `fprintf(stream, "[SLE][RX] ...")`
- `sle_app_config.c` 用 `printf("[SLE][WARN] ...")` 和 `fprintf(stderr, "[CONFIG][ERROR] ...")`

没有统一的日志级别控制，`SLE_VERBOSE` 只在 `sle_multi_client.c` 内有效。

---

## 三、哪些设计是冗余的

### 3.1 【可移除】notify_printer 的日志格式化功能

**原因**：接入 IPC 后，数据消费方从"写日志"变为"发 Socket"。日志格式化不再是核心路径。

**保留**：队列管理（bounded queue + 生产者-消费者模式）
**移除**：`print_packet_to_stream()` 中的 hex/ASCII 格式化逻辑
**替代**：IPC sender 直接从队列取原始 `notify_packet_t`，封装为 `SleRawFrame` 发送

### 3.2 【可精简】server_connections 的 47 个公开函数

**当前**：所有 `mark_*`、`set_*` 函数都是 public
**应改为**：
- 外部只用：`init`, `deinit`, `find_by_addr`, `find_by_conn_id`, `alloc_or_reuse`, `has_capacity`, `active_count`, `get_server_copy`, `record_rx`, `addr_to_string`
- 其余 30+ 个函数降为 `.c` 内部 `static`，或拆到 `connection_ops.c` 内部头文件

### 3.3 【可精简】sle_app_config.h 的 30 个默认值宏

**现状**：30 个 `#define SLE_APP_DEFAULT_*` 宏
**问题**：这些默认值只在 `sle_app_config_init_defaults()` 中使用一次
**应改为**：将默认值直接写在 `init_defaults()` 函数内，或用一个 `static const sle_app_config_t kDefaultConfig` 结构体一次性初始化

### 3.4 【可移除】fallback_name_filter 功能

**现状**：`sle_app_config.h` 有 `fallback_name_filter_enabled` 和 `fallback_target_name` 两个字段
**问题**：这是早期没有 MAC prefix 过滤时的兜底方案，现在 MAC prefix 已稳定工作
**建议**：移除 name filter 逻辑，减少 `seek_result_matches_target()` 的分支

### 3.5 【可精简】scan_restart_worker 每次创建 pthread

**现状**：每次扫描重启都 `pthread_create` + `pthread_detach` 一个新线程
**问题**：扫描重启是高频操作（每次连接完成/断开都会触发），频繁创建销毁线程开销不必要
**应改为**：用一个长驻的 scan worker 线程 + 条件变量唤醒，或复用 maintenance 线程

### 3.6 【可移除】enable_mcs / enable_phy_update / enable_large_data_len 配置

**现状**：这 3 个 PHY 配置字段在代码中读取但从未使用
**原因**：WS73 Linux SDK 当前版本不支持这些特性
**建议**：移除，等 SDK 支持后再加回

---

## 四、改造方案

### 4.1 目标架构

```
sle_data_app (改造后)/
├── src/
│   ├── main.c                   -- 进程入口（保持不变）
│   ├── sle_manager.c            -- 协议栈生命周期 + 公开 API（精简）
│   ├── scan_manager.c           -- 扫描控制 + 候选调度
│   ├── connection_fsm.c         -- 连接状态机
│   ├── sle_callbacks.c          -- SDK 回调注册与分发
│   ├── server_connections.c     -- 连接表（内部函数 static 化）
│   ├── ipc_sender.c             -- 【新增】Unix Socket 发送
│   ├── notify_queue.c           -- 【从 notify_printer 拆出】纯队列
│   ├── sle_app_config.c         -- 配置解析（精简默认值）
│   └── time_utils.c             -- 【新增】共用时间函数
├── inc/
│   ├── sle_manager.h            -- 公开 API（4 个函数，保持不变）
│   ├── server_connections.h     -- 精简为 ~15 个必要函数
│   ├── ipc_sender.h             -- 【新增】IPC 发送接口
│   ├── notify_queue.h           -- 【新增】纯队列接口
│   ├── time_utils.h             -- 【新增】时间工具
│   └── sle_app_config.h         -- 精简默认值宏
└── Makefile
```

### 4.2 改造步骤

#### 步骤 1：提取共用工具（1h）

- 新建 `time_utils.c/h`，统一 `now_ms()` 函数
- 从 `sle_multi_client.c` 和 `server_connections.c` 移除各自的 `now_ms()` / `get_current_time_ms()`

#### 步骤 2：拆分 notify_printer（2h）

- 将 `notify_printer.c` 拆为：
  - `notify_queue.c/h`：纯队列管理（enqueue/dequeue/start/stop）
  - `notify_printer.c`：日志格式化消费（保留向后兼容）
- 队列接口独立后，IPC sender 可以直接消费同一队列

#### 步骤 3：拆分 sle_multi_client.c（4h）

将 1344 行文件拆为 4 个模块：

| 新文件 | 职责 | 从原文件提取的函数 |
|--------|------|-------------------|
| `sle_manager.c` | 公开 API + 全局状态 | `sle_manager_init/deinit/tick/is_running` + 全局变量声明 |
| `scan_manager.c` | 扫描控制 | `start_scan`, `do_scan_restart`, `request_scan_restart`, `scan_restart_worker` |
| `connection_fsm.c` | 连接状态推进 | `prepare_pending_connect`, `issue_pending_connect`, `candidate_try_start_best`, `request_exchange_info`, `start_property_discovery` |
| `sle_callbacks.c` | SDK 回调 | `register_callbacks`, 所有 `*_cb` 函数 |

全局变量通过 `sle_manager.c` 中的 `sle_context_t` 结构体统一管理，各模块通过指针访问。

#### 步骤 4：精简 server_connections.h（1h）

- 将 30+ 个 `mark_*`/`set_*` 函数改为 `server_connections.c` 内部 `static`
- 或创建 `connection_ops_internal.h` 内部头文件，仅 `.c` 文件 include
- 公开头文件只保留 ~15 个必要函数

#### 步骤 5：精简配置（0.5h）

- 移除 `fallback_name_filter_enabled` / `fallback_target_name`
- 移除 `enable_mcs` / `enable_phy_update` / `enable_large_data_len`
- 将 30 个默认值宏合并为一个 `static const` 结构体

#### 步骤 6：新增 IPC sender（3h）

- 新建 `ipc_sender.c/h`
- 在 `notify_queue` 的消费循环中增加 IPC 发送路径
- 实现自动重连

#### 步骤 7：新增 IPC 命令接收（2h）

- 新建 `ipc_cmd_receiver.c/h`
- 监听命令 Socket，接收 `SleCommandFrame`
- 通过 `ssapc_write_req()` 转发到目标 DTU

### 4.3 改造前后对比

| 指标 | 改造前 | 改造后 |
|------|--------|--------|
| 文件数 | 5 个 .c | 10 个 .c |
| 最大单文件行数 | 1344 行 | ~400 行 |
| 全局变量数 | 19 个散落各处 | 1 个 `sle_context_t` 结构体 |
| 公开 API 函数 | 47 + 4 + 3 + 4 = 58 个 | ~15 + 4 + 3 + 4 + 3 = 29 个 |
| 可单元测试性 | 不可测试 | 候选调度、状态机可独立测试 |
| IPC 能力 | 无 | Unix Socket 双向通信 |

---

## 五、实施优先级

| 优先级 | 任务 | 理由 | 工时 |
|--------|------|------|------|
| P0 | 步骤 6: IPC sender | 打通数据通道是第一要务 | 3h |
| P0 | 步骤 2: 拆分 notify_printer | IPC sender 依赖独立队列 | 2h |
| P1 | 步骤 1: 提取 time_utils | 消除重复代码 | 1h |
| P1 | 步骤 3: 拆分 sle_multi_client | 降低维护风险 | 4h |
| P2 | 步骤 4: 精简 server_connections.h | 减少接口暴露 | 1h |
| P2 | 步骤 5: 精简配置 | 减少无用代码 | 0.5h |
| P3 | 步骤 7: IPC 命令接收 | 下行控制可后续补充 | 2h |
| **总计** | | | **~13.5h** |

**建议顺序**：先做 P0（步骤 2 → 6），保证 IPC 通道先通；再做 P1/P2 的重构；P3 最后补充。

---

## 六、相关文档

- `docs/gatewayd项目/SLE数据源替换MockDataSource计划.md` — gatewayd 侧改造计划
- `docs/gatewayd项目/网关后续架构规划.md` — 总体架构规划
- `sle_data_app/docs/architecture.md` — sle_data_app 原始架构文档

---

**文档维护者**: Gateway 开发团队
**最后更新**: 2026-05-24
