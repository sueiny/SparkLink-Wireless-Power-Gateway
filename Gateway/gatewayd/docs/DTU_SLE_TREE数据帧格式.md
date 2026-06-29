# DTU SLE Tree 数据帧格式

> Status: Internal protocol reference.
> Authority: DTU SLE Tree 内部帧可参考本文；gateway-root 对接协议不以本文为准。
> Superseded by: [ST帧对接规定.md](ST帧对接规定.md) for gateway-root `0x02/0x05/0x06` usage.
> Last verified against: DTU SLE Tree source references listed below; gateway-root constraints rechecked on 2026-06-28.

本文整理 `src/application/samples/custom/dtu/run/mesh` 中的 SLE Tree 组网协议帧格式。内容以当前源码为准，主要参考：

- `run/mesh/ST_test_internal.h`
- `run/mesh/ST_test_proto.c`
- `run/mesh/ST_test_link.c`
- `run/mesh/ST_test_route.c`
- `run/dtu_run.c`

## 总览

DTU 的 SLE Tree 协议包含两类数据：

| 类型 | 承载位置 | 用途 |
| --- | --- | --- |
| 广播 TLV | SLE announce / seek result | 父节点发现、角色识别、容量和层级信息发布 |
| 树内数据帧 | SLE SSAP property write / notify | 心跳、业务透传、拓扑摘要、深度更新 |

DTU RUN 模式业务数据不再额外封包。`UART0` 或 `485` 收到的数据会原样作为 `DATA` 帧 payload 注入 SLE Tree；单帧最大 payload 为 `243` 字节，超过后由 `dtu_run.c` 按片发送。

网关对接约束：`gatewayd` 与 root 的业务/拓扑对接只使用 `DATA(0x02)`、`DTU_NETWORK_TOPOLOGY(0x05)`、`EXTERNAL_DEVICE_MAP(0x06)`。本文中的 `HEARTBEAT(0x01)`、`TOPO_SUMMARY(0x03)`、`DEPTH_UPDATE(0x04)` 是 DTU SLE Tree 内部协议说明，不作为后续 gateway-root 主链路帧。root 对接实现以 `ST帧对接规定.md` 为准。

## 公共常量

| 名称 | 值 | 说明 |
| --- | ---: | --- |
| `SLE_TREE_MAGIC0` | `0x53` | ASCII `S` |
| `SLE_TREE_MAGIC1` | `0x54` | ASCII `T` |
| `SLE_TREE_PROTO_VERSION` | `0x01` | 当前协议版本 |
| `SLE_TREE_MAX_FRAME_LEN` | `256` | SSAP property 承载的最大树内帧长度 |
| `SLE_TREE_FRAME_HEADER_LEN` | `13` | 树内帧固定头长度 |
| `SLE_TREE_MAX_PAYLOAD_LEN` | `243` | `256 - 13` |
| `SLE_TREE_ANY_NODE_ID` | `0x0000` | 通配目的节点，心跳和深度更新会用到 |
| `SLE_TREE_INVALID_NODE_ID` | `0x0000` | 无效节点号 |

所有 16-bit 字段均为小端序：低字节在前，高字节在后。

## 节点角色

| 角色 | 值 | 说明 |
| --- | ---: | --- |
| `SLE_TREE_ROLE_ROOT` | `1` | 网关/root |
| `SLE_TREE_ROLE_RELAY` | `2` | 中继节点，也可作为叶子业务节点使用 |
| `SLE_TREE_ROLE_LEAF` | `3` | 叶子节点，DTU 当前主要使用 root/relay 两类固件 |

## 广播 TLV 格式

root 和已入网 relay 会通过广播发布可接入信息，供其他节点扫描选父。广播数据采用 TLV：

```text
Length(1) Type(1) Value(N)
```

其中 `Length = Type 字节数 + Value 字节数`，因此总字段长度为 `1 + Length`。

### 广播包字段

| Type | 名称 | Value 长度 | Value 说明 |
| ---: | --- | ---: | --- |
| `0x01` | Discovery Level | 1 | `SLE_ANNOUNCE_LEVEL_NORMAL` |
| `0x02` | Access Mode | 1 | 当前固定为 `0` |
| `0x0B` | Complete Local Name | 0..16 | 当前节点名称，如 `TREE_ROOT` / `TREE_RELAY` |
| `0xFF` | Manufacturer Specific | 10 | SLE Tree 自定义元数据 |

### Manufacturer Specific 元数据

| 偏移 | 长度 | 字段 | 说明 |
| ---: | ---: | --- | --- |
| 0 | 1 | `magic0` | 固定 `0x53` |
| 1 | 1 | `magic1` | 固定 `0x54` |
| 2 | 1 | `version` | 固定 `0x01` |
| 3 | 1 | `role` | 当前节点角色 |
| 4 | 2 | `node_id` | 当前节点 ID，小端 |
| 6 | 1 | `free_slots` | 当前还能接入的子节点数量 |
| 7 | 2 | `root_node_id` | 当前树的 root 节点 ID，小端 |
| 9 | 1 | `depth` | 当前节点在树中的深度，root 为 `0` |

