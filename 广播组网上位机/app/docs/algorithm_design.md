# Mesh 网络路由算法设计文档

> 版本: v1.8.0 | 更新时间: 2026-06-02

## 1. 系统概述

本系统是一个基于 SLE (Smart Link Extension) 协议的 Mesh 网络上位机测试平台，包含两种路由算法：

| 算法 | 包名 | 核心特点 |
|------|------|---------|
| Dijkstra | `pc_dijkstra_cli` | 经典最短路径算法，基于 RSSI 加权图 |
| D3QN | `pc_d3qn_cli` | 深度强化学习 (Dueling Double DQN + MPNN)，自适应路径选择 |

---

## 2. 硬件环境

### 2.1 节点拓扑

```
                    ┌─────────┐
                    │ 网关 00  │
                    │ Dongle   │
                    └────┬────┘
                         │ USB (0x10)
          ┌──────────────┼──────────────┐
          │              │              │
       ┌──┴──┐       ┌──┴──┐       ┌──┴──┐
       │ 03  │       │ 09  │       │ 05  │
       │ 良  │       │ 中  │       │ 弱  │
       └──┬──┘       └──┬──┘       └──┬──┘
          │              │              │
    ┌─────┼─────┐    ┌──┼──┐       ┌──┼──┐
    │     │     │    │     │       │     │
  ┌─┴─┐ ┌─┴─┐ ┌─┴─┐┌─┴─┐ ┌┴──┐ ┌─┴─┐ ┌┴──┐
  │01 │ │04 │ │06 ││02 │ │08 │ │10 │ │07 │
  │弱 │ │弱 │ │中 ││弱 │ │中 │ │中 │ │弱 │
  └───┘ └───┘ └───┘└───┘ └───┘ └───┘ └───┘
```

### 2.2 关键参数

| 参数 | 值 | 说明 |
|------|-----|------|
| 网关地址 | `0x00` (0) | 路由起点 |
| Dongle 地址 | `0x10` (16) | USB 串口适配器，需排除 |
| RSSI 范围 | -63 ~ -85 dBm | 信号强度范围 |
| 串口 | `/dev/ttyUSB0` | CH341 USB 转串口 |

### 2.3 RSSI 权重映射

| RSSI 范围 | 质量 | 权重 | 可靠模式权重 |
|-----------|------|------|-------------|
| ≥ -55 dBm | 优 | 1 | 1.0 |
| -56 ~ -65 dBm | 良 | 3 | 3.0 |
| -66 ~ -75 dBm | 中 | 6 | 6.0 |
| -76 ~ -80 dBm | 弱 | 12 | 16.0 |
| -81 ~ -85 dBm | 极弱 | 12 | 32.0 |
| < -85 dBm | 不可用 | N/A | N/A (过滤) |

---

## 3. 上位机完整测试流程

### 3.1 流程概览

```
┌─────────────────────────────────────────────────────────────────┐
│                        上位机测试流程                            │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌──────────────┐     ┌──────────────┐     ┌──────────────┐    │
│  │ 1. 启动等待  │ ──→ │ 2. 拓扑采集  │ ──→ │ 3. 路由计算  │    │
│  │ (20秒)       │     │ (RSSI_REQ)   │     │ (Dijkstra)   │    │
│  └──────────────┘     └──────────────┘     └──────────────┘    │
│         │                   │                   │               │
│         ▼                   ▼                   ▼               │
│  ┌──────────────┐     ┌──────────────┐     ┌──────────────┐    │
│  │ 等待设备启动 │     │ 收集所有节点 │     │ 计算最短路径 │    │
│  │ 校准完成     │     │ RSSI 信息    │     │ 生成路由表   │    │
│  └──────────────┘     └──────────────┘     └──────────────┘    │
│                                                                 │
│  ┌──────────────┐     ┌──────────────┐     ┌──────────────┐    │
│  │ 4. 矩阵测试  │ ──→ │ 5. 数据发送  │ ──→ │ 6. 结果统计  │    │
│  │ (源×目标)    │     │ (两跳发送)   │     │ (丢包/延时)  │    │
│  └──────────────┘     └──────────────┘     └──────────────┘    │
│         │                   │                   │               │
│         ▼                   ▼                   ▼               │
│  ┌──────────────┐     ┌──────────────┐     ┌──────────────┐    │
│  │ 遍历所有节点 │     │ 网关→源→目标 │     │ 生成报告     │    │
│  │ 对组合       │     │ 等待ACK确认  │     │ 汇总统计     │    │
│  └──────────────┘     └──────────────┘     └──────────────┘    │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### 3.2 详细流程

#### 阶段 1: 启动等待 (boot_wait)

```python
# 1. 初始化串口连接
client = SerialClient(port, baud)

