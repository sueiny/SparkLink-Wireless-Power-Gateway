from __future__ import annotations

import html
import math
from dataclasses import asdict
from pathlib import Path

from .protocol import format_addr
from .topology import Topology


def rssi_color(rssi: int) -> str:
    if rssi >= -55:
        return "#238b45"
    if rssi >= -65:
        return "#2b6cb0"
    if rssi >= -75:
        return "#b7791f"
    if rssi >= -85:
        return "#c53030"
    return "#7a7f87"


def _path_edges(summary: dict | None) -> set[tuple[int, int]]:
    highlighted: set[tuple[int, int]] = set()
    if not summary:
        return highlighted
    for item in summary.get("targets", {}).values():
        route = item.get("last_route", [])
        for src, dst in zip(route[:-1], route[1:]):
            highlighted.add((int(src), int(dst)))
    return highlighted


def _path_edge_order(summary: dict | None) -> dict[tuple[int, int], int]:
    order: dict[tuple[int, int], int] = {}
    if not summary:
        return order
    index = 0
    for item in summary.get("targets", {}).values():
        route = item.get("last_route", [])
        for src, dst in zip(route[:-1], route[1:]):
            key = (int(src), int(dst))
            if key not in order:
                order[key] = index
                index += 1
    return order


def _route_depths(nodes: list[int], summary: dict | None, gateway: int) -> dict[int, int]:
    depths = {node: 1 for node in nodes}
    depths[gateway] = 0
    if summary:
        for item in summary.get("targets", {}).values():
            for depth, node in enumerate(item.get("last_route", [])):
                node = int(node)
                depths[node] = min(depths.get(node, depth), depth)
    return depths


def _layout(nodes: list[int], width: int, height: int, summary: dict | None, gateway: int) -> dict[int, tuple[float, float]]:
    depths = _route_depths(nodes, summary, gateway)
    positions: dict[int, tuple[float, float]] = {gateway: (width * 0.22, height * 0.52)}
    center_x = width * 0.58
    center_y = height * 0.52
    available = [node for node in nodes if node != gateway]
    if not available:
        return positions

    max_depth = max(depths.get(node, 1) for node in available)
    rings: dict[int, list[int]] = {}
    for node in available:
        rings.setdefault(depths.get(node, 1), []).append(node)

    for depth in sorted(rings):
        group = sorted(rings[depth])
        radius_x = 255 + (depth - 1) * 190
        radius_y = 165 + (depth - 1) * 82
        count = len(group)
        if count == 1:
            angles = [0.0]
        else:
            span = math.radians(255)
            start = -span / 2
            angles = [start + span * index / (count - 1) for index in range(count)]
        for node, angle in zip(group, angles):
            x = center_x + radius_x * math.cos(angle)
            y = center_y + radius_y * math.sin(angle)
            x = min(max(x, 95.0), width - 95.0)
            y = min(max(y, 150.0), height - 88.0)
            positions[node] = (x, y)

    _spread_overlaps(positions, width, height, gateway)
    return positions


def _spread_overlaps(positions: dict[int, tuple[float, float]], width: int, height: int, gateway: int) -> None:
    min_dist = 118.0
    nodes = [node for node in positions if node != gateway]
    for _ in range(80):
        moved = False
        for i, a in enumerate(nodes):
            ax, ay = positions[a]
            for b in nodes[i + 1 :]:
                bx, by = positions[b]
                dx = bx - ax
                dy = by - ay
                dist = math.hypot(dx, dy) or 0.01
                if dist >= min_dist:
                    continue
                push = (min_dist - dist) / 2
                ux = dx / dist
                uy = dy / dist
                ax -= ux * push
                ay -= uy * push
                bx += ux * push
                by += uy * push
                positions[a] = (min(max(ax, 92.0), width - 92.0), min(max(ay, 150.0), height - 92.0))
                positions[b] = (min(max(bx, 92.0), width - 92.0), min(max(by, 150.0), height - 92.0))
                moved = True
        if not moved:
            break


def _curve_path(x1: float, y1: float, x2: float, y2: float, offset: float) -> str:
    mx = (x1 + x2) / 2
    return f"M{x1:.1f},{y1:.1f} C{mx:.1f},{(y1 + offset):.1f} {mx:.1f},{(y2 - offset):.1f} {x2:.1f},{y2:.1f}"


