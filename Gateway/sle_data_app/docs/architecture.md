# sle_data_app 架构说明

## 进程与线程

`sle_data_app` 是 Gateway 侧独立 SLE client 进程，通过 Unix Socket 接入 `gatewayd`。主线程负责初始化编译期默认配置、启动组件、等待退出信号和释放资源；当前不再支持运行时 JSON 配置加载。

运行时主要线程如下：

```text
main thread
  -> init compile-time config
  -> start notify_printer
  -> sle_manager_init()
  -> start maintenance manager
  -> wait SIGINT/SIGTERM
  -> sle_manager_deinit()

maintenance manager worker
  -> periodic sle_manager_tick()
  -> handle connecting/pairing/discovery/param/stale timeout

notify log worker
  -> consume notify queue
  -> write stderr mirror
  -> write /tmp/sle_app.log

SDK callback thread(s)
  -> seek/connect/SSAP/notify callbacks

scan restart worker
  -> short detached delay task
  -> restart scan outside SDK callbacks
```

SDK 回调不直接格式化大量 RX 数据。notify/indication 回调只做 `conn_id -> server_index` 查表、计数和入队，实际打印由 notify worker 完成。

## 模块职责

- `main.c`: 模式参数解析、stdout 重定向、信号等待、长期 worker 生命周期调度。
- `sle_multi_client.c`: SLE enable、扫描、候选缓存、连接、配对、MTU、服务发现、notify 入口和连接维护 tick。
- `server_connections.c`: 按稳定 MAC 维护 `server_index`，按运行期 `conn_id` 找回连接槽。
- `notify_printer.c`: 有界队列 + worker 线程，异步输出 `[SLE][RX]`。
- `sle_app_config.c`: 编译期默认值和配置打印。

## 输出路径

```text
terminal / stderr
  [SLE][STATUS]
  [SLE][TABLE]
  [SLE][TIMING]
  [SLE][RX] mirror

/tmp/sle_app.log
  [SLE][RX] 分析日志，供 test/analyze_log.py 解析

/tmp/sle_stack_raw.log
  SLE SDK stdout 原始日志
```

`main.c` 会把 stdout 重定向到 `/tmp/sle_stack_raw.log`。因此普通 `printf` 和 SDK stdout 不会混入终端，连接状态和 RX 镜像使用 stderr。

## 一对多连接策略

连接身份以 MAC 地址为准，`server_index` 是 app 内部稳定槽位，`conn_id` 是 SDK 每次连接后分配的运行期句柄。

扫描命中目标后先进入候选缓存。当前先按广播 MAC 前两字节 `02:00` 过滤，再解析 LTV 厂商字段：

```text
0B FF 53 54/44 54 01 role node_id_lo node_id_hi free_slots root_lo root_hi depth
```

只有 magic 为 `ST` 或 `DT` 且 `role=1(ROOT)` 的广播会进入候选。候选缓存记录 MAC、RSSI、广播 root_id、树类型、最近出现时间、失败降权时间和连接耗时起点。调度器从候选中选择一个设备发起连接。

当前策略仍然保持单 link-create：

```text
candidate -> stop seek -> sle_connect_remote_device()
```

不会并发调用多个 `sle_connect_remote_device()`，避免触发底层 `0x1401` 超时和连接表污染。一个设备完成 READY 后，如果目标连接数未满，再继续连接下一个候选。

## READY 条件

单个 DTU 进入 READY 需要完成：

```text
CONNECTED
  -> pair/auth if needed
  -> SSAP MTU exchange
  -> service discovery
  -> property discovery
  -> READY
  -> connection parameter update request
```

`wait_param_update_before_scan=false` 时，READY 后不等待参数更新回调即可继续补下一个连接；链路参数更新仍会发起，回调用于记录状态。

## 异常恢复

- connect 失败：对应候选按 `connect_fail_penalty_ms` 降权，优先尝试其他候选。
- pairing/auth 失败：删除配对信息，槽位退回 disconnected，后续重试。
- discovery 超时：断开并释放本次运行期状态。
- READY 后断线：保留地址和可复用配对信息，下次重连仍复用原 `server_index`。
- notify 队列满：不阻塞 SDK 回调，丢弃新包并输出 drop WARN。
