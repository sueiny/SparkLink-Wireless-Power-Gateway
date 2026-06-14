from __future__ import annotations

import heapq
import json
from pathlib import Path

from .defaults import DEFAULTS, D3QNDefaults, default_simulation_params, real_field
from .topology import Topology, edge_key


def rssi_to_capacity(rssi: int, defaults: D3QNDefaults = DEFAULTS) -> float:
    from .topology import rssi_to_weight

    weight = rssi_to_weight(rssi)
    if weight is None:
        return 0.0
    return defaults.capacity / float(weight)


def k_candidate_paths(graph: dict[int, dict[int, float]], src: int, dst: int, k: int) -> list[list[int]]:
    """与 sample 环境 _weighted_k_shortest_paths 一致：纯RSSI权重，无hop_penalty，无向图，按(跳数,字典序)排序"""
    if src == dst:
        return [[src]]
    queue: list[tuple[float, tuple[int, ...]]] = [(0.0, (src,))]
    paths: list[list[int]] = []
    seen = set()
    while queue and len(paths) < k:
        cost, path_tuple = heapq.heappop(queue)
        if path_tuple in seen:
            continue
        seen.add(path_tuple)
        current = path_tuple[-1]
        if current == dst:
            paths.append(list(path_tuple))
            continue
        for neighbor, weight in sorted(graph.get(current, {}).items()):
            if neighbor in path_tuple:
                continue
            heapq.heappush(queue, (cost + float(weight), path_tuple + (neighbor,)))
    # 与 sample 一致：按跳数→字典序排序
    return sorted(paths, key=lambda item: (len(item), item))[:k]


def edge_betweenness(topology: Topology, k_paths: int) -> dict[str, float]:
    graph = topology.graph()
    def _undirected_key(a: int, b: int) -> str:
        return edge_key(min(a, b), max(a, b))
    counts: dict[str, int] = {}
    for edge in topology.edges.values():
        counts[_undirected_key(edge.src, edge.dst)] = 0
    total = 0
    nodes = sorted(topology.nodes)
    for src in nodes:
        for dst in nodes:
            if src == dst:
                continue
            for path in k_candidate_paths(graph, src, dst, k_paths):
                total += 1
                for start, end in zip(path[:-1], path[1:]):
                    key = _undirected_key(start, end)
                    counts[key] = counts.get(key, 0) + 1
    denominator = max(1, total)
    return {key: count / denominator for key, count in counts.items()}


def build_d3qn_state(topology: Topology, summary: dict | None = None, defaults: D3QNDefaults = DEFAULTS, src: int | None = None, dst: int | None = None) -> dict:
    graph = _planning_graph_undirected(topology)
    betweenness = _edge_betweenness_from_graph(graph, sorted(topology.nodes), defaults.k_paths)
    edge_records = _planning_edge_records(topology)
    ordered_edges = sorted(edge_records)
    # 与 sample 环境一致：双向注册 edge_index，无向图路径可能走任意方向
    edge_index: dict[str, int] = {}
    for index, (s, d) in enumerate(ordered_edges):
        edge_index[edge_key(s, d)] = index
        edge_index[edge_key(d, s)] = index

    edge_features = []
    for s, d in ordered_edges:
        edge = edge_records[(s, d)]
        capacity = rssi_to_capacity(edge["rssi"], defaults)
        edge_features.append(
            {
                "src": s,
                "dst": d,
                "rssi": real_field(edge["rssi"], edge["source"], "dBm"),
                "rssi_weight": real_field(edge["weight"], "derived"),
                "capacity": {"value": capacity, "source": "derived", "unit": "capacity_unit", "derived_from": "rssi_weight"},
                "bw_allocated": {"value": defaults.bw_allocated, "source": "default", "unit": "capacity_unit"},
                "remaining_capacity": {"value": capacity - defaults.bw_allocated, "source": "derived", "unit": "capacity_unit"},
                "packet_loss": {"value": defaults.packet_loss, "source": "default", "unit": "ratio"},
                "delay": {"value": defaults.delay, "source": "default", "unit": "seconds"},
                "queueing_delay": {"value": defaults.queueing_delay, "source": "default", "unit": "seconds"},
                "betweenness": {"value": betweenness.get(edge_key(s, d), 0.0), "source": "derived"},
            }
        )

    candidate_paths = {}
    if src is not None and dst is not None:
        # 只计算指定src-dst对的候选路径
        key = f"{src:02X}:{dst:02X}"
        candidate_paths[key] = k_candidate_paths(graph, src, dst, defaults.k_paths)
    else:
        # 计算所有节点对的候选路径（用于拓扑收集等场景）
        for s in sorted(topology.nodes):
            for d in sorted(topology.nodes):
                if s == d:
                    continue
                candidate_paths[f"{s:02X}:{d:02X}"] = k_candidate_paths(graph, s, d, defaults.k_paths)

    return {
        "schema_version": 1,
        "algorithm": "D3QN_MPNN",
        "description": "D3QN runtime state. RSSI is real; unmeasured simulation fields are defaults.",
        "nodes": sorted(topology.nodes),
        "ordered_edges": [list(edge) for edge in ordered_edges],
        "edgesDict": {key: index for key, index in sorted(edge_index.items())},
        "edge_features": edge_features,
        "candidate_paths": candidate_paths,
        "demands": {"value": list(defaults.demands), "source": "default"},
        "default_params": default_simulation_params(defaults),
        "benchmark_summary": summary or {},
    }


