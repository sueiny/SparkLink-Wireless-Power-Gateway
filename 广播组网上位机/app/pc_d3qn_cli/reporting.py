from __future__ import annotations

import html
import math
import statistics
from dataclasses import asdict
from pathlib import Path

from .protocol import format_addr
from .topology import Topology, edge_key


def path_text(route: list[int]) -> str:
    return " -> ".join(format_addr(addr) for addr in route)


def route_hops(route: list[int]) -> int:
    return max(0, len(route) - 1)


def _fmt_ms(value: float | None) -> str:
    return "n/a" if value is None else f"{value:.1f}ms"


def _fmt_rate(value: float | None) -> str:
    return "n/a" if value is None else f"{value:.2%}"


def _fmt_num(value) -> str:
    if value is None:
        return "n/a"
    if isinstance(value, float):
        return f"{value:.4f}".rstrip("0").rstrip(".")
    return str(value)


def link_quality(rssi: int | None) -> str:
    if rssi is None:
        return "unknown"
    if rssi >= -55:
        return "优"
    if rssi >= -65:
        return "良"
    if rssi >= -75:
        return "中"
    if rssi >= -85:
        return "弱"
    return "差"


def rssi_fluctuation(topology: Topology) -> dict:
    values = [edge.rssi for edge in topology.edges.values()]
    if not values:
        return {"min": None, "max": None, "range": None, "stddev": None, "count": 0}
    return {
        "min": min(values),
        "max": max(values),
        "range": max(values) - min(values),
        "stddev": statistics.pstdev(values) if len(values) > 1 else 0.0,
        "count": len(values),
    }


def latency_jitter(latencies_ms: list[float]) -> dict:
    if not latencies_ms:
        return {"jitter_ms": None, "stddev_ms": None}
    diffs = [abs(b - a) for a, b in zip(latencies_ms[:-1], latencies_ms[1:])]
    return {
        "jitter_ms": statistics.fmean(diffs) if diffs else 0.0,
        "stddev_ms": statistics.pstdev(latencies_ms) if len(latencies_ms) > 1 else 0.0,
    }


def _summary_items(summary: dict) -> dict:
    return summary.get("pairs") or summary.get("targets", {})


def enrich_summary(summary: dict, topology: Topology, model_compute_ms: float | None) -> None:
    for collection in (summary.get("targets", {}), summary.get("pairs", {})):
        for item in collection.values():
            route = item.get("last_route", [])
            values = []
            for src, dst in zip(route[:-1], route[1:]):
                edge = topology.edges.get(edge_key(int(src), int(dst)))
                if edge is not None:
                    values.append(edge.rssi)
            item["route_path"] = path_text(route)
            item["route_hops"] = route_hops(route)
            item["path_rssi"] = {
                "route": route,
                "mean_rssi": statistics.fmean(values) if values else None,
                "min_rssi": min(values) if values else None,
                "source": "real_rssi" if values else "unavailable",
            }
            latency = item.get("latency", {}).get("avg_ms")
            item["algorithm_compute_latency_ms"] = model_compute_ms
            item["command_downlink_latency_ms"] = latency
            item["end_to_end_avg_latency_ms"] = latency
            item["avg_single_hop_latency_ms"] = latency / item["route_hops"] if latency is not None and item["route_hops"] else None
            item["latency_jitter"] = latency_jitter(
                [
                    float(result.get("latency_ms"))
                    for result in summary.get("rounds", [])
                    if result.get("target") == int(item["destination"], 16)
                    and result.get("success")
                    and result.get("latency_ms") is not None
                ]
            )
    total_latencies = [
        float(result.get("latency_ms"))
        for result in summary.get("rounds", [])
        if result.get("success") and result.get("latency_ms") is not None
    ]
    display_items = _summary_items(summary)
    hop_values = [item.get("route_hops", 0) for item in display_items.values()]
    per_hop = [
        item["avg_single_hop_latency_ms"]
        for item in display_items.values()
        if item.get("avg_single_hop_latency_ms") is not None
    ]
    summary["metrics"] = {
        "algorithm_compute_latency_ms": model_compute_ms,
        "inference_latency": summary["total"].get("inference_latency", {}),
        "source_to_target_latency": summary["total"].get("source_to_target_latency", {}),
        "command_downlink_latency_ms": summary["total"]["latency"]["avg_ms"],
        "end_to_end_avg_latency_ms": summary["total"]["latency"]["avg_ms"],
        "global_avg_loss_rate": summary["total"]["loss_rate"],
        "avg_route_hops": statistics.fmean(hop_values) if hop_values else 0.0,
        "avg_single_hop_latency_ms": statistics.fmean(per_hop) if per_hop else None,
        "rssi_fluctuation": rssi_fluctuation(topology),
        "latency_jitter": latency_jitter(total_latencies),
        "d3qn_route_failures": summary.get("d3qn_route_failures", 0),
    }


