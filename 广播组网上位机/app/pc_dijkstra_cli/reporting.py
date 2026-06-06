from __future__ import annotations

import statistics
from dataclasses import asdict
from pathlib import Path

from .excel_report import write_xlsx
from .protocol import format_addr
from .topology import Topology


def path_text(route: list[int]) -> str:
    return " -> ".join(format_addr(addr) for addr in route)


def route_hops(route: list[int]) -> int:
    return max(0, len(route) - 1)


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


def build_text_topology(topology: Topology, summary: dict | None = None, gateway: int = 0) -> str:
    lines = [
        "Dijkstra RSSI Topology",
        f"Gateway: {format_addr(gateway)}",
        f"Edge direction: {topology.edge_direction}",
        "",
        "Shortest paths and selected link quality:",
    ]
    if summary:
        for key, item in sorted(_summary_items(summary).items()):
            route = item.get("last_route", [])
            lines.append(
                f"  {item.get('source', format_addr(gateway))} -> {item.get('destination', key)}: {path_text(route)} "
                f"(hops={route_hops(route)}, cost={item.get('last_cost')}, "
                f"loss={_rate(item.get('loss_rate'))}, min_rssi={item.get('path_rssi', {}).get('min_rssi')})"
            )
            for src, dst in zip(route[:-1], route[1:]):
                edge = topology.edges.get(f"{int(src):02X}:{int(dst):02X}")
                if edge is None:
                    lines.append(f"    {format_addr(src)} -> {format_addr(dst)}  quality=unknown")
                else:
                    lines.append(
                        f"    {format_addr(src)} -> {format_addr(dst)}  "
                        f"quality={link_quality(edge.rssi)}  RSSI={edge.rssi}dBm  weight={edge.weight}"
                    )
    lines.extend(["", "Edges:"])
    for edge in sorted(topology.edges.values(), key=lambda item: (item.src, item.dst)):
        lines.append(
            f"  {format_addr(edge.src)} -> {format_addr(edge.dst)}  "
            f"RSSI={edge.rssi}dBm  weight={edge.weight}  source={edge.source}"
        )
    return "\n".join(lines) + "\n"


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


def write_text_topology(path: str | Path, topology: Topology, summary: dict | None = None, gateway: int = 0) -> None:
    output = Path(path)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(build_text_topology(topology, summary, gateway=gateway), encoding="utf-8")


def enrich_summary_with_metrics(summary: dict, topology: Topology, route_compute_ms: float | None = None) -> None:
    total_latencies = [
        float(result.get("latency_ms"))
        for result in summary.get("rounds", [])
        if result.get("success") and result.get("latency_ms") is not None
    ]
    route_hop_values = []
    per_hop_delays = []
    pairs_collection = summary.get("pairs", {})
    for collection in (summary.get("targets", {}), pairs_collection):
        is_pairs = collection is pairs_collection
        for key, item in collection.items():
            route = item.get("last_route", [])
            hops = route_hops(route)
            latency = item.get("latency", {}).get("avg_ms")
            item["source"] = item.get("source", format_addr(0))
            item["destination"] = item.get("destination", key)
            item["route_hops"] = hops
            item["route_path"] = path_text(route)
            item["algorithm_compute_latency_ms"] = route_compute_ms
            item["command_downlink_latency_ms"] = latency
            item["end_to_end_avg_latency_ms"] = item.get("total_latency_ms") or latency
            item["avg_single_hop_latency_ms"] = (latency / hops) if latency is not None and hops > 0 else None
            dst_filter = int(item["destination"], 16)
            src_filter = int(item["source"], 16) if is_pairs else None
            item["latency_jitter"] = latency_jitter(
                [
                    float(result.get("latency_ms"))
                    for result in summary.get("rounds", [])
                    if result.get("target") == dst_filter
                    and (src_filter is None or result.get("source") == src_filter)
                    and result.get("success")
                    and result.get("latency_ms") is not None
                ]
            )
            if collection is summary.get("targets", {}):
                route_hop_values.append(hops)
                if item["avg_single_hop_latency_ms"] is not None:
                    per_hop_delays.append(item["avg_single_hop_latency_ms"])
    summary["metrics"] = {
        "algorithm_compute_latency_ms": route_compute_ms,
        "inference_latency": summary["total"].get("inference_latency", {}),
        "source_to_target_latency": summary["total"].get("source_to_target_latency", {}),
        "command_downlink_latency_ms": summary["total"]["latency"]["avg_ms"],
        "end_to_end_avg_latency_ms": summary["total"]["latency"]["avg_ms"],
        "global_avg_loss_rate": summary["total"]["loss_rate"],
        "avg_route_hops": statistics.fmean(route_hop_values) if route_hop_values else 0.0,
        "avg_single_hop_latency_ms": statistics.fmean(per_hop_delays) if per_hop_delays else None,
        "rssi_fluctuation": rssi_fluctuation(topology),
        "latency_jitter": latency_jitter(total_latencies),
    }


