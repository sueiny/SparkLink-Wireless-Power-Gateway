# SLE Gateway 通信协议规范

## 1. 概述

本文档定义 SLE 树状网络中 **Root 节点 (WS63)** 与 **Gateway 大网关 (RK3506 Linux)** 之间的通信协议格式。

### 1.1 系统架构

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         Gateway (大网关)                                     │
│                         RK3506 Linux                                        │
│                         role = 4, node_id = 0x0000                         │
│                         最多连接 3 个 Root                                   │
└────────┬──────────────────────┬──────────────────────┬───────────────────────┘
         │                      │                      │
         │ SLE                  │ SLE                  │ SLE
         ▼                      ▼                      ▼
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│ Root 1          │    │ Root 2          │    │ Root 3          │
│ node_id = 0x0001│    │ node_id = 0x0002│    │ node_id = 0x0003│
└────────┬────────┘    └────────┬────────┘    └────────┬────────┘
         │                      │                      │
    SLE树网络               SLE树网络               SLE树网络
         │                      │                      │
    ┌────┴────┐            ┌────┴────┐            ┌────┴────┐
    │         │            │         │            │         │
 Relay 18  Relay 21     Relay 35  Relay 40     ...       ...
    │         │            │         │
    ▼         ▼            ▼         ▼
 DTU 101   DTU 102     DTU 103   DTU 104
```

### 1.2 关键设计点

| 项目 | 说明 |
|------|------|
| Gateway 与 Root 数量 | Gateway 最多连接 **3 个 Root** |
| Root ID 分配 | Root ID 固定，分别为 0x0001、0x0002、0x0003 |
| 通信方式 | Gateway 与 Root 之间通过 **SLE** 通信 |
| 路由依据 | Gateway 根据 Root 上报的 TOPO 摘要判断节点归属 |
| 下发寻址 | Gateway 根据 TOPO 信息找到目标 Root，再由 Root 路由到 DTU |

---

## 2. 角色定义

| 角色 | 角色ID | 说明 |
|------|--------|------|
| ROOT | 1 | SLE树网络根节点 (WS63)，ID 固定为 0x0001 / 0x0002 / 0x0003 |
| RELAY | 2 | SLE树网络中继节点 (WS63) |
| LEAF | 3 | SLE树网络叶子节点 (WS63) |
| GATEWAY | 4 | 大网关 (RK3506 Linux)，ID 固定为 0x0000 |

---

## 3. 帧格式定义

沿用 SLE 树网络帧格式，Gateway 与 Root 之间使用 SLE 通信。

### 3.1 帧头格式 (13 字节)

```
偏移    长度    字段            类型            说明
──────────────────────────────────────────────────────────────
0       2       magic           uint8_t[2]      固定 0x53 0x54 ("ST")
2       1       version         uint8_t         协议版本 0x01
3       1       frame_type      uint8_t         帧类型
                                                1 = HEARTBEAT (心跳)
                                                2 = DATA (业务数据)
                                                3 = TOPO_SUMMARY (拓扑摘要)
                                                4 = DEPTH_UPDATE (深度更新)
4       1       src_role        uint8_t         发送方角色
                                                1 = ROOT
                                                2 = RELAY
                                                3 = LEAF
                                                4 = GATEWAY
5       2       src_node_id     uint16_t        发送方节点ID (小端)
7       2       dst_node_id     uint16_t        目标节点ID (小端)
9       2       seq             uint16_t        序列号 (小端)
11      2       payload_len     uint16_t        载荷长度 (小端)
──────────────────────────────────────────────────────────────
```

### 3.2 固定节点 ID 分配

| 节点 | node_id | 说明 |
|------|---------|------|
| Gateway | 0x0000 | 大网关，固定 |
| Root 1 | 0x0001 | 第一个Root，固定 |
| Root 2 | 0x0002 | 第二个Root，固定 |
| Root 3 | 0x0003 | 第三个Root，固定 |
| Relay / DTU | 0x0010+ | 由各子网自行分配 |

---

## 4. DATA 帧 payload 格式

当 `frame_type = DATA (2)` 时，payload 承载 Modbus 数据：

```
偏移    长度    字段            类型            说明
──────────────────────────────────────────────────────────────
0       1       device_type     uint8_t         设备类型
                                                0x01 = 三相电表
                                                0x02 = 单相电表
                                                0x03 = 温湿度变送器
                                                0x04 = 继电器
                                                0x05 = 其他预留