def build_topology_svg(
    topology: Topology,
    summary: dict | None = None,
    width: int = 1680,
    height: int = 1080,
    gateway: int = 0,
) -> str:
    nodes = sorted(topology.nodes | {gateway})
    positions = _layout(nodes, width, height, summary, gateway=gateway)
    highlighted = _path_edges(summary)
    label_order = _path_edge_order(summary)
    path_nodes = {gateway}
    for src, dst in highlighted:
        path_nodes.add(src)
        path_nodes.add(dst)
    edge_items = sorted(topology.edges.values(), key=lambda item: (item.src, item.dst))

    svg: list[str] = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}" role="img" aria-label="Dijkstra layered RSSI topology">',
        '<rect width="100%" height="100%" fill="#ffffff"/>',
        '<text x="42" y="46" font-size="24" font-family="Arial, sans-serif" fill="#111827" font-weight="700">Dijkstra RSSI Layered Topology</text>',
        '<text x="42" y="74" font-size="13" font-family="Arial, sans-serif" fill="#4b5563">图中不显示箭头；深色线为选中的有向最短路径链路，浅灰线为未使用的 RSSI 观测链路。</text>',
    ]

    legend = [
        (">= -55 dBm", "#238b45"),
        ("-56~-65", "#2b6cb0"),
        ("-66~-75", "#b7791f"),
        ("-76~-85", "#c53030"),
        ("route", "#111827"),
    ]
    for index, (label, color) in enumerate(legend):
        x = 42 + index * 132
        svg.append(f'<line x1="{x}" y1="108" x2="{x + 34}" y2="108" stroke="{color}" stroke-width="4" stroke-linecap="round"/>')
        svg.append(f'<text x="{x + 42}" y="113" font-size="12" font-family="Arial, sans-serif" fill="#374151">{html.escape(label)}</text>')

    svg.append('<g id="observed_edges">')
    for edge in edge_items:
        if (edge.src, edge.dst) in highlighted:
            continue
        if edge.src not in positions or edge.dst not in positions:
            continue
        x1, y1 = positions[edge.src]
        x2, y2 = positions[edge.dst]
        offset = 22.0 if edge.src < edge.dst else -22.0
        svg.append(
            f'<path d="{_curve_path(x1, y1, x2, y2, offset)}" fill="none" '
            f'stroke="#9ca3af" stroke-width="1.0" opacity="0.12"/>'
        )
    svg.append("</g>")

    svg.append('<g id="route_edges">')
    for edge in edge_items:
        if (edge.src, edge.dst) not in highlighted:
            continue
        if edge.src not in positions or edge.dst not in positions:
            continue
        x1, y1 = positions[edge.src]
        x2, y2 = positions[edge.dst]
        dx = x2 - x1
        dy = y2 - y1
        length = math.hypot(dx, dy) or 1.0
        sx = x1 + dx / length * 28
        sy = y1 + dy / length * 28
        ex = x2 - dx / length * 28
        ey = y2 - dy / length * 28
        svg.append(
            f'<line x1="{sx:.1f}" y1="{sy:.1f}" x2="{ex:.1f}" y2="{ey:.1f}" '
            f'stroke="#111827" stroke-width="3.2" opacity="0.95"/>'
        )
        mx = (sx + ex) / 2
        my = (sy + ey) / 2
        nx = -dy / length
        ny = dx / length
        order = label_order.get((edge.src, edge.dst), 0)
        side = -1 if order % 2 else 1
        distance = 38.0 + (order % 3) * 18.0
        lx = min(max(mx + nx * side * distance, 92.0), width - 92.0)
        ly = min(max(my + ny * side * distance, 132.0), height - 70.0)
        label = f"{_quality_label(edge.rssi)}  {edge.rssi}dBm  w={edge.weight}"
        svg.append(f'<line x1="{mx:.1f}" y1="{my:.1f}" x2="{lx:.1f}" y2="{ly:.1f}" stroke="{rssi_color(edge.rssi)}" stroke-width="0.9" opacity="0.65"/>')
        svg.append(f'<rect x="{lx - 72:.1f}" y="{ly - 13:.1f}" width="144" height="22" rx="4" fill="#ffffff" stroke="{rssi_color(edge.rssi)}" stroke-width="1.2"/>')
        svg.append(f'<text x="{lx:.1f}" y="{ly + 2:.1f}" text-anchor="middle" font-size="11" font-family="Arial, sans-serif" fill="#111827">{html.escape(label)}</text>')
    svg.append("</g>")

    svg.append('<g id="nodes">')
    for node in nodes:
        x, y = positions[node]
        is_gateway = node == gateway
        on_route = node in path_nodes
        fill = "#111827" if is_gateway else "#ffffff"
        stroke = "#111827" if on_route else "#9ca3af"
        text_fill = "#ffffff" if is_gateway else "#111827"
        radius = 30 if is_gateway else 25
        svg.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="{radius}" fill="{fill}" stroke="{stroke}" stroke-width="{3 if on_route else 1.5}"/>')
        svg.append(
            f'<text x="{x:.1f}" y="{y + 5:.1f}" text-anchor="middle" font-size="15" '
            f'font-family="Arial, sans-serif" fill="{text_fill}" font-weight="700">{format_addr(node)}</text>'
        )
    svg.append("</g>")
    svg.append("</svg>")
    return "\n".join(svg) + "\n"


def _quality_label(rssi: int) -> str:
    if rssi >= -55:
        return "优"
    if rssi >= -65:
        return "良"
    if rssi >= -75:
        return "中"
    if rssi >= -85:
        return "弱"
    return "差"


def write_topology_svg(path: str | Path, topology: Topology, summary: dict | None = None) -> None:
    output_path = Path(path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(build_topology_svg(topology, summary), encoding="utf-8")


def topology_to_dict(topology: Topology) -> dict:
    return {
        "stale_seconds": topology.stale_seconds,
        "edge_direction": topology.edge_direction,
        "nodes": sorted(topology.nodes),
        "edges": [asdict(edge) for edge in sorted(topology.edges.values(), key=lambda item: (item.src, item.dst))],
    }
