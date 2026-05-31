# 测试计划

## 构建验证

```bash
make -C app/Gateway/sle_data_app clean
make -C app/Gateway/sle_data_app
find app/Gateway/sle_data_app/src -name '*.o'
find app/Gateway/sle_data_app/build -name '*.o'
rg "health_check|rssi_fail|sle_read_remote_device_rssi|read_rssi_cb" \
  app/Gateway/sle_data_app/src app/Gateway/sle_data_app/inc app/Gateway/sle_data_app/sle_data_app.json
rg "pthread_create|Thread entry points|maintenance_manager_worker|notify_log_worker|scan_restart_worker" app/Gateway/sle_data_app/src
```

期望：

- `make` 通过并生成 `app/Gateway/sle_data_app/sle_data_app`。
- `src/*.o` 为空。
- 对象文件只出现在 `build/`。
- 不再有 RSSI 主动健康检查逻辑。
- 长期 worker 和短期 scan restart worker 的创建点集中、可搜索。

## 部署

```bash
make -C app/Gateway/sle_data_app push
```

`push` 会同步：

- 可执行文件到 `/userdata/gateway/bin/sle_data_app`
- 配置到 `/userdata/gateway/config/sle_data_app.json`

## 真实 DTU 1 分钟监听

```bash
adb shell 'killall sle_data_app 2>/dev/null || true; sleep 1; rm -f /tmp/sle_app.log /tmp/sle_stack_raw.log; timeout 60 /userdata/gateway/bin/sle_data_app --config /userdata/gateway/config/sle_data_app.json'
adb pull /tmp/sle_app.log app/Gateway/sle_data_app/test/sle_app.log
python3 app/Gateway/sle_data_app/test/analyze_log.py app/Gateway/sle_data_app/test/sle_app.log
```

期望：

- 三台目标 DTU 都进入 `READY`。
- 终端 stderr 能看到 `[SLE][STATUS]`、`[SLE][TABLE]`、`[SLE][TIMING]` 和 `[SLE][RX]` 镜像。
- `/tmp/sle_app.log` 持续写入三台设备的 `[SLE][RX]`。
- `/tmp/sle_stack_raw.log` 保存 SDK stdout 原始日志。
- `analyze_log.py` 可解析 `test/sle_app.log`，丢包统计符合现场预期。
- `timeout` 触发 `SIGTERM` 后打印 `sle_data_app exit`，无残留进程。

## 回归场景

### 单连接

- 启动 1 个 DTU。
- Gateway 能扫描到 MAC 前缀匹配 `mac_prefix` 的设备。
- 完成 connect、pair、MTU、服务发现。
- 进入 `READY` 后持续输出 `[SLE][RX]`。

### 一对三稳定连接

- `target_active_connections=3`。
- 三个不同 MAC 分配到独立 `server_index`。
- 第三个进入 `READY` 后连接表保持 3 个 active。
- 三台设备的 RX 不串到其他 `server_index`。

### 断开重连

- 断电任意 DTU。
- 对应 `server_index` 进入 `DISCONNECTED`。
- DTU 重启后复用原 `server_index`，更新新的 `conn_id`。
- 重连后重新执行 MTU 和发现，再进入 `READY`。

### 重复广播和满连接

- 同一地址重复广播不能重复创建 `server_index`。
- 正在连接或已连接时不能重复调用 connect。
- 达到目标连接数后停止补扫，不连接多余设备。

### 异常 RX

- 空包只打印 WARN，不崩溃。
- 未知 `conn_id` 只打印 WARN，不崩溃。
- 高速 notify 下 SDK 回调不阻塞；队列满时允许出现 drop WARN。

## 日志兼容性

保持以下格式不变：

```text
[SLE][RX] server_index=... conn_id=... mac=... rx_count=... len=... hex=... ascii=...
```

`test/analyze_log.py` 依赖该格式。