2       1       modbus_len      uint8_t         Modbus RTU 帧长度
                                                范围: 1 ~ 241
3       N       modbus_rtu      uint8_t[N]      原始 Modbus RTU 帧
                                                包含 [addr][func][data][crc]
──────────────────────────────────────────────────────────────
总计: 2 + N 字节 (N ≤ 241)
```

**说明**：
- `device_type` 用于标识 DTU 下挂的设备类型，便于 Gateway 解析 Modbus 响应
- 上行数据：Root 通过帧头 `src_node_id` 标识来源，Gateway 根据 TOPO 表知道该 Root 下挂哪些 DTU
- 下行数据：Gateway 通过帧头 `dst_node_id` 指定目标 Root，由 Root 内部路由到具体 DTU

---

## 5. TOPO_SUMMARY 帧 payload 格式

当 `frame_type = TOPO_SUMMARY (3)` 时，Root 向 Gateway 上报拓扑摘要：

```
偏移    长度    字段            类型            说明
──────────────────────────────────────────────────────────────
0       2       node_id         uint16_t        本节点ID (小端)
2       1       child_count     uint8_t         直连子节点数量
3       3×N     children        结构体数组      子节点列表
                                                每项: [node_id(2B)][role(1B)]
──────────────────────────────────────────────────────────────
总计: 3 + 3×child_count 字节
```

---

## 6. 通信场景

### 6.1 Root 上报遥测数据 (Root → Gateway)

**场景描述**：DTU 采集的 Modbus 数据通过 SLE 树网络到达 Root，Root 再通过 SLE 发送给 Gateway。

**帧头字段**：

| 字段 | 值 | 说明 |
|------|-----|------|
| magic | 0x53 0x54 | 固定 |
| version | 0x01 | 固定 |
| frame_type | 2 (DATA) | 业务数据 |
| src_role | 1 (ROOT) | 发送方是 Root |
| src_node_id | 0x0001 / 0x0002 / 0x0003 | 哪个 Root 发的 |
| dst_node_id | 0x0000 | 目标是 Gateway |
| seq | 递增 | 序列号 |

**payload 字段**：

| 字段 | 值 | 说明 |
|------|-----|------|
| device_type | 0x02 | 单相电表 |
| modbus_len | 0x11 (17) | Modbus 帧长度 |
| modbus_rtu | [01 03 10 ...] | Modbus RTU 帧 |

**完整帧示例** (Root 1 上报单相电表数据)：

```
帧头:
  53 54        magic = "ST"
  01           version = 1
  02           frame_type = DATA
  01           src_role = ROOT
  01 00        src_node_id = 1 (Root 1)
  00 00        dst_node_id = 0 (Gateway)
  01 00        seq = 1
  13 00        payload_len = 19

payload:
  02           device_type = 单相电表
  11           modbus_len = 17
  01 03 10 ... modbus_rtu (17字节)