# 2. 等待设备启动和校准
time.sleep(boot_wait)  # 默认 20 秒

# 3. 清空串口缓冲区
drain_messages(client, duration=2.0)
```

**关键点**：
- 设备启动需要时间（包括 Flash 初始化、SLE 初始化、校准等）
- 校准过程可能需要 60-120 秒
- 建议 `boot_wait=20` 秒以上

#### 阶段 2: 拓扑采集 (collect_topology)

```python
# 1. 发送 RSSI_REQ 命令
client.send_rssi_req()

# 2. 等待所有节点响应
messages = drain_until_quiet(
    client,
    idle_timeout=1.0,   # 1秒无新数据则结束
    max_seconds=10.0     # 最多等待10秒
)

# 3. 解析 RSSI_REPORT
for message in messages:
    if isinstance(message, RssiReport):
        topology.update_from_rssi_report(message)

# 4. 重复采集 N 次
for i in range(rssi_requests):  # 默认 2 次
    send_rssi_req()
    collect_responses()
```

**RSSI_REQ 命令格式**：
```
RSSI_REQ\r\n
```

**RSSI_REPORT 响应格式**：
```
HEX: 4D 53 01 01 <src_addr> <count> <addr1> <rssi1> <addr2> <rssi2> ...
TEXT: RSSI_REPORT src=<addr> count=<count> [<addr>:<rssi>] ...
```

**关键点**：
- 每个节点都会响应 RSSI_REQ（包括网关 00）
- 响应时间因节点而异（0.1-5秒）
- 必须等待所有节点响应，否则路由表不完整
- `idle_timeout=1.0` 确保等待足够长

#### 阶段 3: 路由计算

```python
# 1. 构建加权图
graph = topology.graph(
    route_mode="baseline_dijkstra",
    min_rssi=-85,           # 过滤弱链路
    max_hops=4              # 最大跳数
)

# 2. 计算所有节点对的路由
routes = build_route_table(graph, nodes, max_hops=4)

# 3. 保存路由表
save_topology("state.json", topology)
write_routes_json("routes.json", routes)
```

**拓扑过滤机制**：
```python
def graph(self, min_rssi=-85):
    for edge in self.edges.values():
        # 1. 过期边过滤
        if self.stale_seconds and timestamp - edge.updated_at > self.stale_seconds:
            continue
        # 2. 网关入边过滤（防止环路）
        if self.gateway is not None and edge.dst == self.gateway:
            continue
        # 3. 排除节点过滤（Dongle）
        if edge.src in self.relay_excluded or edge.dst in self.relay_excluded:
            continue
        # 4. 弱信号过滤
        if edge.rssi < min_rssi:
            continue
```

#### 阶段 4: 矩阵测试 (source × target)

```python
# 生成所有节点对组合
pairs = source_target_pairs(nodes, sources)

for source, target in pairs:
    for round_index in range(1, rounds + 1):
        # 1. 计算网关到源节点的路径
        gateway_route = topology.route(gateway, source)
        
        # 2. 计算源节点到目标节点的路径
        pair_route = _select_dijkstra_pair_route(topology, source, target)
        
        # 3. 合并路径（去重防环）
        full_path = _combine_gateway_path(gateway_route, pair_route)
        
        # 4. 发送数据
        send_and_wait_ack(dst=source, path=gateway_route)
        send_and_wait_ack(dst=target, path=full_path)
```

**路径选择算法**：
```python
def _select_dijkstra_pair_route(topology, source, target, ...):
    # 1. 枚举候选路径
    paths = _candidate_paths(graph, source, target, limit=5, max_hops=4)
    
    # 2. 过滤环路
    paths = [p for p in paths if len(p) == len(set(p))]
    
    # 3. 计算综合得分
    for path in paths:
        base_cost = _path_base_cost(graph, path)
        history_penalty = _history_penalty(...)
        hop_penalty = max(0, len(path) - 2) * 2.0  # 跳数惩罚
        weak_rssi_penalty = calc_weak_rssi_penalty(...)
        
    # 4. 选择得分最低的路径
    scored.sort(key=lambda item: (item[0], len(item[1])))