### Scan Response 字段

| Type | 名称 | Value 长度 | Value 说明 |
| ---: | --- | ---: | --- |
| `0x0C` | TX Power Level | 1 | 当前固定为 `SLE_TREE_ADV_TX_POWER`，值为 `20` |

## 树内通用帧头

树内帧通过 SLE SSAP property 承载：

- 子节点到父节点：`ssapc_write_req`
- 父节点到子节点：`ssaps_notify_indicate`

固定头为 13 字节：

| 偏移 | 长度 | 字段 | 说明 |
| ---: | ---: | --- | --- |
| 0 | 1 | `magic0` | 固定 `0x53` |
| 1 | 1 | `magic1` | 固定 `0x54` |
| 2 | 1 | `version` | 固定 `0x01` |
| 3 | 1 | `frame_type` | 帧类型 |
| 4 | 1 | `src_role` | 源节点角色 |
| 5 | 2 | `src_node_id` | 源节点 ID，小端 |
| 7 | 2 | `dst_node_id` | 目的节点 ID，小端 |
| 9 | 2 | `seq` | 序号，小端 |
| 11 | 2 | `payload_len` | payload 长度，小端 |
| 13 | N | `payload` | 载荷，长度为 `payload_len` |

解析规则：

- `magic0/magic1/version` 不匹配则丢弃。
- `payload_len + 13 > buffer_len` 则丢弃。
- `DATA` 帧使用独立的 `next_data_seq` 递增序号。
- 非 `DATA` 帧使用 `next_seq` 递增序号。

## 帧类型

| `frame_type` | 名称 | payload 长度 | 用途 |
| ---: | --- | ---: | --- |
| `1` | `HEARTBEAT` | 0 | 上行心跳、路由刷新 |
| `2` | `DATA` | 0..243 | DTU 业务透传数据 |
| `3` | `TOPO_SUMMARY` | `3 + child_count * 3` | relay 向 root 上报局部拓扑 |
| `4` | `DEPTH_UPDATE` | 1 | 父节点通知子节点更新深度 |
| `5` | `DTU_NETWORK_TOPOLOGY` | 文本 | gateway-root 对接帧：root 上报完整 DTU 树拓扑文本，详见 `ST动态拓扑上报帧05_06设计与改造计划.md`。 |
| `6` | `EXTERNAL_DEVICE_MAP` | 文本 | gateway-root 对接帧：root 上报 DTU 与外接设备键值对文本，详见 `ST动态拓扑上报帧05_06设计与改造计划.md`。 |

## HEARTBEAT 帧

### 通用头字段

| 字段 | 值 |
| --- | --- |
| `frame_type` | `1` |
| `dst_node_id` | `SLE_TREE_ANY_NODE_ID`，即 `0x0000` |
| `payload_len` | `0` |

### 行为

- relay/leaf 周期性向父节点发送。
- 父节点收到后学习或刷新 `src_node_id -> conn_id` 路由。
- relay 收到子节点 heartbeat 后会继续上行转发；如果 uplink 未就绪，则进入上行缓存队列。
- root 收到后用于刷新路由和拓扑活动时间。

### 示例

节点 `0x0011` relay 发 heartbeat：

```text
53 54 01 01 02 11 00 00 00 01 00 00 00
```

含义：

- `53 54 01`：协议头
- `01`：HEARTBEAT
- `02`：源角色 relay
- `11 00`：源节点 `17`
- `00 00`：通配目的节点
- `01 00`：seq = 1
- `00 00`：无 payload

## DATA 帧

### 通用头字段

| 字段 | 值 |
| --- | --- |
| `frame_type` | `2` |
| `dst_node_id` | 目标节点 ID |
| `payload_len` | DTU 原始业务数据长度，最大 `243` |

### Payload

payload 是 DTU RUN 模式透传数据本体，不包含额外 DTU 子协议头：

- 普通节点 `485 -> SLE Tree -> root`：485 数据原样进入 payload。
- root `UART0/485 -> SLE Tree -> 下级节点`：输入数据原样进入 payload。
- 单次输入超过 243 字节时，`dtu_run.c` 拆成多帧 `DATA`，接收端按多次回调处理，当前协议帧内没有分片编号或重组头。

### 路由行为

- 当前节点是目的节点：调用 `sle_tree_report_data()`。
- relay 发现目标在下游路由表中：通过 notify 下发到对应 child。
- relay 不知道下游路由或目标在上游：通过 uplink write 上行。
- uplink 不可用时，client 角色会把完整树内帧放入 `frame_queue`。
- root 下行广播不是协议层广播，而是 `dtu_run_root_broadcast()` 遍历 root 已学习路由，对每个目标节点分别发送 `DATA` 帧。

### 示例

root 节点 `1` 向节点 `17` 发送 ASCII `ABC`：

```text
53 54 01 02 01 01 00 11 00 01 00 03 00 41 42 43
```

## TOPO_SUMMARY 帧

relay 周期性或拓扑变化时向 root 上报自己的直连子节点列表。

### 通用头字段