def write_text_topology(path: str | Path, topology: Topology, summary: dict | None = None, gateway: int = 0) -> None:
    lines = [
        "D3QN_MPNN RSSI Topology",
        f"Gateway: {format_addr(gateway)}",
        f"Edge direction: {topology.edge_direction}",
        "",
        "D3QN selected paths:",
    ]
    if summary:
        for target, item in sorted(summary.get("targets", {}).items()):
            route = item.get("last_route", [])
            lines.append(
                f"  {format_addr(gateway)} -> {target}: {path_text(route)} "
                f"(action={item.get('last_action')}, hops={route_hops(route)}, "
                f"loss={_fmt_rate(item.get('loss_rate'))}, min_rssi={item.get('path_rssi', {}).get('min_rssi')})"
            )
            for src, dst in zip(route[:-1], route[1:]):
                edge = topology.edges.get(edge_key(int(src), int(dst)))
                if edge:
                    lines.append(f"    {format_addr(src)} -> {format_addr(dst)} quality={link_quality(edge.rssi)} RSSI={edge.rssi}dBm weight={edge.weight}")
    lines.extend(["", "Edges:"])
    for edge in sorted(topology.edges.values(), key=lambda item: (item.src, item.dst)):
        lines.append(f"  {format_addr(edge.src)} -> {format_addr(edge.dst)} RSSI={edge.rssi}dBm weight={edge.weight} source={edge.source}")
    output = Path(path)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")


def _rssi_color(rssi: int) -> str:
    if rssi >= -55:
        return "#238b45"
    if rssi >= -65:
        return "#2b6cb0"
    if rssi >= -75:
        return "#b7791f"
    if rssi >= -85:
        return "#c53030"
    return "#7a7f87"