```

**路径合并（去重防环）**：
```python
def _combine_gateway_path(gateway_route, pair_route):
    combined = list(gateway_route)
    for node in pair_route[1:]:
        if node in combined:
            idx = combined.index(node)
            combined = combined[:idx]  # 截断到该节点之前
        combined.append(node)
    return combined
```

#### 阶段 5: 数据发送

```python
def _send_and_wait_ack(client, topology, dst, path, payload, ack_timeout):
    # 1. 构建 SEND 命令
    # 注意：目标地址用十进制格式（1-10），路径用十六进制格式（01-0A）
    command = f"SEND {dst} {len(path)} {' '.join(f'{x:02X}' for x in path)} {payload}\r\n"
    
    # 2. 发送命令
    client.write_command(command)
    
    # 3. 等待 ACK
    deadline = time.monotonic() + ack_timeout
    while time.monotonic() < deadline:
        for message in client.read_available():
            if isinstance(message, Ack) and message.src_addr == dst:
                return True, latency_ms, ack_seq
    
    return False, None, None  # 超时
```

**SEND 命令格式**：
```
SEND <dst> <path_len> <addr0> <addr1> ... <payload>\r\n
```

**重要说明**：
- `dst`：目标地址，使用**十进制格式**（1-10）
- `path`：路径，使用**十六进制格式**（01-0A）
- 示例：`SEND 10 2 00 0A aabbcc` 表示发送到节点 10（0x0A），路径 00 -> 0A

**ACK 响应格式**：
```
HEX: 4D 53 01 05 <src_addr> <dst_addr> <seq_high> <seq_low>
TEXT: ACK <src_addr> <seq>
```

**关键点**：
- 两跳发送：先发网关→源节点，再发网关→源节点→目标节点
- 点到点延时 = 目标节点ACK延时 - 源节点ACK延时
- ACK 超时默认 2.0 秒

#### 阶段 6: 结果统计

```python
# 统计每个节点对的结果
for result in results:
    if result.success:
        success_count += 1
    else:
        if result.status == "gateway_to_source_timeout":
            source_timeout += 1
        elif result.status == "gateway_to_target_timeout":
            target_timeout += 1
        elif result.status == "unreachable":
            unreachable += 1

# 计算丢包率
loss_rate = (total_sent - success) / total_sent

# 生成报告
write_report(log_dir, summary, topology)
```

**丢包率计算**：
```
总轮次 = 180 (10节点 × 2轮 × 9对)
路由不可达 = X (不计入总发送)
总发送 = 180 - X
成功 = Y
ACK 超时 = 总发送 - Y
丢包率 = (总发送 - 成功) / 总发送
```

---

## 4. Dijkstra 路由算法详解

### 4.1 算法流程

```
┌─────────────┐
│  收集拓扑    │ ← RSSI_REQ × N 次
└──────┬──────┘
       │
       ▼
┌─────────────┐
│  构建加权图  │ ← 过滤弱链路、排除节点
└──────┬──────┘
       │
       ▼
┌─────────────┐
│  Dijkstra   │ ← 最短路径计算
│  路径计算    │
└──────┬──────┘
       │
       ▼
┌─────────────┐
│  环路检测    │ ← 检测并避免环路
└──────┬──────┘
       │
       ▼
┌─────────────┐
│  跳数限制    │ ← 最大 4 跳
└──────┬──────┘
       │
       ▼
┌─────────────┐
│  发送数据    │ ← SEND <dst> <path_len> <path...> <payload>
└─────────────┘
```

### 4.2 候选路径选择

```python
def _candidate_paths(graph, src, dst, limit=5, max_hops=4):
    """使用 Dijkstra 枚举前 K 条候选路径"""
    queue = [(0.0, (src,))]
    paths = []
    seen = set()
    
    while queue and len(paths) < limit:
        cost, path_tuple = heapq.heappop(queue)
        if path_tuple in seen:
            continue
        seen.add(path_tuple)
        
        current = path_tuple[-1]
        if current == dst:
            paths.append(list(path_tuple))
            continue
        
        if len(path_tuple) - 1 >= max_hops:
            continue
        
        for neighbor, weight in sorted(graph.get(current, {}).items()):
            if neighbor in path_tuple:  # 环路检测
                continue
            heapq.heappush(queue, (cost + float(weight), path_tuple + (neighbor,)))
    
    return paths
