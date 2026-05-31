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
    return paths


def edge_betweenness(topology: Topology, k_paths: int) -> dict[str, float]:
    graph = topology.graph()
    counts = {edge_key(edge.src, edge.dst): 0 for edge in topology.edges.values()}
    total = 0
    nodes = sorted(topology.nodes)
    for src in nodes:
        for dst in nodes:
            if src == dst:
                continue
            for path in k_candidate_paths(graph, src, dst, k_paths):
                total += 1
                for start, end in zip(path[:-1], path[1:]):
                    key = edge_key(start, end)
                    counts[key] = counts.get(key, 0) + 1
    denominator = max(1, total)
    return {key: count / denominator for key, count in counts.items()}


def build_d3qn_state(topology: Topology, summary: dict | None = None, defaults: D3QNDefaults = DEFAULTS) -> dict:
    graph = _planning_graph_with_reciprocal_edges(topology)
    betweenness = _edge_betweenness_from_graph(graph, sorted(topology.nodes), defaults.k_paths)
    edge_records = _planning_edge_records(topology)
    ordered_edges = sorted(edge_records)
    edge_index = {edge_key(src, dst): index for index, (src, dst) in enumerate(ordered_edges)}

    edge_features = []
    for src, dst in ordered_edges:
        edge = edge_records[(src, dst)]
        capacity = rssi_to_capacity(edge["rssi"], defaults)
        edge_features.append(
            {
                "src": src,
                "dst": dst,
                "rssi": real_field(edge["rssi"], edge["source"], "dBm"),
                "rssi_weight": real_field(edge["weight"], "derived"),
                "capacity": {"value": capacity, "source": "derived", "unit": "capacity_unit", "derived_from": "rssi_weight"},
                "bw_allocated": {"value": defaults.bw_allocated, "source": "default", "unit": "capacity_unit"},
                "remaining_capacity": {"value": capacity - defaults.bw_allocated, "source": "derived", "unit": "capacity_unit"},
                "packet_loss": {"value": defaults.packet_loss, "source": "default", "unit": "ratio"},
                "delay": {"value": defaults.delay, "source": "default", "unit": "seconds"},
                "queueing_delay": {"value": defaults.queueing_delay, "source": "default", "unit": "seconds"},
                "betweenness": {"value": betweenness.get(edge_key(src, dst), 0.0), "source": "derived"},
            }
        )

    candidate_paths = {}
    for src in sorted(topology.nodes):
        for dst in sorted(topology.nodes):
            if src == dst:
                continue
            candidate_paths[f"{src:02X}:{dst:02X}"] = k_candidate_paths(graph, src, dst, defaults.k_paths)

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
    records: dict[tuple[int, int], dict] = {}
    for edge in topology.edges.values():
        records[(edge.src, edge.dst)] = {
            "src": edge.src,
            "dst": edge.dst,
            "rssi": edge.rssi,
            "weight": edge.weight,
            "source": "real_rssi",
        }
    for edge in topology.edges.values():
        reverse = (edge.dst, edge.src)
        if reverse not in records:
            records[reverse] = {
                "src": edge.dst,
                "dst": edge.src,
                "rssi": edge.rssi,
                "weight": edge.weight + 8,
                "source": "derived_reciprocal_rssi",
            }
    return records


def _planning_graph_with_reciprocal_edges(topology: Topology) -> dict[int, dict[int, float]]:
    graph: dict[int, dict[int, float]] = {}
    for (src, dst), edge in _planning_edge_records(topology).items():
        graph.setdefault(src, {})[dst] = float(edge["weight"])
    return graph


def _edge_betweenness_from_graph(graph: dict[int, dict[int, float]], nodes: list[int], k_paths: int) -> dict[str, float]:
    counts = {edge_key(src, dst): 0 for src, neighbors in graph.items() for dst in neighbors}
    total = 0
    for src in nodes:
        for dst in nodes:
            if src == dst:
                continue
            for path in k_candidate_paths(graph, src, dst, k_paths):
                total += 1
                for start, end in zip(path[:-1], path[1:]):
                    key = edge_key(start, end)
                    counts[key] = counts.get(key, 0) + 1
    denominator = max(1, total)
    return {key: count / denominator for key, count in counts.items()}


def write_d3qn_state(path: str | Path, topology: Topology, summary: dict | None = None, defaults: D3QNDefaults = DEFAULTS) -> dict:
    state = build_d3qn_state(topology, summary, defaults)
    output_path = Path(path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(state, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return state
