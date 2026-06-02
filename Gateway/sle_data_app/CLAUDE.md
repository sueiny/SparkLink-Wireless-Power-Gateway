# sle_data_app 工程约束

> 适用范围：RK3506 SLE 数据采集进程 `sle_data_app`
> 语言：纯 C（不使用 C++）
> 当前阶段：SLE 协议栈管理 + Unix Socket IPC 数据转发 + 日志输出
> 最高优先级：稳定可运行、可读可维护、多注释、拒绝过度设计

---

## 1. 总原则

本项目必须以 **稳定连接 DTU、可靠转发数据、容易调试定位** 为目标。

必须遵守：

1. 每个 `.c` 文件只负责一类明确职责。
2. 文件之间通过 `*.h` 头文件的公开接口交互，禁止直接访问其他文件的 `static` 变量。
3. 代码必须容易读、容易改、容易定位问题。
4. 所有关键结构体、公开函数、核心流程、SLE SDK 回调必须写中文注释。
5. 不为了"设计模式"而设计模式。
6. 不为了"模块化"而拆出过细的文件。
7. 不做无意义抽象，不写空壳包装函数。
8. 不牺牲性能去追求形式上的架构漂亮。
9. 第一版优先保证稳定、清晰、可运行、可扩展。
10. 编译期默认值集中定义在 `kDefaultConfig`，修改配置需重新编译，不再支持运行时 JSON 加载。

---

## 2. 文件职责划分

| 文件 | 只允许负责 |
|---|---|
| `main.c` | 进程入口、信号等待、主循环 tick、模块启动/停止编排 |
| `sle_multi_client.c` | SLE 协议栈生命周期、扫描、连接状态机、SDK 回调、候选调度 |
| `server_connections.c` | 连接表 CRUD、状态标记、超时管理、快速重连支持 |
| `notify_printer.c` | 线程安全队列 + 日志格式化输出 + IPC 发送调用 |
| `ipc_sender.c` | Unix Socket 连接管理、帧序列化、自动重连 |
| `sle_app_config.c` | 编译期默认配置定义、配置打印 |

禁止：

```text
main.c 里直接操作连接表
notify_printer.c 里调用 SLE SDK
ipc_sender.c 里做日志格式化
server_connections.c 里做 IPC 发送
sle_app_config.c 里做运行时配置加载
```

---

## 3. 低耦合要求

必须保证：

1. `sle_multi_client.c` 不直接调用 `ipc_sender`，数据通过 `notify_printer` 的队列间接传递。
2. `ipc_sender` 不依赖 SLE SDK 头文件，只依赖 `ipc_protocol.h` 和标准库。
3. `server_connections` 不依赖 `notify_printer` 或 `ipc_sender`。
4. `notify_printer` 通过 `ipc_sender.h` 公开接口发送 IPC，不关心 Socket 实现细节。
5. 后续替换 IPC 为共享内存时，只改 `ipc_sender.c` 内部实现，不改 `notify_printer.c`。
6. 后续增加命令接收时，只新增 `ipc_cmd_receiver.c`，不改现有模块。

---

## 4. 可读性要求

代码以"后续自己和队友能快速看懂"为标准。

要求：

1. 函数名表达意图，用小写下划线命名法。
2. 变量名不要过度缩写（`conn` 可以，`c` 不行）。
3. 单个函数尽量控制在 50 行以内。
4. 复杂流程拆成 `static` 内部函数。
5. 不写过深嵌套（超过 3 层必须拆分）。
6. 不写一行超长逻辑（超过 100 字符换行）。
7. 不把多个职责塞进一个函数。
8. 日志信息要能帮助定位问题，格式统一为 `[SLE][TAG]`。

推荐函数名：

```c
server_connections_mark_disconnected()
candidate_try_start_best()
notify_printer_enqueue_packet()
ipc_sender_send_raw()
```

不推荐函数名：

```c
do_work()
process()
handle()
run()
```

---

## 5. 可维护性要求

必须为后续扩展留边界，但不能过度设计。

后续扩展方式：