```

### 4.3 惩罚机制

| 惩罚项 | 权重 | 触发条件 |
|--------|------|---------|
| 丢包率惩罚 | 1000 + 500×loss_rate | loss_rate ≥ 10% |
| 延时惩罚 | 500 + avg_latency | avg_latency ≥ 220ms |
| 跳数惩罚 | **2.0/hop** | 跳数 > 2 |
| **弱 RSSI 惩罚** | **10.0/链路** | **RSSI < -80dBm** |
| **中等 RSSI 惩罚** | **5.0/链路** | **RSSI < -75dBm** |

**注意**：已移除 `avoid_nodes` 惩罚（v1.3.0），避免选择过长路径。

### 4.4 环路检测

**三重环路防护**：

1. **候选路径生成时**：
```python
if neighbor in path_tuple:  # 已访问过的节点跳过
    continue
```

2. **路径选择时**：
```python
# 跳过包含环路的路径
if len(path) != len(set(path)):
    continue
```

3. **路径合并时**：
```python
# 去重防环
combined = list(gateway_route)
for node in pair_route[1:]:
    if node in combined:
        idx = combined.index(node)
        combined = combined[:idx]
    combined.append(node)
```

### 4.5 测试命令

```bash
python -m pc_dijkstra_cli bench \
    --port /dev/ttyUSB0 --baud 115200 \
    --nodes "1,2,3,4,5,6,7,8,9,10" --rounds 2 \
    --payload AABBCC \
    --gateway 0 --dongle-addr 16 \
    --boot-wait 20 --rssi-requests 3 \
    --ack-timeout 2.0 --interval 1.0 \
    --recollect-consecutive-failures 2 \
    --max-hops 4
```

---

## 5. D3QN 路由算法

### 5.1 模型架构

D3QN = Dueling Double DQN + MPNN (Message Passing Neural Network)

```
┌─────────────────────────────────────────────────┐
│                  D3QN 模型                       │
│                                                 │
│  ┌───────────┐    ┌───────────┐    ┌───────────┐│
│  │ MPNN 编码  │ →  │ Dueling   │ →  │ 动作选择  ││
│  │ 图特征提取 │    │ 网络      │    │ Q值估计   ││
│  └───────────┘    └───────────┘    └───────────┘│
│                                                 │
│  输入: 拓扑图 + 源节点 + 目标节点 + 需求         │
│  输出: K条候选路径 + Q值                         │
└─────────────────────────────────────────────────┘
```

### 5.2 决策流程

```python
for source, target in pairs:
    # 1. D3QN 决策
    decision = predictor.decide(topology, source, target, demand)
    
    # 2. 健康路径选择（避免退化路径）
    selected_path, action, reason, all_degraded = _select_healthy_candidate(
        decision, path_history, ...
    )
    
    # 3. 发送网关到源节点
    gateway_success, gateway_ms = _send_and_wait_ack(dst=source, path=gateway_route)
    
    # 4. 发送源节点到目标节点
    target_success, target_ms = _send_and_wait_ack(dst=target, path=full_path)
    
    # 5. 计算点到点延时
    point_to_point_ms = target_ms - gateway_ms
```

### 5.3 环路检测

```python
def _select_healthy_candidate(decision, path_history, ...):
    for action in q_order:
        path = decision.candidate_paths[action]
        
        # 环路检测
        if len(path) != len(set(path)):
            continue
        
        # 跳数限制
        if len(path) - 1 > max_hops:
            continue
        
        # 历史退化检测
        degraded, reason = _history_degraded(...)
        if not degraded:
            return path, action, None, False
```

### 5.4 测试命令

```bash
python -m pc_d3qn_cli bench \
    --port /dev/ttyUSB0 --baud 115200 \
    --nodes "1,2,3,4,5,6,7,8,9,10" --rounds 2 \
    --payload AABBCC \
    --gateway 0 --dongle-addr 16 \
    --boot-wait 20 --rssi-requests 3 \
    --ack-timeout 2.0 --interval 1.0 \
    --recollect-consecutive-failures 2 \
    --path-loss-degrade-threshold 0.20 \
    --path-p95-degrade-ms 1500 \
    --path-avg-degrade-ms 500 \
    --path-health-window 5
