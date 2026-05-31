# server_connections 设计

## server_index 字段

每个 `server_index` 保存：

| 字段 | 含义 |
| --- | --- |
| `used` | `server_index` 是否已分配过地址 |
| `state` | `idle/connecting/connected/pairing/discovering/ready/disconnected` |
| `addr` | server SLE 地址 |
| `conn_id` | SDK 当前连接 ID |
| `pair_state` | SDK 配对状态 |
| `mtu_done` | 是否完成 MTU exchange |
| `discovery_done` | 是否完成服务/属性发现 |
| `notify_handle` | notify 属性 handle |
| `write_handle` | 写属性 handle |
| `last_rx_ms` | 最近一次收到 notify/indication 的时间 |
| `rx_count` | 收包计数 |
| `disconnect_reason` | 最近一次断线原因 |

## 分配规则

1. 扫描到设备后，先按地址查已有 `server_index`。
2. 如果地址已存在并处于 connected/connecting/pairing/discovering/ready，则不重复连接。
3. 如果地址已存在但 disconnected，则复用原 `server_index`。
4. 如果地址不存在，分配空 `server_index`。
5. 如果没有空 `server_index`，不连接新设备。

## `conn_id -> server_index`

`conn_id` 只在连接成功后才可信。连接表必须提供 `server_connections_find_by_conn_id()`，所有 MTU、发现、notify、read、write 回调都先通过 `conn_id` 查 `server_index`。

同一地址重连时，SDK 可能给出新的 `conn_id`。这时必须更新原 `server_index` 的 `conn_id`，不能新增 `server_index`。

## 断线回收

断线时按 `conn_id` 查 `server_index`，然后：

- 状态改为 disconnected。
- 保存 `disconnect_reason`。
- 清空 `conn_id`、`mtu_done`、`discovery_done`、`notify_handle`、`write_handle`。
- 保留地址，便于后续重连复用 `server_index`。

## 句柄变化处理

每次连接或重连后都必须重新执行 MTU exchange 和服务发现。旧 handle 不可复用，因为 server 固件升级、重启或服务表变化都可能导致 handle 改变。