def write_topology_svg(path: str | Path, topology: Topology, summary: dict | None = None, gateway: int = 0) -> None:
    width, height = 1680, 1080
    nodes = sorted(topology.nodes | {gateway})
    positions = _layout(nodes, width, height, gateway)
    highlighted = set()
    if summary:
        for item in summary.get("targets", {}).values():
            for src, dst in zip(item.get("last_route", [])[:-1], item.get("last_route", [])[1:]):
                highlighted.add((int(src), int(dst)))
    svg = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="#ffffff"/>',
        '<text x="42" y="48" font-size="24" font-family="Arial, sans-serif" fill="#111827" font-weight="700">D3QN_MPNN RSSI Topology</text>',
        '<text x="42" y="76" font-size="13" font-family="Arial, sans-serif" fill="#4b5563">图中不显示箭头；深色线为 D3QN 选中路径，浅灰线为未使用 RSSI 链路。</text>',
    ]
    svg.append('<g id="edges">')
    # 无向图：去重，小节点在前
    drawn_edges: set[tuple[int, int]] = set()
    for edge in sorted(topology.edges.values(), key=lambda item: (item.src, item.dst)):
        a, b = sorted((edge.src, edge.dst))
        if (a, b) in drawn_edges:
            continue
        if a not in positions or b not in positions:
            continue
        drawn_edges.add((a, b))
        x1, y1 = positions[a]
        x2, y2 = positions[b]
        selected = (a, b) in highlighted or (b, a) in highlighted
        svg.append(
            f'<line x1="{x1:.1f}" y1="{y1:.1f}" x2="{x2:.1f}" y2="{y2:.1f}" '
            f'stroke="{("#111827" if selected else "#9ca3af")}" stroke-width="{3.2 if selected else 1.0}" opacity="{0.92 if selected else 0.16}"/>'
        )
        if selected:
            mx = (x1 + x2) / 2
            my = (y1 + y2) / 2
            label = f"{link_quality(edge.rssi)} {edge.rssi}dBm w={edge.weight}"
            svg.append(f'<rect x="{mx - 64:.1f}" y="{my - 32:.1f}" width="128" height="22" rx="4" fill="#ffffff" stroke="{_rssi_color(edge.rssi)}"/>')
            svg.append(f'<text x="{mx:.1f}" y="{my - 16:.1f}" text-anchor="middle" font-size="11" font-family="Arial, sans-serif" fill="#111827">{html.escape(label)}</text>')
    svg.append("</g>")
    svg.append('<g id="nodes">')
    path_nodes = {node for edge in highlighted for node in edge} | {gateway}
    for node in nodes:
        x, y = positions[node]
        is_gateway = node == gateway
        on_path = node in path_nodes
        svg.append(f'<circle cx="{x:.1f}" cy="{y:.1f}" r="{32 if is_gateway else 26}" fill="{("#111827" if is_gateway else "#ffffff")}" stroke="{("#111827" if on_path else "#9ca3af")}" stroke-width="{3 if on_path else 1.4}"/>')
        svg.append(f'<text x="{x:.1f}" y="{y + 5:.1f}" text-anchor="middle" font-size="15" font-family="Arial, sans-serif" fill="{("#ffffff" if is_gateway else "#111827")}" font-weight="700">{format_addr(node)}</text>')
    svg.append("</g></svg>")
    output = Path(path)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(svg) + "\n", encoding="utf-8")


def _layout(nodes: list[int], width: int, height: int, gateway: int) -> dict[int, tuple[float, float]]:
    positions = {gateway: (width * 0.18, height * 0.52)}
    others = [node for node in nodes if node != gateway]
    center_x, center_y = width * 0.58, height * 0.52
    radius_x, radius_y = 500.0, 340.0
    count = max(1, len(others))
    for index, node in enumerate(others):
        angle = -math.pi * 0.82 + (math.pi * 1.64) * index / max(1, count - 1)
        positions[node] = (
            min(max(center_x + radius_x * math.cos(angle), 110.0), width - 110.0),
            min(max(center_y + radius_y * math.sin(angle), 150.0), height - 110.0),
        )
    return positions


