from __future__ import annotations

import heapq
from dataclasses import dataclass
from typing import Dict, Iterable, List, Mapping, Optional, Tuple


BASELINE_ROUTE_MODE = "baseline_dijkstra"
RELIABLE_ROUTE_MODE = "reliable_dijkstra_v1"
ROUTE_MODES = {BASELINE_ROUTE_MODE, RELIABLE_ROUTE_MODE}


def rssi_to_weight(rssi: int) -> Optional[int]:
    if rssi >= -55:
        return 1
    if rssi >= -65:
        return 3
    if rssi >= -75:
        return 6
    if rssi >= -85:
        return 12
    return None


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
    return None


@dataclass(frozen=True)
class Route:
    src: int
    dst: int
    path: List[int]
    cost: float
    status: str


def dijkstra(graph: Mapping[int, Mapping[int, int]], src: int, dst: int) -> Route:
    if src == dst:
        return Route(src=src, dst=dst, path=[src], cost=0.0, status="valid")

    distances: Dict[int, float] = {src: 0.0}
    predecessors: Dict[int, int] = {}
    queue: List[Tuple[float, int]] = [(0.0, src)]
    visited = set()

    while queue:
        current_cost, current = heapq.heappop(queue)
        if current in visited:
            continue
        visited.add(current)
        if current == dst:
            break

        for neighbor, weight in graph.get(current, {}).items():
            new_cost = current_cost + float(weight)
            if new_cost < distances.get(neighbor, float("inf")):
                distances[neighbor] = new_cost
                predecessors[neighbor] = current
                heapq.heappush(queue, (new_cost, neighbor))

    if dst not in distances:
        return Route(src=src, dst=dst, path=[], cost=float("inf"), status="unreachable")

    path = [dst]
    while path[-1] != src:
        path.append(predecessors[path[-1]])
    path.reverse()
    return Route(src=src, dst=dst, path=path, cost=distances[dst], status="valid")


def build_route_table(graph: Mapping[int, Mapping[int, int]], nodes: Iterable[int]) -> Dict[str, Route]:
    node_list = sorted(set(nodes))
    routes: Dict[str, Route] = {}
    for src in node_list:
        for dst in node_list:
            if src == dst:
                continue
            route = dijkstra(graph, src, dst)
            if route.status == "valid":
                routes[f"{src:02X}:{dst:02X}"] = route
    return routes