def write_excel_summary(path: str | Path, summary: dict, topology: Topology, config: dict | None = None) -> None:
    config = config or summary.get("config", {})
    display_items = _summary_items(summary)
    result_rows = [[
        "出发点",
        "目标点",
        "路径",
        "成功/实际SEND",
        "计划轮次",
        "ACK timeout",
        "路由不可达",
        "丢包率",
        "点到点平均延时ms",
        "P95ms",
        "网关到源节点ms",
        "网关到目标节点ms",
        "推理时间ms",
        "源到目标延时ms",
        "总延时ms",
        "重采次数",
        "最弱RSSI",
    ]]
    for key, item in sorted(display_items.items()):
        latency = item.get("latency", {})
        result_rows.append([
            item.get("source", "00"),
            item.get("destination", key),
            item.get("route_path") or path_text(item.get("last_route", [])),
            f"{item.get('success')}/{item.get('sent')}",
            item.get("planned_rounds"),
            item.get("ack_timeout_loss"),
            item.get("route_failed"),
            item.get("loss_rate"),
            latency.get("avg_ms"),
            latency.get("p95_ms"),
            item.get("gateway_to_source_ms"),
            item.get("gateway_to_target_ms"),
            item.get("inference_ms"),
            item.get("source_to_target_ms"),
            item.get("total_latency_ms"),
            item.get("recollect_count"),
            item.get("path_rssi", {}).get("min_rssi"),
        ])

    metrics = summary.get("metrics", {})
    full_metric_rows = build_full_metric_rows(summary)
    metric_rows = [
        ["指标", "值", "单位", "说明"],
        ["算法计算延时", metrics.get("algorithm_compute_latency_ms"), "ms", "网关/上位机算出 Dijkstra 路由路径的耗时"],
        ["推理时间", metrics.get("inference_latency", {}).get("avg_ms"), "ms", "网关收到源ACK到下发路径的时间"],
        ["源到目标延时", metrics.get("source_to_target_latency", {}).get("avg_ms"), "ms", "网关下发路径到收到目标ACK的时间"],
        ["指令下发延时", metrics.get("command_downlink_latency_ms"), "ms", "当前硬件无中间节点时间戳，第一版用 SEND 到 ACK 总延时近似记录"],
        ["端到端实际传输平均延时", metrics.get("end_to_end_avg_latency_ms"), "ms", "现有统计总 ACK 时延"],
        ["全局平均丢包率", metrics.get("global_avg_loss_rate"), "ratio", "总 timeout / 总发送"],
        ["单路径平均跳数", metrics.get("avg_route_hops"), "hops", "各目标最终路径跳数平均值"],
        ["平均单跳传输耗时", metrics.get("avg_single_hop_latency_ms"), "ms/hop", "端到端平均延时 / 跳数折算"],
        ["RSSI最小值", metrics.get("rssi_fluctuation", {}).get("min"), "dBm", "当前拓扑边 RSSI 最小值"],
        ["RSSI最大值", metrics.get("rssi_fluctuation", {}).get("max"), "dBm", "当前拓扑边 RSSI 最大值"],
        ["RSSI波动范围", metrics.get("rssi_fluctuation", {}).get("range"), "dB", "max RSSI - min RSSI"],
        ["RSSI标准差", metrics.get("rssi_fluctuation", {}).get("stddev"), "dB", "当前拓扑边 RSSI 标准差"],
        ["时延抖动均值", metrics.get("latency_jitter", {}).get("jitter_ms"), "ms", "相邻成功 ACK 延时差值均值"],
        ["时延标准差", metrics.get("latency_jitter", {}).get("stddev_ms"), "ms", "成功 ACK 延时标准差"],
    ]

    path_rows = [[
        "目标点",
        "路由跳数",
        "单路径丢包率",
        "平均单跳传输耗时ms",
        "算法计算延时ms",
        "推理时间ms",
        "源到目标延时ms",
        "总延时ms",
        "时延抖动ms",
        "RSSI均值",
        "最弱RSSI",
        "最短路径链路质量",
    ]]
    for key, item in sorted(display_items.items()):
        route = item.get("last_route", [])
        selected_quality = []
        for src, dst in zip(route[:-1], route[1:]):
            edge = topology.edges.get(f"{int(src):02X}:{int(dst):02X}")
            if edge is not None:
                selected_quality.append(f"{format_addr(src)}->{format_addr(dst)} {link_quality(edge.rssi)} {edge.rssi}dBm w{edge.weight}")
        path_rows.append([
            item.get("destination", key),
            item.get("route_hops"),
            item.get("loss_rate"),
            item.get("avg_single_hop_latency_ms"),
            item.get("algorithm_compute_latency_ms"),
            item.get("inference_ms"),
            item.get("source_to_target_ms"),
            item.get("total_latency_ms"),
            item.get("latency_jitter", {}).get("jitter_ms"),
            item.get("path_rssi", {}).get("mean_rssi"),
            item.get("path_rssi", {}).get("min_rssi"),
            "; ".join(selected_quality),
        ])

    rssi_rows = [["src", "dst", "rssi", "weight", "direction", "source", "updated_at"]]
    for edge in sorted(topology.edges.values(), key=lambda item: (item.src, item.dst)):
        data = asdict(edge)
        rssi_rows.append([
            format_addr(edge.src),
            format_addr(edge.dst),
            edge.rssi,
            edge.weight,
            data.get("direction"),
            data.get("source"),
            data.get("updated_at"),
        ])

    config_rows = [["参数", "值"]]
    for key, value in sorted(config.items()):
        if isinstance(value, (dict, list)):
            value = str(value)
        config_rows.append([key, value])

    optimization_rows = build_optimization_rows(summary)

    write_xlsx(path, [
        ("测试结果", result_rows),
        ("完整指标汇总", full_metric_rows),
        ("核心指标", metric_rows),
        ("路径对比", path_rows),
        ("优化参数对比", optimization_rows),
        ("RSSI拓扑", rssi_rows),
        ("测试参数", config_rows),
    ])


