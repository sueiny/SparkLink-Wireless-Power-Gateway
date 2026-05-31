from __future__ import annotations

import json
import statistics
import time
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Dict, Iterable, List

from .protocol import RssiReport
from .routing import BASELINE_ROUTE_MODE, RELIABLE_ROUTE_MODE, ROUTE_MODES, Route, build_route_table, dijkstra, rssi_to_reliable_weight, rssi_to_weight


@dataclass
class Edge:
    src: int
    dst: int
    rssi: int
    weight: int
    updated_at: float
    direction: str = "neighbor_to_src"
    source: str = "real_rssi"
    samples: List[int] | None = None


class Topology:
    def __init__(self, stale_seconds: float | None = 30.0, edge_direction: str = "neighbor_to_src", gateway: int | None = None, relay_excluded: set[int] | None = None, min_rssi: int = -85):
        if edge_direction not in {"neighbor_to_src", "src_to_neighbor"}:
            raise ValueError(f"unsupported edge direction: {edge_direction}")
        self.stale_seconds = stale_seconds
        self.edge_direction = edge_direction
        self.gateway = gateway
        self.relay_excluded = relay_excluded or set()
        self.min_rssi = min_rssi
        self.edges: Dict[str, Edge] = {}
        self.nodes: set[int] = set()

    def update_from_rssi_report(self, report: RssiReport, now: float | None = None) -> List[Edge]:
        timestamp = time.time() if now is None else float(now)
        self.nodes.add(report.src_addr)
        updated = []
        for neighbor in report.neighbors:
            self.nodes.add(neighbor.addr)
            baseline_weight = rssi_to_weight(neighbor.rssi)
            if baseline_weight is None:
                continue
            edge_src, edge_dst = self._edge_endpoints(report.src_addr, neighbor.addr)
            key = self._edge_key(edge_src, edge_dst)
            samples = list(self.edges[key].samples or [self.edges[key].rssi]) if key in self.edges else []
            samples.append(neighbor.rssi)
            median_rssi = int(round(statistics.median(samples)))
            edge = Edge(
                src=edge_src,
                dst=edge_dst,
                rssi=median_rssi,
                weight=baseline_weight,
                updated_at=timestamp,
                direction=self.edge_direction,
                source="real_rssi",
                samples=samples,
            )
            self.edges[key] = edge
            updated.append(edge)
        return updated

    def graph(self, now: float | None = None, route_mode: str = BASELINE_ROUTE_MODE, min_rssi: int = -85) -> Dict[int, Dict[int, float]]:
        if route_mode not in ROUTE_MODES:
            raise ValueError(f"unsupported route mode: {route_mode}")
        timestamp = time.time() if now is None else float(now)
        graph: Dict[int, Dict[int, float]] = {}
        for edge in self.edges.values():
            if self.stale_seconds is not None and timestamp - edge.updated_at > self.stale_seconds:
                continue
            if self.gateway is not None and edge.dst == self.gateway:
                continue
            if edge.src in self.relay_excluded or edge.dst in self.relay_excluded:
                continue
            if edge.rssi < min_rssi:
                continue
            if route_mode == RELIABLE_ROUTE_MODE:
                weight = rssi_to_reliable_weight(edge.rssi)
                if weight is None:
                    continue
                weight += 0.5
            else:
                weight = float(edge.weight)
            graph.setdefault(edge.src, {})[edge.dst] = weight
        return graph

    def route(self, src: int, dst: int, route_mode: str = BASELINE_ROUTE_MODE, fallback: bool = True) -> Route:
        route = dijkstra(self.graph(route_mode=route_mode, min_rssi=self.min_rssi), src, dst)
        if route.status != "valid" and route_mode != BASELINE_ROUTE_MODE and fallback:
            return dijkstra(self.graph(route_mode=BASELINE_ROUTE_MODE, min_rssi=self.min_rssi), src, dst)
        return route

    def routes(self, route_mode: str = BASELINE_ROUTE_MODE) -> Dict[str, Route]:
        return build_route_table(self.graph(route_mode=route_mode, min_rssi=self.min_rssi), self.nodes)

    def to_dict(self) -> dict:
        return {
            "stale_seconds": self.stale_seconds,
            "edge_direction": self.edge_direction,
            "gateway": self.gateway,
            "relay_excluded": sorted(self.relay_excluded),
            "min_rssi": self.min_rssi,
            "nodes": sorted(self.nodes),
            "edges": [asdict(edge) for edge in sorted(self.edges.values(), key=lambda item: (item.src, item.dst))],
        }

    @classmethod
    def from_dict(cls, data: dict) -> "Topology":
        topology = cls(stale_seconds=data.get("stale_seconds", 30.0), edge_direction=data.get("edge_direction", "neighbor_to_src"), gateway=data.get("gateway"), relay_excluded=set(data.get("relay_excluded", [])), min_rssi=data.get("min_rssi", -85))
        topology.nodes = {int(node) for node in data.get("nodes", [])}
        for edge_data in data.get("edges", []):
            edge = Edge(
                src=int(edge_data["src"]),
                dst=int(edge_data["dst"]),
                rssi=int(edge_data["rssi"]),
                weight=int(edge_data["weight"]),
                updated_at=float(edge_data["updated_at"]),
                direction=edge_data.get("direction", data.get("edge_direction", "neighbor_to_src")),
                source=edge_data.get("source", "real_rssi"),
                samples=[int(value) for value in edge_data.get("samples", [])] or [int(edge_data["rssi"])],
            )
            topology.edges[topology._edge_key(edge.src, edge.dst)] = edge
            topology.nodes.add(edge.src)
            topology.nodes.add(edge.dst)
        return topology

    @staticmethod
    def _edge_key(src: int, dst: int) -> str:
        return f"{src:02X}:{dst:02X}"

    def _edge_endpoints(self, report_src: int, neighbor: int) -> tuple[int, int]:
        if self.edge_direction == "src_to_neighbor":
            return report_src, neighbor
        return neighbor, report_src


def load_topology(path: str | Path) -> Topology:
    state_path = Path(path)
    if not state_path.exists():
        return Topology()
    with state_path.open("r", encoding="utf-8") as file_obj:
        return Topology.from_dict(json.load(file_obj))


def save_topology(path: str | Path, topology: Topology) -> None:
    state_path = Path(path)
    state_path.parent.mkdir(parents=True, exist_ok=True)
    with state_path.open("w", encoding="utf-8") as file_obj:
        json.dump(topology.to_dict(), file_obj, ensure_ascii=False, indent=2, sort_keys=True)
        file_obj.write("\n")


def routes_to_dict(routes: Dict[str, Route]) -> Dict[str, dict]:
    return {
        key: {
            "src": route.src,
            "dst": route.dst,
            "path": route.path,
            "cost": route.cost,
            "status": route.status,
        }
        for key, route in sorted(routes.items())
    }