```

**Gateway 接收处理**：
1. 解析帧头，确认 `src_node_id = 0x0001` (Root 1)
2. 解析 payload，得到 Modbus RTU 帧
3. 根据 TOPO 表，Root 1 下挂 DTU 101、102
4. 通过 Modbus 协议栈解析响应数据，关联到对应设备

---

### 6.2 Root 上报拓扑摘要 (Root → Gateway)

**场景描述**：Root 周期性向 Gateway 上报自己管理的子网拓扑，Gateway 据此建立路由表。

**帧头字段**：

| 字段 | 值 | 说明 |
|------|-----|------|
| magic | 0x53 0x54 | 固定 |
| version | 0x01 | 固定 |
| frame_type | 3 (TOPO_SUMMARY) | 拓扑摘要 |
| src_role | 1 (ROOT) | 发送方是 Root |
| src_node_id | 0x0001 / 0x0002 / 0x0003 | 哪个 Root 发的 |
| dst_node_id | 0x0000 | 目标是 Gateway |
| seq | 递增 | 序列号 |

**payload 字段**：

| 字段 | 值 | 说明 |
|------|-----|------|
| node_id | 0x0001 | Root 自己的 ID |
| child_count | 3 | 有 3 个直连子节点 |
| child[0] | 0x0012, 0x02 | node_id=18, role=RELAY |
| child[1] | 0x0015, 0x02 | node_id=21, role=RELAY |
| child[2] | 0x0023, 0x02 | node_id=35, role=RELAY |

**Gateway 路由表构建**：

Gateway 收到多个 Root 的 TOPO_SUMMARY 后，构建全局路由表：

```
Root 1 上报: 自己下挂 Relay 18, Relay 21
Root 2 上报: 自己下挂 Relay 35, Relay 40
Root 3 上报: 自己下挂 Relay 50

Gateway 路由表:
  DTU 101 → Relay 18 → Root 1
  DTU 102 → Relay 21 → Root 1
  DTU 103 → Relay 35 → Root 2
  DTU 104 → Relay 40 → Root 2
  DTU 105 → Relay 50 → Root 3
```

---

### 6.3 Gateway 下发数据 (Gateway → Root)

**场景描述**：Gateway 向指定 Root 发送 Modbus 查询/控制请求，由 Root 内部路由到具体 DTU。

**帧头字段**：

| 字段 | 值 | 说明 |
|------|-----|------|
| magic | 0x53 0x54 | 固定 |
| version | 0x01 | 固定 |
| frame_type | 2 (DATA) | 业务数据 |
| src_role | 4 (GATEWAY) | 发送方是 Gateway |
| src_node_id | 0x0000 | Gateway 固定 ID |
| dst_node_id | 0x0001 / 0x0002 / 0x0003 | 目标 Root 的 ID |
| seq | 递增 | 序列号 |

**payload 字段**：

| 字段 | 值 | 说明 |
|------|-----|------|
| device_type | 0x02 | 单相电表 |
| modbus_len | 0x06 (6) | Modbus 帧长度 |
| modbus_rtu | [01 03 00 00 00 08 XX XX] | Modbus RTU 帧 |

**完整帧示例** (Gateway 下发查询到 Root 1)：

```
帧头:
  53 54        magic = "ST"
  01           version = 1
  02           frame_type = DATA
  04           src_role = GATEWAY
  00 00        src_node_id = 0 (Gateway)
  01 00        dst_node_id = 1 (Root 1)
  01 00        seq = 1
  08 00        payload_len = 8

payload:
  02           device_type = 单相电表
  06           modbus_len = 6
  01 03 00 00 00 08 XX XX  modbus_rtu (6字节)
```

**下发寻址流程**：

1. Gateway 需要查询 DTU 101
2. 查询路由表：DTU 101 属于 Root 1
3. 帧头 `dst_node_id = 0x0001` (Root 1)
4. 通过 SLE 发送给 Root 1
5. Root 1 内部根据 Modbus 地址路由到 DTU 101

---

## 7. Gateway 路由表设计

### 7.1 路由表结构

Gateway 维护一张路由表，记录每个 Root 下挂的 Relay/DTU：

| root_node_id | relay_node_id | 最近更新时间 |
|--------------|---------------|-------------|
| 0x0001 | 0x0012 | 1717234567 |
| 0x0001 | 0x0015 | 1717234567 |
| 0x0002 | 0x0023 | 1717234568 |
| 0x0002 | 0x0028 | 1717234568 |
| 0x0003 | 0x0032 | 1717234569 |

### 7.2 路由表更新

- **来源**：Root 上报的 TOPO_SUMMARY 帧
- **周期**：Root 周期性上报（建议 10 秒）
- **超时**：超过 60 秒未更新的条目标记为过期
- **重建**：Gateway 重启后等待各 Root 上报，重新构建路由表

### 7.3 下发寻址流程

```
Gateway 收到云端下发请求 (目标: DTU 101, Modbus FC 0x03)
    │
    ▼
