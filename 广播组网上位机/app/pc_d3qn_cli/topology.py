from __future__ import annotations

import json
import statistics
import time
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Dict, List

from .protocol import RssiReport


def rssi_to_weight(rssi: int) -> int | None:
    if rssi >= -55:
        return 1
    if rssi >= -65:
        return 3
    if rssi >= -75:
        return 6
    if rssi >= -85:
        return 12
    return None


@dataclass
class Edge:
    src: int
    dst: int
    rssi: int
    weight: int
    updated_at: float
    direction: str = "src_to_neighbor"
    source: str = "real_rssi"
    samples: List[int] | None = None


class Topology:
    def __init__(self, stale_seconds: float | None = None, edge_direction: str = "src_to_neighbor", gateway: int | None = None, relay_excluded: set[int] | None = None):
        if edge_direction not in {"src_to_neighbor", "neighbor_to_src"}:
            raise ValueError(f"unsupported edge direction: {edge_direction}")
        self.stale_seconds = stale_seconds
        self.edge_direction = edge_direction
        self.gateway = gateway
        self.relay_excluded = relay_excluded or set()
        self.edges: Dict[str, Edge] = {}
        self.nodes: set[int] = set()
        self.raw_rssi_samples: list[dict] = []

    def update_from_rssi_report(self, report: RssiReport, now: float | None = None) -> list[Edge]:
        timestamp = time.time() if now is None else float(now)
        self.nodes.add(report.src_addr)
        updated: list[Edge] = []
        for neighbor in report.neighbors:
            self.nodes.add(neighbor.addr)
            self.raw_rssi_samples.append(
                {
                    "ts": timestamp,
                    "report_src": report.src_addr,
                    "neighbor": neighbor.addr,
                    "rssi": neighbor.rssi,
                    "source": "real_rssi",
                }
            )
            weight = rssi_to_weight(neighbor.rssi)
            if weight is None:
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
                weight=rssi_to_weight(median_rssi) or weight,
                updated_at=timestamp,
                direction=self.edge_direction,
                source="real_rssi",
                samples=samples,
            )
            self.edges[key] = edge
            updated.append(edge)
        return updated

    def graph(self, now: float | None = None) -> dict[int, dict[int, float]]:
        timestamp = time.time() if now is None else float(now)
        graph: dict[int, dict[int, float]] = {}
        for edge in self.edges.values():
            if self.stale_seconds is not None and timestamp - edge.updated_at > self.stale_seconds:
                continue
            if self.gateway is not None and edge.dst == self.gateway:
                continue
            if edge.src in self.relay_excluded or edge.dst in self.relay_excluded:
                continue
            graph.setdefault(edge.src, {})[edge.dst] = float(edge.weight)
        return graph

    def to_dict(self) -> dict:
        return {
            "stale_seconds": self.stale_seconds,
            "edge_direction": self.edge_direction,
            "gateway": self.gateway,
            "relay_excluded": sorted(self.relay_excluded),
            "nodes": sorted(self.nodes),
            "edges": [asdict(edge) for edge in sorted(self.edges.values(), key=lambda item: (item.src, item.dst))],
            "raw_rssi_samples": self.raw_rssi_samples,
        }

    @classmethod
    def from_dict(cls, data: dict) -> "Topology":
        topology = cls(stale_seconds=data.get("stale_seconds"), edge_direction=data.get("edge_direction", "src_to_neighbor"), gateway=data.get("gateway"), relay_excluded=set(data.get("relay_excluded", [])))
        topology.nodes = {int(node) for node in data.get("nodes", [])}
        topology.raw_rssi_samples = list(data.get("raw_rssi_samples", []))
        for edge_data in data.get("edges", []):
            edge = Edge(
                src=int(edge_data["src"]),
                dst=int(edge_data["dst"]),
                rssi=int(edge_data["rssi"]),
                weight=int(edge_data["weight"]),
                updated_at=float(edge_data["updated_at"]),
                direction=edge_data.get("direction", data.get("edge_direction", "src_to_neighbor")),
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
    return Topology.from_dict(json.loads(state_path.read_text(encoding="utf-8")))


def save_topology(path: str | Path, topology: Topology) -> None:
    state_path = Path(path)
    state_path.parent.mkdir(parents=True, exist_ok=True)
    state_path.write_text(json.dumps(topology.to_dict(), ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def edge_key(src: int, dst: int) -> str:
    return f"{src:02X}:{dst:02X}"

