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
            a, b = min(int(src), int(dst)), max(int(src), int(dst))
            highlighted.add((a, b))
    return highlighted


def _undirected_edges(topology: Topology) -> list[dict]:
    seen: dict[tuple[int, int], dict] = {}
    for edge in topology.edges.values():
        a, b = min(edge.src, edge.dst), max(edge.src, edge.dst)
        key = (a, b)
        if key not in seen:
            seen[key] = {"src": a, "dst": b, "rssi": edge.rssi, "weight": edge.weight, "samples": list(edge.samples or [edge.rssi])}
        else:
            seen[key]["samples"].extend(edge.samples or [edge.rssi])
    result = []
    for (a, b), info in seen.items():
        samples = sorted(info["samples"])
        mid = len(samples) // 2
        if len(samples) % 2 == 0:
            median_rssi = int(round((samples[mid - 1] + samples[mid]) / 2))
        else:
            median_rssi = int(round(samples[mid]))
        result.append({"src": a, "dst": b, "rssi": median_rssi, "weight": info["weight"]})
    return sorted(result, key=lambda e: (e["src"], e["dst"]))


def _fixed_layout(width: int, height: int) -> dict[int, tuple[float, float]]:
    """根据实际硬件摆放位置的固定布局"""
    margin_x = 200.0
    margin_y = 150.0
    col_w = (width - 2 * margin_x) / 5
    row_h = (height - 2 * margin_y) / 6

    positions = {
        0x00: (margin_x + 0.5 * col_w, margin_y + 3 * row_h),
        0x01: (margin_x + 4.5 * col_w, margin_y + 5 * row_h),
        0x02: (margin_x + 2 * col_w, margin_y + 1 * row_h),
        0x03: (margin_x + 2 * col_w, margin_y + 2.5 * row_h),
        0x04: (margin_x + 4.5 * col_w, margin_y + 4 * row_h),
        0x05: (margin_x + 4.5 * col_w, margin_y + 0 * row_h),
        0x06: (margin_x + 2 * col_w, margin_y + 3.5 * row_h),
        0x07: (margin_x + 3 * col_w, margin_y + 0.5 * row_h),
        0x08: (margin_x + 2 * col_w, margin_y + 5 * row_h),
        0x09: (margin_x + 1 * col_w, margin_y + 0 * row_h),
        0x0A: (margin_x + 3 * col_w, margin_y + 0 * row_h),
    }
    return positions


def _rssi_based_layout(nodes: list[int], edges: list[dict], width: int, height: int, gateway: int) -> dict[int, tuple[float, float]]:
    """基于RSSI的力导向布局"""
    import random
    random.seed(42)
    margin = 120.0
    cx, cy = width / 2, height / 2

    # 初始随机位置
    positions: dict[int, tuple[float, float]] = {}
    for node in nodes:
        if node == gateway:
            positions[node] = (margin, cy)
        else:
            angle = random.uniform(0, 2 * math.pi)
            r = random.uniform(300, min(width, height) * 0.45)
            positions[node] = (cx + r * math.cos(angle), cy + r * math.sin(angle))

    # RSSI到理想距离的映射
    ideal_dist: dict[tuple[int, int], float] = {}
    for e in edges:
        a, b = min(e["src"], e["dst"]), max(e["src"], e["dst"])
        rssi = e["rssi"]
        # RSSI越强(越接近0)，距离越近
        strength = max(0.0, (rssi + 110) / 55.0)  # 归一化到0-1
        ideal_dist[(a, b)] = 200.0 + (1.0 - strength) * 700.0

    # 力导向迭代
    iterations = 400
    temp = 350.0
    cooling = temp / iterations

    for iteration in range(iterations):
        forces: dict[int, list[float]] = {node: [0.0, 0.0] for node in nodes}

        # 弹簧力(吸引)
        for i, a in enumerate(nodes):
            for b in nodes[i + 1:]:
                ax, ay = positions[a]
                bx, by = positions[b]
                dx, dy = bx - ax, by - ay
                dist = math.hypot(dx, dy) or 0.01
                ux, uy = dx / dist, dy / dist

                pair = (min(a, b), max(a, b))
                ideal = ideal_dist.get(pair, 400.0)

                diff = dist - ideal
                force = diff * 0.04

                forces[a][0] += ux * force
                forces[a][1] += uy * force
                forces[b][0] -= ux * force
                forces[b][1] -= uy * force

        # 排斥力
        for i, a in enumerate(nodes):
            for b in nodes[i + 1:]:
                ax, ay = positions[a]
                bx, by = positions[b]
                dx, dy = bx - ax, by - ay
                dist = math.hypot(dx, dy) or 0.01
                ux, uy = dx / dist, dy / dist

                repulse = 10000.0 / (dist * dist + 150.0)
                forces[a][0] -= ux * repulse
                forces[a][1] -= uy * repulse
                forces[b][0] += ux * repulse
                forces[b][1] += uy * repulse

        # 更新位置
        for node in nodes:
            if node == gateway:
                continue
            fx, fy = forces[node]
            f_mag = math.hypot(fx, fy)
            if f_mag > 0:
                step = min(f_mag, temp) / f_mag
                nx = positions[node][0] + fx * step
                ny = positions[node][1] + fy * step
            else:
                nx, ny = positions[node]

            nx = max(margin, min(width - margin, nx))
            ny = max(margin, min(height - margin, ny))
            positions[node] = (nx, ny)

        temp -= cooling

    return positions


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