def build_optimization_rows(summary: dict) -> list[list[object]]:
    config = summary.get("config", {})
    total = summary.get("total", {})
    latency = total.get("latency", {})
    target = config.get("optimization_target") or {"loss_rate_min": 0.04, "loss_rate_max": 0.06, "ack_timeout": 2.0}
    low = target.get("loss_rate_min", 0.04)
    high = target.get("loss_rate_max", 0.06)
    loss_rate = total.get("loss_rate")
    in_target = loss_rate is not None and config.get("ack_timeout") == 2.0 and low <= loss_rate <= high
    return [
        ["算法模式", "RSSI请求次数", "发包间隔s", "ACK timeout s", "总发送", "成功", "丢包率", "平均延时ms", "P95ms", "目标范围", "是否达标"],
        [
            config.get("route_mode", "baseline_dijkstra"),
            config.get("rssi_requests"),
            config.get("interval"),
            config.get("ack_timeout"),
            total.get("sent"),
            total.get("success"),
            loss_rate,
            latency.get("avg_ms"),
            latency.get("p95_ms"),
            f"{low:.2%}~{high:.2%}",
            "是" if in_target else "否",
        ],
    ]


def build_full_metric_rows(summary: dict) -> list[list[object]]:
    metrics = summary.get("metrics", {})
    rssi = metrics.get("rssi_fluctuation", {})
    jitter = metrics.get("latency_jitter", {})
    rows: list[list[object]] = [[
        "出发点",
        "目标点",
        "路径",
        "算法计算延时ms",
        "指令下发延时ms",
        "端到端实际传输平均延时ms",
        "节点丢包率",
        "全局平均丢包率",
        "路由跳数",
        "单路径平均跳数",
        "平均单跳传输耗时ms",
        "RSSI最小值dBm",
        "RSSI最大值dBm",
        "RSSI波动范围dB",
        "RSSI标准差dB",
        "节点时延抖动ms",
        "全局时延抖动ms",
    ]]
    for key, item in sorted(_summary_items(summary).items()):
        latency = item.get("latency", {})
        rows.append([
            item.get("source", "00"),
            item.get("destination", key),
            item.get("route_path") or path_text(item.get("last_route", [])),
            item.get("algorithm_compute_latency_ms", metrics.get("algorithm_compute_latency_ms")),
            item.get("command_downlink_latency_ms", latency.get("avg_ms")),
            item.get("end_to_end_avg_latency_ms", latency.get("avg_ms")),
            item.get("loss_rate"),
            metrics.get("global_avg_loss_rate"),
            item.get("route_hops"),
            metrics.get("avg_route_hops"),
            item.get("avg_single_hop_latency_ms"),
            rssi.get("min"),
            rssi.get("max"),
            rssi.get("range"),
            rssi.get("stddev"),
            item.get("latency_jitter", {}).get("jitter_ms"),
            jitter.get("jitter_ms"),
        ])
    return rows


def _rate(value: float | None) -> str:
    return "n/a" if value is None else f"{value:.2%}"