def _planning_edge_records(topology: Topology) -> dict[tuple[int, int], dict]:
    """与 sample 环境一致：小节点在前，不生成反向边"""
    records: dict[tuple[int, int], dict] = {}
    for edge in topology.edges.values():
        # 与 sample environment1.py:472 一致：tuple(sorted(edge))
        a, b = sorted((edge.src, edge.dst))
        key = (a, b)
        if key not in records:
            records[key] = {
                "src": a,
                "dst": b,
                "rssi": edge.rssi,
                "weight": edge.weight,
                "source": "real_rssi",
            }
    return records


def _planning_graph_undirected(topology: Topology) -> dict[int, dict[int, float]]:
    """直接复用 topology.graph()：双向验证 + 网关例外 + relay_excluded 过滤，与 Dijkstra SAMPLE_ROUTE_MODE 对齐。"""
    return topology.graph()


def _edge_betweenness_from_graph(graph: dict[int, dict[int, float]], nodes: list[int], k_paths: int) -> dict[str, float]:
    """与 sample 环境一致：原始 betweenness 计数后做 z-score 标准化，无向边用统一 key"""
    # 无向边只用小节点在前的 key
    def _undirected_key(a: int, b: int) -> str:
        return edge_key(min(a, b), max(a, b))
    counts: dict[str, int] = {}
    for src, neighbors in graph.items():
        for dst in neighbors:
            key = _undirected_key(src, dst)
            counts[key] = 0
    total = 0
    for src in nodes:
        for dst in nodes:
            if src == dst:
                continue
            for path in k_candidate_paths(graph, src, dst, k_paths):
                total += 1
                for start, end in zip(path[:-1], path[1:]):
                    key = _undirected_key(start, end)
                    counts[key] = counts.get(key, 0) + 1
    denominator = max(1, total)
    raw = {key: count / denominator for key, count in counts.items()}
    # z-score 标准化（与 sample environment1.py:485 一致）
    values = list(raw.values())
    if values:
        mu = sum(values) / len(values)
        std = max((sum((v - mu) ** 2 for v in values) / len(values)) ** 0.5, 1e-8)
    else:
        mu, std = 0.0, 1.0
    return {key: (v - mu) / std for key, v in raw.items()}


def write_d3qn_state(path: str | Path, topology: Topology, summary: dict | None = None, defaults: D3QNDefaults = DEFAULTS) -> dict:
    state = build_d3qn_state(topology, summary, defaults)
    output_path = Path(path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(state, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return state
