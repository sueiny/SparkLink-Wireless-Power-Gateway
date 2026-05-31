# 一对多连接流程

本文只描述 `sle_data_app` 当前 Gateway 侧行为。真实 DTU 工程不在本目录内，本 app 不修改 DTU 参数。

## 总流程

```text
main()
  -> load sle_data_app.json
  -> redirect stdout to /tmp/sle_stack_raw.log
  -> notify_printer_start()
  -> sle_manager_init()
      -> register_callbacks()
      -> enable_sle()
      -> sle_enable_cb()
          -> set_default_connection_params()
          -> ssapc_register_client()
          -> start_scan()
  -> maintenance_manager_start()
  -> wait SIGINT/SIGTERM
```

maintenance manager 每秒调用 `sle_manager_tick()`，处理连接流程超时和 stale 检测。退出时先停止 maintenance manager，再 `sle_manager_deinit()` 停止扫描和 SDK 回调，最后 `notify_printer_stop()` drain 剩余 RX。

## 扫描与候选缓存

```text
seek_result_cb()
  -> match MAC prefix or fallback name
  -> candidate_update(addr, rssi, now)
  -> candidate_try_start_best()
```

候选表保存目标 MAC、RSSI、首次/最近发现时间、失败降权时间和连接起点。连接失败的候选会短期降权，调度器优先尝试未降权、RSSI 更好、最近出现的设备。

## 单 link-create 限制

```text
candidate_try_start_best()
  -> reject if pending connect exists
  -> reject if any server is CONNECTING/PAIRING
  -> choose best candidate
  -> prepare_pending_connect()
  -> stop seek if scan is active
  -> seek_disable_cb()
  -> issue_pending_connect()
  -> sle_connect_remote_device()
```

当前明确不并发调用 `sle_connect_remote_device()`。这样可以降低底层 `0x1401` 超时和 SDK 状态竞争风险。已连接设备的 MTU、发现、参数更新可以继续进行，但新的 link-create 仍保持串行。

## 单设备连接状态

```text
connect_state_changed_cb(CONNECTED)
  -> server_connections_mark_connected()
  -> pair if pair_state == SLE_PAIR_NONE
  -> otherwise request_exchange_info()

pair_complete_cb()
  -> request_exchange_info()

exchange_info_cb()
  -> mark mtu_done
  -> mark DISCOVERING
  -> start_property_discovery()

find_service_cb()
  -> cache service handles
  -> start_property_only_discovery()

find_property_cb()
  -> match data_property_uuid or auto-select data channel
  -> cache notify/write handle

find_complete_cb()
  -> mark READY
  -> print [SLE][TIMING] ready
  -> request_connection_param_update()
  -> request_scan_restart() when capacity remains
```

`wait_param_update_before_scan=false` 时，READY 后不等待参数更新回调即可继续补扫下一台。参数更新请求仍会发出，回调只解除超时门控并记录状态。

## Notify 数据路径

```text
notification_cb() / indication_cb()
  -> find server_index by conn_id
  -> reject empty payload
  -> server_connections_record_rx()
  -> notify_printer_enqueue_packet()

notify log worker
  -> format [SLE][RX]
  -> write stderr mirror
  -> write /tmp/sle_app.log
```

SDK 回调内不做 `fprintf`、文件写入或 hex/ascii 格式化。队列满时丢弃新包并累计 drop 计数，优先保护回调实时性。

## 失败恢复

```text
connect failure or connecting timeout
  -> candidate_mark_failed()
  -> mark DISCONNECTED
  -> try next candidate or restart scan

auth/pair failure
  -> remove paired device
  -> mark DISCONNECTED
  -> restart scan

discovery timeout
  -> disconnect remote device
  -> mark DISCONNECTED
  -> restart scan

READY stale timeout
  -> disconnect remote device
  -> mark DISCONNECTED
  -> restart scan
```

`early_connect_abort_ms` 默认是 `0`，表示关闭。只有确认底层 SDK 能可靠中止卡住的 link-create 后，才建议现场临时打开验证。