查询路由表: DTU 101 属于 Root 1 下挂的 Relay 18
    │
    ▼
构建帧: src=GATEWAY(0x0000), dst=ROOT(0x0001)
    │
    ▼
通过 SLE 发送给 Root 1
    │
    ▼
Root 1 内部路由: Relay 18 → DTU 101
```

---

## 8. 常量定义

| 常量 | 值 | 说明 |
|------|-----|------|
| GATEWAY_ROLE_ID | 4 | Gateway 角色 ID |
| GATEWAY_NODE_ID | 0x0000 | Gateway 节点 ID |
| ROOT_1_NODE_ID | 0x0001 | Root 1 节点 ID |
| ROOT_2_NODE_ID | 0x0002 | Root 2 节点 ID |
| ROOT_3_NODE_ID | 0x0003 | Root 3 节点 ID |
| FRAME_MAGIC_0 | 0x53 | 帧头魔数第1字节 |
| FRAME_MAGIC_1 | 0x54 | 帧头魔数第2字节 |
| FRAME_VERSION | 0x01 | 协议版本 |
| FRAME_HEADER_LEN | 13 | 帧头长度 |
| APP_HEADER_LEN | 2 | 应用层头长度 (device_type + modbus_len) |
| MAX_FRAME_LEN | 256 | 最大帧长度 |
| MAX_PAYLOAD_LEN | 243 | 最大 payload 长度 |
| MAX_MODBUS_LEN | 241 | 最大 Modbus 帧长度 |
| MAX_ROOT_COUNT | 3 | 最大 Root 数量 |
| DEVICE_TYPE_THREE_PHASE_METER | 0x01 | 三相电表 |
| DEVICE_TYPE_SINGLE_PHASE_METER | 0x02 | 单相电表 |
| DEVICE_TYPE_TEMP_HUMIDITY | 0x03 | 温湿度变送器 |
| DEVICE_TYPE_RELAY | 0x04 | 继电器 |
| DEVICE_TYPE_OTHER | 0x05 | 其他预留 |

---

## 9. 约束与注意事项

### 9.1 长度约束

| 参数 | 值 | 说明 |
|------|-----|------|
| 帧头长度 | 13 字节 | 固定 |
| SLE payload 最大 | 243 字节 | 256 - 13 |
| 应用层头长度 | 2 字节 | device_type(1) + modbus_len(1) |
| Modbus 帧最大 | 241 字节 | 243 - 2 |

### 9.2 错误处理

| 错误场景 | 处理方式 |
|---------|---------|
| magic 不匹配 | 丢弃帧 |
| version 不匹配 | 丢弃帧 |
| payload_len 超出帧长 | 丢弃帧 |
| src_node_id 不是已知 Root | 丢弃帧 |
| dst_node_id 对应 Root 不在线 | 记录错误，等待重连 |

---

## 附录 A: 与 SLE 树网络的关系

| 项目 | SLE 树网络 | Gateway 协议 |
|------|-----------|--------------|
| 角色 | ROOT/RELAY/LEAF | 新增 GATEWAY=4 |
| Root 数量 | 1 个 | 最多 3 个 |
| 节点 ID | 动态分配 | Root ID 固定 |
| 通信方式 | SLE | SLE |
| payload | 透传 | 定义 Modbus 格式 |

## 附录 B: 版本历史

| 版本 | 日期 | 说明 |
|------|------|------|
| 1.0.0 | 2026-01 | 初始版本 |