def build_topology_svg(
    topology: Topology,
    summary: dict | None = None,
    width: int = 1680,
    height: int = 1080,
    gateway: int = 0,
) -> str:
    nodes = sorted(topology.nodes | {gateway})
    edges = _undirected_edges(topology)
    positions = _rssi_based_layout(nodes, edges, width, height, gateway)
    highlighted = _path_edges(summary)
    path_nodes = {gateway}
    for src, dst in highlighted:
        path_nodes.add(src)
        path_nodes.add(dst)

    svg: list[str] = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="#ffffff"/>',
        '<text x="42" y="46" font-size="24" font-family="Arial, sans-serif" fill="#111827" font-weight="700">Dijkstra RSSI Topology (Undirected)</text>',
        '<text x="42" y="74" font-size="13" font-family="Arial, sans-serif" fill="#4b5563">无向图：每条边为双向链路，RSSI 取中位数。深色为最短路径，浅灰为观测链路。</text>',
    ]

    legend = [
        (">= -55 dBm 优", "#238b45"),
        ("-56~-65 良", "#2b6cb0"),
        ("-66~-75 中", "#b7791f"),
        ("-76~-85 弱", "#c53030"),
        ("<-85 差", "#7a7f87"),
        ("route", "#111827"),
    ]
    for index, (label, color) in enumerate(legend):
        x = 42 + index * 120
        svg.append(f'<line x1="{x}" y1="108" x2="{x + 28}" y2="108" stroke="{color}" stroke-width="4" stroke-linecap="round"/>')
        svg.append(f'<text x="{x + 36}" y="113" font-size="11" font-family="Arial, sans-serif" fill="#374151">{html.escape(label)}</text>')

    svg.append('<g id="observed_edges">')
    for edge in edges:
        pair = (min(edge["src"], edge["dst"]), max(edge["src"], edge["dst"]))
        if pair in highlighted:
            continue
        if edge["src"] not in positions or edge["dst"] not in positions:
            continue
        x1, y1 = positions[edge["src"]]
        x2, y2 = positions[edge["dst"]]
        svg.append(
            f'<line x1="{x1:.1f}" y1="{y1:.1f}" x2="{x2:.1f}" y2="{y2:.1f}" '
            f'stroke="{rssi_color(edge["rssi"])}" stroke-width="1.2" opacity="0.25"/>'
        )
    svg.append("</g>")

    svg.append('<g id="route_edges">')
    for idx, edge in enumerate(edges):
        pair = (min(edge["src"], edge["dst"]), max(edge["src"], edge["dst"]))
        if pair not in highlighted:
            continue
        if edge["src"] not in positions or edge["dst"] not in positions:
            continue
        x1, y1 = positions[edge["src"]]
        x2, y2 = positions[edge["dst"]]
        
        # 添加偏移避免重叠
        offset = ((idx * 17) % 21 - 10) * 3.0  # -30到+30的偏移
        dx = x2 - x1
        dy = y2 - y1
        length = math.hypot(dx, dy) or 1.0
        nx = -dy / length * offset
        ny = dx / length * offset
        
        svg.append(
            f'<line x1="{x1 + nx:.1f}" y1="{y1 + ny:.1f}" x2="{x2 + nx:.1f}" y2="{y2 + ny:.1f}" '
            f'stroke="#111827" stroke-width="2.5" opacity="0.95"/>'
        )
        mx = (x1 + x2) / 2 + nx
        my = (y1 + y2) / 2 + ny
        label = f"{_quality_label(edge['rssi'])}  {edge['rssi']}dBm"
        svg.append(f'<text x="{mx:.1f}" y="{my - 8:.1f}" text-anchor="middle" font-size="10" font-family="Arial, sans-serif" fill="#111827">{html.escape(label)}</text>')
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


def write_topology_svg(path: str | Path, topology: Topology, summary: dict | None = None) -> None:
    output_path = Path(path)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(build_topology_svg(topology, summary), encoding="utf-8")


def write_topology_svgs_by_source(path: str | Path, topology: Topology, summary: dict | None = None) -> None:
    """为每个源节点生成单独的拓扑图，显示从该源节点到所有目标节点的路由"""
    output_dir = Path(path) / "拓扑图"
    output_dir.mkdir(parents=True, exist_ok=True)
    
    if not summary:
        return
    
    # 按源节点分组（从pairs中获取）
    sources: dict[str, dict] = {}
    for pair_key, pair_item in summary.get("pairs", {}).items():
        source = pair_item.get("source", "00")
        if source not in sources:
            sources[source] = {"targets": {}}
        # 提取目标节点
        destination = pair_item.get("destination", "")
        sources[source]["targets"][destination] = pair_item
    
    # 为每个源节点生成拓扑图
    for source_key, source_summary in sources.items():
        source_node = int(source_key, 16)
        svg_content = build_topology_svg(topology, source_summary)
        svg_path = output_dir / f"源节点{source_key}.svg"
        svg_path.write_text(svg_content, encoding="utf-8")


def topology_to_dict(topology: Topology) -> dict:
    return {
        "stale_seconds": topology.stale_seconds,
        "edge_direction": topology.edge_direction,
        "nodes": sorted(topology.nodes),
        "edges": [asdict(edge) for edge in sorted(topology.edges.values(), key=lambda item: (item.src, item.dst))],
    }