| 后续功能 | 扩展方式 |
|---|---|
| 接入 gatewayd IPC | 已完成 `ipc_sender.c`，新增 `ipc_cmd_receiver.c` |
| Modbus 解析 | 新增 `modbus_parser.c`，由 gatewayd 侧调用 |
| 多 SLE 芯片 | 扩展 `sle_multi_client.c` 支持多 client_id |
| 动态配置 | 新增 `ipc_config_receiver.c`，接收 gatewayd 下发的配置 |
| 命令下发 | 新增 `ipc_cmd_receiver.c`，接收 `SleCommandFrame` 并调用 `ssapc_write_req` |

第一版不要提前写：

```text
抽象工厂
插件管理器
事件总线
复杂状态机框架
通用消息队列
```

---

## 6. 注释要求

必须注释：

1. 每个 `.c` 文件开头的模块职责说明。
2. 每个 `*.h` 公开函数的输入、输出、失败条件。
3. 核心主流程（连接建立、数据转发、超时处理）。
4. SLE SDK 回调的触发时机和上下文（SDK 线程 vs 应用线程）。
5. 线程安全相关逻辑（mutex 保护范围、volatile 语义）。
6. 为什么某个地方选择简单实现，而不是复杂框架。
7. 重要配置项的含义和默认行为。

推荐注释：

```c
/*
 * 候选调度器：从候选缓存中选择最佳设备发起连接。
 * 选择优先级：失败次数少 > RSSI 高 > 最近出现。
 * 一次只允许一个 pending connect，避免 SDK 0x1401 超时。
 */
static void candidate_try_start_best(void)
```

不推荐注释：

```c
// i++
i++;
```

注释语言：**中文**。

---

## 7. 线程安全要求

本进程只有 2 个线程，必须明确每段数据的归属：

| 线程 | 可访问的数据 | 同步方式 |
|---|---|---|
| main 线程 | `g_config`, `g_server_connections`, `g_candidates`, 连接状态机全局变量 | 无需加锁（唯一访问者） |
| notify_log 线程 | notify 队列、`g_ctx`（notify_printer 内部）、`ipc_sender` 的 fd 和 mutex | 队列用 mutex+condvar，ipc_sender 用 mutex |
| SLE SDK 回调线程（外部） | `g_server_connections`（读写）、notify 队列（只写） | 连接表用 `table->mutex`，队列用 `g_ctx.mutex` |

禁止：

```text
在 SDK 回调线程中调用 sle_manager_tick()
在 main 线程中直接调用 notify_printer_enqueue_packet()
在 notify_log 线程中修改 g_server_connections
```

---

## 8. 性能要求

第一版不是极限性能版本，但不能写明显低效的代码。

要求：

1. 不在 SDK 回调线程中做任何 I/O（打印、写文件、Socket 发送）。
2. notify 队列满时丢弃并计数，不阻塞 SDK 回调。
3. IPC 发送失败时不阻塞重试，按间隔异步重连。
4. 不在每秒 tick 中做动态内存分配。
5. 连接表操作尽量减少持锁时间（先拷贝快照再操作）。
6. 日志不要在高频路径输出过多无效内容（`SLE_VERBOSE` 受配置控制）。
7. 主循环必须可退出，`sigtimedwait` 有超时，不写死无限阻塞。

---

## 9. 禁止事项

```text
禁止在 sle_data_app 中引入 C++ 代码
禁止引入外部 JSON 库（cJSON、nlohmann 等）
禁止引入动态库依赖（除 SLE SDK 的 libsle_host.a）
禁止在代码中硬编码 IP 地址、端口号、MQTT topic
禁止使用 goto 跳出多层嵌套（只允许用于 main.c 的 cleanup 路径）
禁止在 SDK 回调中调用 pthread_mutex_lock（使用 trylock 或无锁队列）
```

---

## 10. 一句话版本

```text
sle_data_app 是纯 C 进程，负责 SLE 协议栈管理和数据转发。
必须稳定可运行、可读可维护，核心逻辑必须有中文注释；
线程边界必须清晰，SDK 回调线程禁止做 I/O；
拒绝过度设计，不引入不必要的抽象和依赖。
```