| 字段 | 值 |
| --- | --- |
| `frame_type` | `3` |
| `dst_node_id` | `root_node_id` |
| `src_role` | 通常为 `2` relay |

### Payload

```text
owner_node_id(2 LE)
child_count(1)
child[0].node_id(2 LE)
child[0].role(1)
...
child[N-1].node_id(2 LE)
child[N-1].role(1)
```

| 偏移 | 长度 | 字段 | 说明 |
| ---: | ---: | --- | --- |
| 0 | 2 | `owner_node_id` | 发送该摘要的 relay 节点 ID |
| 2 | 1 | `child_count` | 后续 child 条目数量 |
| 3 | 2 | `child[0].node_id` | 第 1 个直连子节点 ID |
| 5 | 1 | `child[0].role` | 第 1 个直连子节点角色 |
| 6 | 3 | `child[1]` | 依次类推 |

长度必须满足：

```text
payload_len >= 3 + child_count * 3
```

### root 处理

- root 根据 `owner_node_id` 刷新 relay 拓扑项。
- 对每个 child 条目设置 `parent_node_id = owner_node_id`。
- 对同一个 owner 之前存在但本次未上报的 child，会被认为已不再挂在该 relay 下并清理。

### 示例

relay `17` 上报两个子节点：`18` relay、`55` leaf：

```text
Payload:
11 00 02 12 00 02 37 00 03
```

含义：

- `11 00`：owner = 17
- `02`：child_count = 2
- `12 00 02`：child 18，role relay
- `37 00 03`：child 55，role leaf

## DEPTH_UPDATE 帧

父节点重连后深度变化时，用于通知直连子节点更新自身深度。

### 通用头字段

| 字段 | 值 |
| --- | --- |
| `frame_type` | `4` |
| `dst_node_id` | `SLE_TREE_ANY_NODE_ID`，即 `0x0000` |
| `payload_len` | `1` |

### Payload

| 偏移 | 长度 | 字段 | 说明 |
| ---: | ---: | --- | --- |
| 0 | 1 | `new_parent_depth` | 发送方当前 depth |

接收方计算：

```text
new_depth = new_parent_depth + 1
```

relay 收到后：

- 更新自己的 `uplink.depth`。
- 如果超过 `SLE_TREE_MAX_DEPTH`，断开父节点并重新选父。
- 否则继续向所有直连子节点转发 `DEPTH_UPDATE`。

leaf 收到后：

- 更新自己的 `uplink.depth`。
- 如果超过 `SLE_TREE_MAX_DEPTH`，断开并重新选父。

## 上行帧缓存队列

client 角色在 uplink 不可用时，会缓存完整树内帧，而不是只缓存 payload。

| 名称 | 值 | 说明 |
| --- | ---: | --- |
| `SLE_TREE_FRAME_QUEUE_LEN` | `16` | 最多缓存 16 帧 |
| 单项最大长度 | `256` | 与 `SLE_TREE_MAX_FRAME_LEN` 一致 |

队列满时丢弃最旧帧。uplink 恢复后每轮最多 flush 4 帧。

## DTU RUN 模式映射

| 来源 | 当前角色 | 行为 | 生成帧 |
| --- | --- | --- | --- |
| `UART0` | root | PC 下行，遍历已学习路由广播到所有下级节点 | 多个 `DATA` 帧 |
| `UART0` | relay/leaf | 本地调试注入，上行发往 root | `DATA` 帧 |
| `485` | root | 485 总线数据下发到所有已学习节点 | 多个 `DATA` 帧 |
| `485` | relay/leaf | 现场设备数据上行发往 root | `DATA` 帧 |
| SLE Tree 收到 `DATA` | root | 输出到 PC 观察口，标记源节点 | 不再转发到 485 |
| SLE Tree 收到 `DATA` | relay/leaf 且目的为本机 | 输出到 PC 观察口，并原样转发到本地 485 | 不再重新封包 |

## 字节序和边界条件

- 所有 `uint16_t` 字段均使用 little-endian。
- `payload_len` 最大为 `243`。
- `DATA` 帧序号和控制帧序号分开递增。
- 广播 `root_node_id` 对 root 来说等于自身 `node_id`；relay 未入网时为 `0`，入网后为上行 root。
- 广播 `depth` 对 root 为 `0`；relay 入网后为自身深度；未入网时返回 `SLE_TREE_MAX_DEPTH`。
- `SLE_TREE_ANY_NODE_ID` 与 invalid node 都是 `0x0000`，上下文决定含义。

## 快速抓包识别

树内帧最小特征：

```text
53 54 01 <type> <src_role> <src_id_lo> <src_id_hi> <dst_lo> <dst_hi> <seq_lo> <seq_hi> <len_lo> <len_hi> ...
```

常见 `type`：

- `01`：心跳
- `02`：DTU 业务数据
- `03`：拓扑摘要
- `04`：深度更新

广播元数据最小特征是在 manufacturer specific value 内看到：

```text
53 54 01 <role> <node_id_lo> <node_id_hi> <free_slots> <root_lo> <root_hi> <depth>
```