def write_excel_summary(path: str | Path, summary: dict, topology: Topology, config: dict) -> None:
    try:
        from openpyxl import Workbook
    except ImportError:
        Path(path).with_suffix(".csv").write_text("openpyxl unavailable; install D3QN env to write xlsx\n", encoding="utf-8")
        return

    workbook = Workbook()
    default = workbook.active
    workbook.remove(default)

    def sheet(name: str, rows: list[list[object]]) -> None:
        ws = workbook.create_sheet(name)
        for row in rows:
            ws.append(row)

    result_rows = [["出发点", "目标点", "路径", "D3QN动作", "成功/实际SEND", "计划轮次", "ACK timeout", "D3QN路由失败", "丢包率", "点到点平均ms", "点到点P95ms", "推理平均ms", "源到目标平均ms", "总延时平均ms", "重采次数", "路径切换次数", "最弱RSSI"]]
    for key, item in sorted(_summary_items(summary).items()):
        latency = item.get("latency", {})
        result_rows.append([
            item.get("source", "00"),
            item.get("destination", key),
            item.get("route_path"),
            item.get("last_action"),
            f"{item.get('success')}/{item.get('sent')}",
            item.get("planned_rounds"),
            item.get("ack_timeout_loss"),
            item.get("d3qn_route_failures"),
            item.get("loss_rate"),
            latency.get("avg_ms"),
            latency.get("p95_ms"),
            item.get("inference_latency", {}).get("avg_ms"),
            item.get("source_to_target_latency", {}).get("avg_ms"),
            item.get("total_latency_ms"),
            item.get("recollect_count"),
            item.get("path_switch_count"),
            item.get("path_rssi", {}).get("min_rssi"),
        ])

    metrics = summary.get("metrics", {})
    rssi = metrics.get("rssi_fluctuation", {})
    jitter = metrics.get("latency_jitter", {})
    metric_rows = [
        ["指标", "值", "单位", "说明"],
        ["算法计算延时", metrics.get("algorithm_compute_latency_ms"), "ms", "上位机加载 D3QN 并选择路径的平均耗时"],
        ["推理时间", metrics.get("inference_latency", {}).get("avg_ms"), "ms", "网关收到源ACK到下发路径的时间"],
        ["源到目标延时", metrics.get("source_to_target_latency", {}).get("avg_ms"), "ms", "网关下发路径到收到目标ACK的时间"],
        ["指令下发延时", metrics.get("command_downlink_latency_ms"), "ms", "当前按 SEND 到 ACK 总时延近似"],
        ["端到端实际传输平均延时", metrics.get("end_to_end_avg_latency_ms"), "ms", "现有统计总 ACK 时延"],
        ["全局平均丢包率", metrics.get("global_avg_loss_rate"), "ratio", "总 timeout / 总发送"],
        ["D3QN路由失败次数", metrics.get("d3qn_route_failures"), "count", "无候选路径或模型不可用导致的失败"],
        ["单路径平均跳数", metrics.get("avg_route_hops"), "hops", "各目标最终路径跳数平均值"],
        ["平均单跳传输耗时", metrics.get("avg_single_hop_latency_ms"), "ms/hop", "端到端平均延时 / 跳数折算"],
        ["RSSI最小值", rssi.get("min"), "dBm", "当前拓扑边 RSSI 最小值"],
        ["RSSI最大值", rssi.get("max"), "dBm", "当前拓扑边 RSSI 最大值"],
        ["RSSI波动范围", rssi.get("range"), "dB", "max-min"],
        ["RSSI标准差", rssi.get("stddev"), "dB", "当前拓扑边 RSSI 标准差"],
        ["时延抖动均值", jitter.get("jitter_ms"), "ms", "相邻成功 ACK 延时差值均值"],
        ["时延标准差", jitter.get("stddev_ms"), "ms", "成功 ACK 延时标准差"],
    ]

    path_rows = [["出发点", "目标点", "D3QN动作", "候选路径数", "路由跳数", "单路径丢包率", "平均单跳传输耗时ms", "推理时间ms", "源到目标延时ms", "总延时ms", "路径切换原因", "RSSI均值", "最弱RSSI"]]
    for key, item in sorted(_summary_items(summary).items()):
        path_rows.append([
            item.get("source", "00"),
            item.get("destination", key),
            item.get("last_action"),
            item.get("candidate_path_count"),
            item.get("route_hops"),
            item.get("loss_rate"),
            item.get("avg_single_hop_latency_ms"),
            item.get("inference_latency", {}).get("avg_ms"),
            item.get("source_to_target_latency", {}).get("avg_ms"),
            item.get("total_latency_ms"),
            ", ".join(item.get("path_switch_reasons", [])),
            item.get("path_rssi", {}).get("mean_rssi"),
            item.get("path_rssi", {}).get("min_rssi"),
        ])

    rssi_rows = [["src", "dst", "rssi", "weight", "direction", "source", "updated_at"]]
    for edge in sorted(topology.edges.values(), key=lambda item: (item.src, item.dst)):
        data = asdict(edge)
        rssi_rows.append([format_addr(edge.src), format_addr(edge.dst), edge.rssi, edge.weight, data.get("direction"), data.get("source"), data.get("updated_at")])

    config_rows = [["参数", "值"]]
    for key, value in sorted(config.items()):
        config_rows.append([key, str(value)])

    sheet("测试结果", result_rows)
    sheet("核心指标", metric_rows)
    sheet("路径对比", path_rows)
    sheet("RSSI拓扑", rssi_rows)
    sheet("测试参数", config_rows)
    output = Path(path)
    output.parent.mkdir(parents=True, exist_ok=True)
    workbook.save(output)


