from __future__ import annotations

import heapq
from dataclasses import dataclass
from typing import Dict, Iterable, List, Mapping, Optional, Tuple


BASELINE_ROUTE_MODE = "baseline_dijkstra"
RELIABLE_ROUTE_MODE = "reliable_dijkstra_v1"
SAMPLE_ROUTE_MODE = "sample_dijkstra"
ROUTE_MODES = {BASELINE_ROUTE_MODE, RELIABLE_ROUTE_MODE, SAMPLE_ROUTE_MODE}

# sample/Dijkstra 仿真对齐：边权 = 1 / remaining_capacity
# 仿真里 capacity 由 RSSI 推导（强信号→大容量→小权重），allocated 初始=0
# 硬件上同样用 RSSI 推导容量，allocated=0，完全复现仿真行为
_D3QN_CAPACITY = 200.0


def _d3qn_rssi_weight(rssi: int) -> float:
    """RSSI → 权重分桶，5dBm 步长细化版（Dijkstra 路由专用）。
    原 10dBm 6档 → 5dBm 11档，同档内质量差异可被区分，减少同桶误判。"""
    if rssi >= -55:  return 1.0
    if rssi >= -60:  return 1.5
    if rssi >= -65:  return 3.0
    if rssi >= -70:  return 4.5
    if rssi >= -75:  return 6.0
    if rssi >= -80:  return 9.0
    if rssi >= -85:  return 12.0
    if rssi >= -90:  return 18.0
    if rssi >= -95:  return 24.0
    if rssi >= -100: return 36.0
    if rssi >= -105: return 48.0
    return 96.0


def sample_capacity_weight(rssi: int) -> float:
    """仿真 Dijkstra 边权：1/remaining_capacity，capacity=200/rssi_weight，allocated=0。
    强信号 → 大容量 → 小权重（优先），弱信号 → 大权重（绕行走中继）。"""
    capacity = _D3QN_CAPACITY / _d3qn_rssi_weight(rssi)
    return 1.0 / max(capacity, 1e-6)


def sample_pure_weight() -> float:
    """保留兼容：恒定权重（纯跳数），不再用于 sample_dijkstra 模式。"""
    return 1.0 / _D3QN_CAPACITY


def rssi_to_weight(rssi: int) -> Optional[int]:
    if rssi >= -55:
        return 4
    if rssi >= -65:
        return 5
    if rssi >= -75:
        return 6
    if rssi >= -85:
        return 12
    return 15


def rssi_to_reliable_weight(rssi: int) -> Optional[float]:
    if rssi >= -55:
        return 1.0
    if rssi >= -65:
        return 3.0
    if rssi >= -75:
        return 6.0
    if rssi >= -80:
        return 16.0
    if rssi >= -85:
        return 32.0
    return 40.0


@dataclass(frozen=True)
class Route:
    src: int
    dst: int
    path: List[int]
    cost: float
    status: str


def _calculate_node_congestion_penalty(graph: Mapping[int, Mapping[int, int]], gateway: int = 0, penalty_factor: float = 5.0) -> Dict[int, float]:
    """计算节点拥塞惩罚权重
    
    基于节点度数计算拥塞惩罚：
    - 度数越高的节点，越可能是热门中继节点
    - 热门中继节点容易拥塞，应该赋予更高的惩罚
    
    惩罚公式：
    - gateway节点：惩罚0（必须经过）
    - 其他节点：惩罚 = 度数 * 惩罚系数
    - 度数越高，惩罚越大
    """
    # 计算每个节点的度数
    node_degree: Dict[int, int] = {}
    for node in graph:
        if node not in node_degree:
            node_degree[node] = 0
        for neighbor in graph[node]:
            node_degree[node] += 1
            if neighbor not in node_degree:
                node_degree[neighbor] = 0
            node_degree[neighbor] += 1
    
    # 计算拥塞惩罚
    congestion_penalty: Dict[int, float] = {}
    for node, degree in node_degree.items():
        if node == gateway:
            congestion_penalty[node] = 0.0  # gateway节点不惩罚
        else:
            # 度数越高，惩罚越大
            congestion_penalty[node] = degree * penalty_factor
    
    return congestion_penalty


def update_congestion_penalty(congestion_penalty: Dict[int, float], high_latency_nodes: list[int], penalty_increase: float = 50.0) -> Dict[int, float]:
    """更新拥塞惩罚权重
    
    基于高延迟路径中经过的节点，增加这些节点的拥塞惩罚
    
    参数：
    - congestion_penalty: 当前拥塞惩罚
    - high_latency_nodes: 高延迟路径中经过的节点列表
    - penalty_increase: 惩罚增加量
    """
    for node in high_latency_nodes:
        if node in congestion_penalty:
            congestion_penalty[node] += penalty_increase
    return congestion_penalty


def dijkstra(graph: Mapping[int, Mapping[int, int]], src: int, dst: int, max_hops: int = 4, hop_penalty: float = 0.0, congestion_penalty: Optional[Dict[int, float]] = None) -> Route:
    if src == dst:
        return Route(src=src, dst=dst, path=[src], cost=0.0, status="valid")

    distances: Dict[int, float] = {src: 0.0}
    predecessors: Dict[int, int] = {}
    hop_counts: Dict[int, int] = {src: 0}
    queue: List[Tuple[float, int]] = [(0.0, src)]
    visited = set()

    while queue:
        current_cost, current = heapq.heappop(queue)
        if current in visited:
            continue
        visited.add(current)
        if current == dst:
            break

        current_hops = hop_counts.get(current, 0)
        if current_hops >= max_hops:
            continue

        for neighbor, weight in graph.get(current, {}).items():
            node_penalty = congestion_penalty.get(neighbor, 0.0) if congestion_penalty else 0.0
            new_cost = current_cost + float(weight) + hop_penalty + node_penalty
            new_hops = current_hops + 1
            if new_hops > max_hops:
                continue
            if new_cost < distances.get(neighbor, float("inf")):
                distances[neighbor] = new_cost
                predecessors[neighbor] = current
                hop_counts[neighbor] = new_hops
                heapq.heappush(queue, (new_cost, neighbor))

    if dst not in distances:
        return Route(src=src, dst=dst, path=[], cost=float("inf"), status="unreachable")

    path = [dst]
    while path[-1] != src:
        path.append(predecessors[path[-1]])
    path.reverse()
    return Route(src=src, dst=dst, path=path, cost=distances[dst], status="valid")


def build_route_table(graph: Mapping[int, Mapping[int, int]], nodes: Iterable[int], max_hops: int = 4, gateway: int = 0, hop_penalty: float = 0.0, congestion_penalty: Optional[Dict[int, float]] = None) -> Dict[str, Route]:
    node_list = sorted(set(nodes))
    routes: Dict[str, Route] = {}
    for src in node_list:
        for dst in node_list:
            if src == dst:
                continue
            route = dijkstra(graph, src, dst, max_hops=max_hops, hop_penalty=hop_penalty, congestion_penalty=congestion_penalty)
            if route.status == "valid":
                routes[f"{src:02X}:{dst:02X}"] = route
    return routes