```

---

## 6. 拓扑重采集机制

### 6.1 触发条件

当连续丢包次数达到阈值时触发拓扑重采集：

```python
def _should_recollect(consecutive_failures, source, target, threshold):
    return threshold > 0 and consecutive_failures.get((source, target), 0) >= threshold
```

默认阈值: `--recollect-consecutive-failures 2`

### 6.2 重采集流程

```
连续 2 次丢包 → 发送 RSSI_REQ → 更新拓扑 → 重新计算路由 → 继续测试
```

### 6.3 拓扑稳定性保护

```python
def _recollect_topology(..., old_topology=None):
    new_topology = collect_topology(...)
    
    if old_topology is not None:
        old_edges = len(old_topology.edges)
        new_edges = len(new_topology.edges)
        
        # 如果新拓扑边数减少超过30%，保留旧拓扑
        if new_edges < old_edges * 0.7:
            logger.event("topology_recollect_rejected", reason="quality_degradation")
            topology = old_topology
        else:
            topology = new_topology
```

---

## 7. 测试报告生成

### 7.1 报告结构

```
第N次测试/
├── 测试结果汇报.md      # 主报告
├── 测试指标汇总.xlsx    # Excel 汇总
├── 拓扑图.svg          # 可视化拓扑
├── 拓扑图.txt          # 文本拓扑
├── 原始串口日志.log     # 串口原始数据
└── 原始JSON数据/
    ├── events.jsonl     # 事件日志
    ├── rounds.jsonl     # 轮次结果
    ├── summary.json     # 汇总统计
    ├── state.json       # 拓扑状态
    ├── routes.json      # 路由表
    └── hardware_test_record.json  # 硬件测试记录