def build_report(summary: dict, hardware_record: dict, log_dir: str | Path) -> str:
    total = summary["total"]
    metrics = summary.get("metrics", {})
    rssi = metrics.get("rssi_fluctuation", {})
    jitter = metrics.get("latency_jitter", {})
    lines = [
        "# D3QN_MPNN 真实硬件测试汇总报告",
        "",
        f"- 日志目录：`{log_dir}`",
        f"- 算法：`D3QN_MPNN`",
        "- 推理策略：`纯D3QN，无Dijkstra fallback，无规则兜底`",
        "- 目标：有效 SEND 平均点到点延时 `<220ms`，实际 ACK 丢包率 `<10%`；路由失败单独统计。",
        f"- Checkpoint：`{hardware_record['routing_params']['checkpoint']}`",
        f"- 节点：`{', '.join(hardware_record['test_config']['nodes']['targets'])}`",
        "- 地址说明：CLI 按十六进制地址解析，因此目标 `10` 表示地址 `0x10`。",
        f"- 计划轮次：`{total.get('planned_rounds')}`，实际SEND：`{total['sent']}`，成功：`{total['success']}`，ACK timeout：`{total.get('ack_timeout_loss')}`，D3QN路由失败：`{total.get('route_failed')}`，实际丢包率：`{_fmt_rate(total['loss_rate'])}`",
        f"- 端到端平均延时：`{_fmt_ms(total['latency'].get('avg_ms'))}`，P95：`{_fmt_ms(total['latency'].get('p95_ms'))}`，最小/最大：`{_fmt_ms(total['latency'].get('min_ms'))}` / `{_fmt_ms(total['latency'].get('max_ms'))}`",
        f"- 推理时间平均：`{_fmt_ms(metrics.get('inference_latency', {}).get('avg_ms'))}`，源到目标平均：`{_fmt_ms(metrics.get('source_to_target_latency', {}).get('avg_ms'))}`",
        f"- 时延抖动均值：`{_fmt_ms(jitter.get('jitter_ms'))}`，时延标准差：`{_fmt_ms(jitter.get('stddev_ms'))}`",
        f"- D3QN 路由失败次数：`{metrics.get('d3qn_route_failures')}`",
        "",
        "## 拓扑图",
        "",
        "![D3QN RSSI 拓扑图](拓扑图.svg)",
        "",
        "## 测试结果",
        "",
        "| 出发点 | 目标点 | 路径 | D3QN动作 | 成功/实际SEND | ACK timeout | 路由失败 | 丢包率 | 点到点平均 | P95 | 推理平均 | 源到目标平均 | 总延时 | 重采 | 切换 | 最弱 RSSI |",
        "|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|",
    ]
    for key, item in sorted(_summary_items(summary).items()):
        latency = item["latency"]
        inference = item.get("inference_latency", {})
        source_to_target = item.get("source_to_target_latency", {})
        lines.append(
            f"| `{item.get('source', '00')}` | `{item.get('destination', key)}` | `{item.get('route_path')}` | `{item.get('last_action')}` | `{item['success']}/{item['sent']}` | "
            f"`{item.get('ack_timeout_loss', 0)}` | `{item.get('d3qn_route_failures', 0)}` | `{_fmt_rate(item['loss_rate'])}` | `{_fmt_ms(latency['avg_ms'])}` | `{_fmt_ms(latency['p95_ms'])}` | "
            f"`{_fmt_ms(inference.get('avg_ms'))}` | `{_fmt_ms(source_to_target.get('avg_ms'))}` | `{_fmt_ms(item.get('total_latency_ms'))}` | `{item.get('recollect_count')}` | `{item.get('path_switch_count')}` | `{item.get('path_rssi', {}).get('min_rssi')}` |"
        )
    learn_info = total.get("online_learn_latency", {})
    learn_events = learn_info.get("events", [])
    if learn_events:
        lines.extend([
            "",
            "## 在线学习触发记录",
            "",
            f"共触发 **{learn_info.get('update_count', 0)}** 次，合计耗时 `{_fmt_ms(learn_info.get('total_ms'))}ms`，均摊每轮 `{_fmt_ms(learn_info.get('amortized_per_round_ms'))}ms`",
            "",
            "| 轮次 (round_index) | 触发轮 source→target | 更新耗时 |",
            "|---:|---|---:|",
        ])
        for ev in learn_events:
            lines.append(f"| {ev['round_index']} | `{ev['source']}→{ev['target']}` | `{_fmt_ms(ev['ms'])}ms` |")
    else:
        lines.extend([
            "",
            "## 在线学习触发记录",
            "",
            "本次测试未触发在线学习更新（未开启或未达到触发间隔）。",
        ])
    lines.extend([
        "",
        "## 指标总结对比",
        "",
        "| 指标 | 当前值 | 单位 | 说明 |",
        "|---|---:|---|---|",
        f"| 算法计算延时 | `{_fmt_ms(metrics.get('algorithm_compute_latency_ms'))}` | ms | 上位机用 D3QN 算出路径的平均耗时 |",
        f"| 推理时间 | `{_fmt_ms(metrics.get('inference_latency', {}).get('avg_ms'))}` | ms | 网关收到源ACK到下发路径的时间 |",
        f"| 源到目标延时 | `{_fmt_ms(metrics.get('source_to_target_latency', {}).get('avg_ms'))}` | ms | 网关下发路径到收到目标ACK的时间 |",
        f"| 指令下发延时 | `{_fmt_ms(metrics.get('command_downlink_latency_ms'))}` | ms | 当前硬件无中间节点时间戳，用 SEND 到 ACK 总时延近似 |",
        f"| 端到端实际传输平均延时 | `{_fmt_ms(metrics.get('end_to_end_avg_latency_ms'))}` | ms | 现有统计总 ACK 时延 |",
        f"| 全局平均丢包率 | `{_fmt_rate(metrics.get('global_avg_loss_rate'))}` | ratio | 总 timeout / 总发送 |",
        f"| D3QN 路由失败次数 | `{metrics.get('d3qn_route_failures')}` | count | 无候选路径、checkpoint 缺失或模型输入不匹配 |",
        f"| 单路径平均跳数 | `{_fmt_num(metrics.get('avg_route_hops'))}` | hops | 各目标最终路径跳数平均值 |",
        f"| 平均单跳传输耗时 | `{_fmt_ms(metrics.get('avg_single_hop_latency_ms'))}` | ms/hop | 端到端平均延时 / 跳数折算 |",
        f"| RSSI 实时波动范围 | `{_fmt_num(rssi.get('range'))}` | dB | 当前拓扑边 RSSI 最大值减最小值 |",
        f"| RSSI 标准差 | `{_fmt_num(rssi.get('stddev'))}` | dB | 当前拓扑边 RSSI 标准差 |",
        f"| 时延抖动均值 | `{_fmt_ms(jitter.get('jitter_ms'))}` | ms | 相邻成功 ACK 延时差值均值 |",
        f"| 时延标准差 | `{_fmt_ms(jitter.get('stddev_ms'))}` | ms | 成功 ACK 延时标准差 |",
        "",
        "## 文件",
        "",
        "- [`测试指标汇总.xlsx`](测试指标汇总.xlsx)",
        "- [`拓扑图.txt`](拓扑图.txt)",
        "- [`原始串口日志.log`](原始串口日志.log)",
        "- `原始JSON数据/model_decisions.jsonl`",
        "- `原始JSON数据/d3qn_state.json`",
        "",
        "## 来源说明",
        "",
        "| 来源 | 含义 |",
        "|---|---|",
        "| `real_rssi` | 由 RSSI_REQ 和 RSSI_REPORT 得到 |",
        "| `real_ack` | 由真实 ACK 成功/timeout 统计得到 |",
        "| `default` | 当前硬件不可直接测量，使用默认值占位 |",
        "| `derived` | 由真实测试记录派生计算得到 |",
        "| `derived_from_rssi` | 训练环境中容量、延时、丢包等不可测字段由真实 RSSI 分段派生 |",
    ])
    return "\n".join(lines) + "\n"


def write_report(path: str | Path, summary: dict, hardware_record: dict) -> None:
    output = Path(path)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(build_report(summary, hardware_record, output.parent), encoding="utf-8")