```

### 7.2 关键指标

| 指标 | 计算方式 | 目标值 |
|------|---------|--------|
| 丢包率 | (total_sent - success) / total_sent | < 10% |
| 平均延时 | mean(success_latencies) | < 220ms |
| P95 延时 | percentile95(success_latencies) | < 700ms |
| 平均跳数 | mean(route_hops) | ≤ 3 |
| 单跳耗时 | avg_latency / avg_hops | < 80ms |

### 7.3 丢包率计算说明

```
总轮次 = 节点数 × 轮数 × (节点数-1)
路由不可达 = X (不计入总发送)
总发送 = 总轮次 - X
成功 = Y
ACK 超时 = 总发送 - Y
丢包率 = (总发送 - 成功) / 总发送
```

**注意**：路由不可达的节点对不计入丢包率计算。

---

## 8. 测试结果对比

### 8.1 历史测试结果

| 轮次 | 算法 | 发送 | 成功 | 丢包率 | ACK超时 | 路由不可达 | 说明 |
|------|------|------|------|--------|---------|-----------|------|
| 第6轮 | Dijkstra | 180 | 122 | 32.22% | 58 | 0 | 优化前 |
| 第6轮 | D3QN | 180 | 145 | 19.44% | 35 | 0 | 优化前 |
| 第9轮 | Dijkstra | 162 | 110 | 32.10% | 52 | 18 | RSSI采集修复 |
| 第11轮 | Dijkstra | 180 | 144 | 20.00% | 36 | 0 | RSSI采集修复 |
| 第12轮 | Dijkstra | 144 | 144 | **0.00%** | 0 | 36 | **重传机制** |
| 第13轮 | Dijkstra | 144 | 116 | 19.44% | 28 | 36 | 无重传 |
| 第14轮 | Dijkstra | 144 | 120 | 16.67% | 24 | 36 | interval=1.0s |

### 8.2 关键发现

1. **RSSI 采集等待时间**：从 0.35s/2.5s 增加到 1.0s/10.0s，确保收到所有节点响应
2. **avoid_nodes 惩罚**：导致选择过长路径，移除后丢包率降低
3. **环路检测**：消除 7 个环路，减少丢包
4. **跳数惩罚**：从 0.25 增加到 2.0，强制选择短路径
5. **重传机制**：将 19.44% 丢包率降到 0%，但增加延时

### 8.3 重传机制效果

| 配置 | 丢包率 | 平均延时 | 说明 |
|------|--------|---------|------|
| 无重传, interval=0.5s | 19.44% | 256.2ms | 基准 |
| 无重传, interval=1.0s | 16.67% | - | 拉长间隔 |
| 重传2次, interval=0.5s | 0.00% | 191.7ms | 最佳效果 |
| 重传1次, interval=1.0s | 待测 | - | 平衡方案 |

---

## 9. 已知问题与限制

### 9.1 硬件限制

| 问题 | 影响 | 状态 |
|------|------|------|
| 节点 06 不响应 | 18 个节点对路由不可达 | 硬件问题 |
| RSSI 信号弱 | 27.5% 链路 < -85dBm | 环境限制 |
| 设备启动慢 | 需要 60-120 秒校准 | 硬件特性 |

### 9.2 软件限制

| 问题 | 影响 | 状态 |
|------|------|------|
| 路由不可达计入丢包 | 丢包率不准确 | 设计如此 |
| 两跳发送增加延时 | 平均延时 191ms | 架构限制 |
| 拓扑采集耗时 | 增加测试时间 | 需要优化 |

---

## 10. 变更日志

| 版本 | 日期 | 变更内容 |
|------|------|---------|
| v1.0.0 | 2026-05-31 | 初始版本，包含 Dijkstra 和 D3QN 算法设计 |
| v1.1.0 | 2026-05-31 | 增加环路检测、跳数限制、弱 RSSI 惩罚、拓扑稳定性保护 |
| v1.2.0 | 2026-05-31 | 修复 RSSI 采集等待时间问题，从 0.35s/2.5s 增加到 1.0s/10.0s |
| v1.3.0 | 2026-06-01 | 重大优化：移除 avoid_nodes 惩罚、增加跳数惩罚、防环路径合并、ACK 自动重传 |
| v1.4.0 | 2026-06-01 | 更新完整上位机流程文档，增加详细流程说明和测试结果对比 |
| v1.5.0 | 2026-06-01 | 修复 dongle 地址解析问题，支持十进制和十六进制格式 |
| v1.6.0 | 2026-06-01 | 修复 SEND 命令格式：目标地址用十进制（1-10），路径用十六进制（01-0A） |
| v1.7.0 | 2026-06-01 | D3QN 优化：降低 min_rssi 到 -100、增加 max_hops 到 6、修复 SEND 命令格式 |
| v1.8.0 | 2026-06-02 | 测试结果：Dijkstra 10.42% 丢包率、D3QN 38.19% 丢包率（硬件环境变化） |

---

## 11. 已知问题与限制

### 11.1 硬件限制

| 问题 | 影响 | 状态 |
|------|------|------|
| 节点 06 不响应 | 18 个节点对路由不可达 | 硬件问题 |
| RSSI 信号弱 | 27.5% 链路 < -85dBm | 环境限制 |
| 设备启动慢 | 需要 60-120 秒校准 | 硬件特性 |

### 11.2 软件限制

| 问题 | 影响 | 状态 |
|------|------|------|
| 路由不可达计入丢包 | 丢包率不准确 | 设计如此 |
| 两跳发送增加延时 | 平均延时 191ms | 架构限制 |
| 拓扑采集耗时 | 增加测试时间 | 需要优化 |

### 11.3 dongle 地址解析

**问题**：`--dongle-addr 16` 被解析为 `0x16` = 22，而不是 0x10 = 16

**修复**：
- 支持十进制格式：`--dongle-addr 16` → 16 (0x10)
- 支持十六进制格式：`--dongle-addr 0x10` → 16 (0x10)

**正确用法**：
```bash
# 十进制格式
--dongle-addr 16

# 十六进制格式
--dongle-addr 0x10
```

### 11.4 网关中继

**设计**：网关不能作为中继节点

**原因**：
1. 网关是 USB 串口适配器，不是 mesh 节点
2. 网关入边被过滤，防止环路

**影响**：所有路径必须从网关开始，不能经过网关中继

### 11.5 跳数说明

| 跳数 | 含义 | 示例 |
|------|------|------|
| 0 | 路由不可达（路径为空） | route=[] |
| 1 | 直接通信 | route=[00, 03] |
| 2 | 一跳中继 | route=[00, 03, 01] |
| 3 | 两跳中继 | route=[00, 03, 01, 02] |

**注意**：跳数0不是直接通信，而是路径为空，表示找不到路径。
