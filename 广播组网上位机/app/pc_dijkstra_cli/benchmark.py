from __future__ import annotations

import json
import heapq
import statistics
import time
from dataclasses import asdict, dataclass
from datetime import datetime
from pathlib import Path
from typing import Iterable

from .defaults import DEFAULTS
from .protocol import Ack, RssiReport, build_send_command, format_addr, parse_addr
from .reporting import enrich_summary_with_metrics, path_text, route_hops, write_excel_summary, write_text_topology
from .serial_client import RawSerialEvent, SerialClient
from .topology import Topology, routes_to_dict, save_topology
from .topology_svg import write_topology_svg
from .routing import BASELINE_ROUTE_MODE, RELIABLE_ROUTE_MODE, Route


@dataclass
class RoundResult:
    target: int
    round_index: int
    route: list[int]
    cost: float
    command: str
    success: bool
    latency_ms: float | None
    ack_seq: int | None
    payload: str = ""
    demand: int | None = None
    send_ts: str | None = None
    ack_ts: str | None = None
    interval_s: float | None = None
    status: str = "unknown"
    error: str | None = None
    route_mode: str = BASELINE_ROUTE_MODE
    route_fallback: bool = False
    source: int = 0x00
    gateway_to_source_ms: float | None = None
    gateway_to_target_ms: float | None = None
    point_to_point_ms: float | None = None
    total_latency_ms: float | None = None
    inference_ms: float | None = None
    source_to_target_ms: float | None = None
    topology_recollected: bool = False
    ack1_monotonic: float | None = None
    send2_monotonic: float | None = None


class BenchmarkLogger:
    def __init__(self, log_dir: str | Path):
        self.log_dir = Path(log_dir)
        self.log_dir.mkdir(parents=True, exist_ok=True)
        self.json_dir = self.log_dir / "原始JSON数据"
        self.json_dir.mkdir(parents=True, exist_ok=True)
        self.raw_file = (self.log_dir / "原始串口日志.log").open("w", encoding="utf-8")
        self.events_file = (self.json_dir / "events.jsonl").open("w", encoding="utf-8")
        self.rounds_file = (self.json_dir / "rounds.jsonl").open("w", encoding="utf-8")
        self.topology_file = (self.json_dir / "topology_snapshots.jsonl").open("w", encoding="utf-8")

    def close(self) -> None:
        self.raw_file.close()
        self.events_file.close()
        self.rounds_file.close()
        self.topology_file.close()

    def raw_callback(self, event: RawSerialEvent) -> None:
        text = "".join(chr(b) if b in (9, 10, 13) or 32 <= b <= 126 else "." for b in event.data)
        self.raw_file.write(
            f"{datetime.fromtimestamp(event.timestamp).isoformat(timespec='milliseconds')} "
            f"{event.direction.upper()} len={len(event.data)} hex={event.data.hex(' ')} text={text!r}\n"
        )
        self.raw_file.flush()

    def event(self, event_type: str, **payload) -> None:
        record = {
            "ts": datetime.now().isoformat(timespec="milliseconds"),
            "type": event_type,
            **payload,
        }
        self.events_file.write(json.dumps(record, ensure_ascii=False, sort_keys=True) + "\n")
        self.events_file.flush()

    def round(self, result: RoundResult) -> None:
        self.rounds_file.write(json.dumps(asdict(result), ensure_ascii=False, sort_keys=True) + "\n")
        self.rounds_file.flush()

    def topology_snapshot(self, topology: Topology, label: str) -> None:
        record = {
            "ts": datetime.now().isoformat(timespec="milliseconds"),
            "label": label,
            "topology": topology.to_dict(),
        }
        self.topology_file.write(json.dumps(record, ensure_ascii=False, sort_keys=True) + "\n")
        self.topology_file.flush()


def parse_node_list(value: str) -> list[int]:
    nodes = [parse_addr(item) for item in value.split(",") if item.strip()]
    if not nodes:
        raise ValueError("nodes must not be empty")
    return nodes


def parse_optional_node_list(value: str | None) -> list[int] | None:
    if value is None or not str(value).strip():
        return None
    return parse_node_list(value)


def source_target_pairs(nodes: list[int], sources: list[int] | None = None) -> list[tuple[int, int]]:
    node_set = set(nodes)
    source_list = sources if sources is not None else nodes
    missing = [node for node in source_list if node not in node_set]
    if missing:
        raise ValueError(f"sources must be included in nodes: {', '.join(format_addr(node) for node in missing)}")
    return [(source, target) for source in source_list for target in nodes if target != source]


def parse_float_list(value: str) -> list[float]:
    values = [float(item) for item in value.split(",") if item.strip()]
    if not values:
        raise ValueError("intervals must not be empty")
    return values


def parse_int_list(value: str) -> list[int]:
    values = [int(item) for item in value.split(",") if item.strip()]
    if not values:
        raise ValueError("integer list must not be empty")
    return values


def parse_route_mode_list(value: str) -> list[str]:
    values = [item.strip() for item in value.split(",") if item.strip()]
    if not values:
        raise ValueError("route modes must not be empty")
    unsupported = [item for item in values if item not in {BASELINE_ROUTE_MODE, RELIABLE_ROUTE_MODE}]
    if unsupported:
        raise ValueError(f"unsupported route mode(s): {', '.join(unsupported)}")
    return values


def demand_for_payload(payload: str, demands: tuple[int, ...] = DEFAULTS.demands) -> int:
    payload_bytes = max(1, len("".join(payload.split())) // 2)
    for demand in sorted(demands):
        if payload_bytes <= demand:
            return int(demand)
    return int(max(demands))


def _drain_messages(client: SerialClient, logger: BenchmarkLogger, duration: float) -> list[object]:
    deadline = time.monotonic() + duration
    messages = []
    while time.monotonic() < deadline:
        for message in client.read_available():
            messages.append(message)
            if isinstance(message, RssiReport):
                logger.event(
                    "rssi_report",
                    src=message.src_addr,
                    neighbors=[asdict(neighbor) for neighbor in message.neighbors],
                )
            elif isinstance(message, Ack):
                logger.event("ack_observed", src=message.src_addr, seq=message.seq)
    return messages


def _drain_messages_until_quiet(
    client: SerialClient,
    logger: BenchmarkLogger,
    idle_timeout: float = 1.0,
    max_seconds: float = 10.0,
) -> list[object]:
    deadline = time.monotonic() + max_seconds
    idle_deadline = time.monotonic() + idle_timeout
    messages = []
    while time.monotonic() < deadline and time.monotonic() < idle_deadline:
        chunk_messages = client.read_available()
        if chunk_messages:
            idle_deadline = time.monotonic() + idle_timeout
        for message in chunk_messages:
            messages.append(message)
            if isinstance(message, RssiReport):
                logger.event(
                    "rssi_report",
                    src=message.src_addr,
                    neighbors=[asdict(neighbor) for neighbor in message.neighbors],
                )
            elif isinstance(message, Ack):
                logger.event("ack_observed", src=message.src_addr, seq=message.seq)
    return messages


def collect_topology(
    client: SerialClient,
    logger: BenchmarkLogger,
    collect_seconds: float,
    requests: int,
    nodes: list[int],
    gateway: int,
    dongle_addr: int | None = None,
    min_rssi: int = -85,
    max_hops: int = 4,
) -> Topology:
    relay_excluded = {dongle_addr} if dongle_addr is not None else set()
    topology = Topology(stale_seconds=None, edge_direction="src_to_neighbor", gateway=gateway, relay_excluded=relay_excluded, min_rssi=min_rssi, max_hops=max_hops)
    for index in range(1, requests + 1):
        command = client.send_rssi_req()
        logger.event("send_command", command=command.rstrip(), request=index)
        for message in _drain_messages_until_quiet(client, logger, idle_timeout=1.0, max_seconds=10.0):
            if isinstance(message, RssiReport):
                updated = topology.update_from_rssi_report(message)
                logger.event(
                    "topology_update",
                    src=message.src_addr,
                    edges=[asdict(edge) for edge in updated],
                )
                logger.topology_snapshot(topology, f"rssi_req_{index}")
        if all(topology.route(gateway, node).status == "valid" for node in nodes):
            logger.event("topology_ready", request=index)
            break
    logger.topology_snapshot(topology, "post_collection")
    return topology


def _combine_gateway_path(gateway_route: list[int], pair_route: list[int]) -> list[int]:
    if not gateway_route or not pair_route:
        return []
    if gateway_route[-1] != pair_route[0]:
        return []
    # A2: 去重防环 - 将网关路径和配对路径合并，去除重复节点
    combined = list(gateway_route)
    for node in pair_route[1:]:
        # 如果节点已在路径中，截断到该节点之前
        if node in combined:
            idx = combined.index(node)
            combined = combined[:idx]
        combined.append(node)
    return combined


def _send_and_wait_ack_with_retry(
    client: SerialClient,
    logger: BenchmarkLogger,
    topology: Topology,
    *,
    dst: int,
    path: list[int],
    payload: str,
    ack_timeout: float,
    event_payload: dict,
    max_retries: int = 2,
    retry_interval: float = 0.5,
) -> tuple[bool, float | None, int | None, str | None, str, int, float | None]:
    """B1+B2: ACK 超时自动重传"""
    for attempt in range(max_retries + 1):
        success, latency_ms, ack_seq, ack_ts, send_ts, ack_monotonic = _send_and_wait_ack(
            client, logger, topology,
            dst=dst, path=path, payload=payload,
            ack_timeout=ack_timeout, event_payload=event_payload,
        )
        if success:
            return success, latency_ms, ack_seq, ack_ts, send_ts, attempt, ack_monotonic
        
        # 还有重试机会
        if attempt < max_retries:
            logger.event("retry", attempt=attempt+1, max_retries=max_retries, dst=dst, **event_payload)
            time.sleep(retry_interval)
    
    # 所有重试都失败
    return False, None, None, None, send_ts, max_retries, None


def _send_and_wait_ack(
    client: SerialClient,
    logger: BenchmarkLogger,
    topology: Topology,
    *,
    dst: int,
    path: list[int],
    payload: str,
    ack_timeout: float,
    event_payload: dict,
) -> tuple[bool, float | None, int | None, str | None, str, float | None]:
    command = build_send_command(dst, path, payload)
    send_ts = datetime.now().isoformat(timespec="milliseconds")
    logger.event("round_send", command=command.rstrip(), **event_payload)
    started = time.monotonic()
    client.write_command(command)
    success = False
    latency_ms = None
    ack_seq = None
    ack_ts = None
    ack_monotonic = None
    deadline = started + ack_timeout
    while time.monotonic() < deadline:
        for message in client.read_available():
            if isinstance(message, RssiReport):
                updated = topology.update_from_rssi_report(message)
                logger.event("topology_update", src=message.src_addr, edges=[asdict(edge) for edge in updated])
                logger.topology_snapshot(topology, f"round_{event_payload.get('source'):02X}_{dst:02X}_{event_payload.get('round')}")
            elif isinstance(message, Ack):
                logger.event("ack_observed", src=message.src_addr, seq=message.seq)
                if message.src_addr == dst:
                    success = True
                    ack_monotonic = time.monotonic()
                    latency_ms = (ack_monotonic - started) * 1000.0
                    ack_seq = message.seq
                    ack_ts = datetime.now().isoformat(timespec="milliseconds")
                    logger.event("round_ack", ack_target=dst, seq=ack_seq, latency_ms=latency_ms, **event_payload)
                    break
        if success:
            break
    if not success:
        logger.event("round_timeout", ack_target=dst, timeout_s=ack_timeout, **event_payload)
    return success, latency_ms, ack_seq, ack_ts, send_ts, ack_monotonic


def _latency_stats(values: list[float]) -> dict:
    if not values:
        return {
            "avg_ms": None,
            "min_ms": None,
            "max_ms": None,
            "p95_ms": None,
        }
    sorted_values = sorted(values)
    p95_index = min(len(sorted_values) - 1, max(0, int(0.95 * len(sorted_values) + 0.999999) - 1))
    return {
        "avg_ms": statistics.fmean(values),
        "min_ms": min(values),
        "max_ms": max(values),
        "p95_ms": sorted_values[p95_index],
    }


def _result_latency_ms(result: RoundResult) -> float | None:
    return result.point_to_point_ms if result.point_to_point_ms is not None else result.latency_ms


def _counts_as_transmission(result: RoundResult) -> bool:
    return result.status != "unreachable"


def _is_ack_timeout(result: RoundResult) -> bool:
    return _counts_as_transmission(result) and not result.success


def summarize(results: Iterable[RoundResult], nodes: list[int], sources: list[int] | None = None) -> dict:
    result_list = list(results)
    pairs = {}
    pair_keys = sorted({(result.source, result.target) for result in result_list} | set(source_target_pairs(nodes, sources)))
    for source, target in pair_keys:
        pair_results = [result for result in result_list if result.source == source and result.target == target]
        sent_results = [result for result in pair_results if _counts_as_transmission(result)]
        success_results = [result for result in pair_results if result.success and _result_latency_ms(result) is not None]
        latencies = [float(_result_latency_ms(result)) for result in success_results if _result_latency_ms(result) is not None]
        sent = len(sent_results)
        success = len(success_results)
        last = pair_results[-1] if pair_results else None
        pairs[f"{source:02X}:{target:02X}"] = {
            "source": f"{source:02X}",
            "destination": f"{target:02X}",
            "sent": sent,
            "success": success,
            "lost": sent - success,
            "loss_rate": (sent - success) / sent if sent else None,
            "planned_rounds": len(pair_results),
            "route_failed": len([result for result in pair_results if result.status == "unreachable"]),
            "ack_timeout_loss": len([result for result in pair_results if _is_ack_timeout(result)]),
            "route_failed_rate": len([result for result in pair_results if result.status == "unreachable"]) / len(pair_results) if pair_results else None,
            "latency": _latency_stats(latencies),
            "inference_latency": _latency_stats([float(result.inference_ms) for result in pair_results if result.inference_ms is not None]),
            "source_to_target_latency": _latency_stats([float(result.source_to_target_ms) for result in success_results if result.source_to_target_ms is not None]),
            "last_route": last.route if last else [],
            "last_cost": last.cost if last else None,
            "gateway_to_source_ms": last.gateway_to_source_ms if last else None,
            "gateway_to_target_ms": last.gateway_to_target_ms if last else None,
            "point_to_point_ms": last.point_to_point_ms if last else None,
            "total_latency_ms": last.total_latency_ms if last else None,
            "inference_ms": last.inference_ms if last else None,
            "source_to_target_ms": last.source_to_target_ms if last else None,
            "route_mode": last.route_mode if last else BASELINE_ROUTE_MODE,
            "route_fallback": last.route_fallback if last else False,
            "recollect_count": len([result for result in pair_results if result.topology_recollected]),
            "path_rssi": {
                "route": last.route if last else [],
                "mean_rssi": None,
                "min_rssi": None,
                "source": "unavailable",
            },
        }
    targets = {}
    for node in nodes:
        node_results = [result for result in result_list if result.target == node]
        sent_results = [result for result in node_results if _counts_as_transmission(result)]
        success_results = [result for result in node_results if result.success and _result_latency_ms(result) is not None]
        latencies = [float(_result_latency_ms(result)) for result in success_results if _result_latency_ms(result) is not None]
        sent = len(sent_results)
        success = len(success_results)
        targets[f"{node:02X}"] = {
            "source": "00",
            "destination": f"{node:02X}",
            "sent": sent,
            "success": success,
            "lost": sent - success,
            "loss_rate": (sent - success) / sent if sent else None,
            "planned_rounds": len(node_results),
            "route_failed": len([result for result in node_results if result.status == "unreachable"]),
            "ack_timeout_loss": len([result for result in node_results if _is_ack_timeout(result)]),
            "route_failed_rate": len([result for result in node_results if result.status == "unreachable"]) / len(node_results) if node_results else None,
            "latency": _latency_stats(latencies),
            "inference_latency": _latency_stats([float(result.inference_ms) for result in node_results if result.inference_ms is not None]),
            "source_to_target_latency": _latency_stats([float(result.source_to_target_ms) for result in success_results if result.source_to_target_ms is not None]),
            "last_route": node_results[-1].route if node_results else [],
            "last_cost": node_results[-1].cost if node_results else None,
            "gateway_to_source_ms": node_results[-1].gateway_to_source_ms if node_results else None,
            "gateway_to_target_ms": node_results[-1].gateway_to_target_ms if node_results else None,
            "point_to_point_ms": node_results[-1].point_to_point_ms if node_results else None,
            "total_latency_ms": node_results[-1].total_latency_ms if node_results else None,
            "inference_ms": node_results[-1].inference_ms if node_results else None,
            "source_to_target_ms": node_results[-1].source_to_target_ms if node_results else None,
            "route_mode": node_results[-1].route_mode if node_results else BASELINE_ROUTE_MODE,
            "route_fallback": node_results[-1].route_fallback if node_results else False,
            "path_rssi": {
                "route": node_results[-1].route if node_results else [],
                "mean_rssi": None,
                "min_rssi": None,
                "source": "unavailable",
            },
        }
    total_sent = len([result for result in result_list if _counts_as_transmission(result)])
    total_success = len([result for result in result_list if result.success])
    all_latencies = [float(_result_latency_ms(result)) for result in result_list if result.success and _result_latency_ms(result) is not None]
    all_inference = [float(result.inference_ms) for result in result_list if result.inference_ms is not None]
    all_source_to_target = [float(result.source_to_target_ms) for result in result_list if result.success and result.source_to_target_ms is not None]
    return {
        "total": {
            "sent": total_sent,
            "success": total_success,
            "lost": total_sent - total_success,
            "loss_rate": (total_sent - total_success) / total_sent if total_sent else None,
            "latency": _latency_stats(all_latencies),
            "inference_latency": _latency_stats(all_inference),
            "source_to_target_latency": _latency_stats(all_source_to_target),
            "planned_rounds": len(result_list),
            "route_failed": len([result for result in result_list if result.status == "unreachable"]),
            "ack_timeout_loss": len([result for result in result_list if _is_ack_timeout(result)]),
            "route_failed_rate": len([result for result in result_list if result.status == "unreachable"]) / len(result_list) if result_list else None,
        },
        "targets": targets,
        "pairs": pairs,
        "topology_recollect_count": len([result for result in result_list if result.topology_recollected]),
    }


def enrich_summary_with_rssi(summary: dict, topology: Topology) -> None:
    for collection in (summary.get("targets", {}), summary.get("pairs", {})):
        for item in collection.values():
            route = item.get("last_route", [])
            values = []
            for src, dst in zip(route[:-1], route[1:]):
                edge = topology.edges.get(f"{src:02X}:{dst:02X}")
                if edge is not None:
                    values.append(edge.rssi)
            item["path_rssi"] = {
                "route": route,
                "mean_rssi": statistics.fmean(values) if values else None,
                "min_rssi": min(values) if values else None,
                "source": "real_rssi" if values else "unavailable",
            }


def _write_routes_json(log_path: Path, topology: Topology, route_mode: str) -> float:
    json_path = _json_dir(log_path)
    route_compute_start = time.perf_counter()
    routes = routes_to_dict(topology.routes(route_mode=route_mode))
    route_compute_ms = (time.perf_counter() - route_compute_start) * 1000.0
    (json_path / "routes.json").write_text(json.dumps(routes, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return route_compute_ms


def _record_unreachable_rounds(
    *,
    logger: BenchmarkLogger,
    results: list[RoundResult],
    source: int,
    target: int,
    round_index: int,
    payload: str,
    demand: int,
    interval: float,
    route_mode: str,
    error: str,
    topology_recollected: bool,
) -> None:
    result = RoundResult(
        target=target,
        round_index=round_index,
        route=[],
        cost=float("inf"),
        command="",
        success=False,
        latency_ms=None,
        ack_seq=None,
        payload=payload,
        demand=demand,
        interval_s=interval,
        status="unreachable",
        error=error,
        route_mode=route_mode,
        source=source,
        topology_recollected=topology_recollected,
    )
    results.append(result)
    logger.round(result)


def _should_recollect(consecutive_failures: dict[tuple[int, int], int], source: int, target: int, threshold: int) -> bool:
    return threshold > 0 and consecutive_failures.get((source, target), 0) >= threshold


def _reset_recollect_counter(consecutive_failures: dict[tuple[int, int], int], source: int, target: int) -> None:
    consecutive_failures[(source, target)] = 0


def _mark_pair_result(consecutive_failures: dict[tuple[int, int], int], source: int, target: int, success: bool) -> None:
    key = (source, target)
    consecutive_failures[key] = 0 if success else consecutive_failures.get(key, 0) + 1


def _path_key(path: list[int]) -> tuple[int, ...]:
    return tuple(int(item) for item in path)


def _candidate_paths(graph: dict[int, dict[int, float]], src: int, dst: int, limit: int = 5, max_hops: int = 4) -> list[list[int]]:
    if src == dst:
        return [[src]]
    queue: list[tuple[float, tuple[int, ...]]] = [(0.0, (src,))]
    paths: list[list[int]] = []
    seen: set[tuple[int, ...]] = set()
    while queue and len(paths) < limit:
        cost, path_tuple = heapq.heappop(queue)
        if path_tuple in seen:
            continue
        seen.add(path_tuple)
        current = path_tuple[-1]
        if current == dst:
            paths.append(list(path_tuple))
            continue
        if len(path_tuple) - 1 >= max_hops:
            continue
        for neighbor, weight in sorted(graph.get(current, {}).items()):
            if neighbor in path_tuple:
                continue
            heapq.heappush(queue, (cost + float(weight), path_tuple + (neighbor,)))
    return paths


def _path_base_cost(graph: dict[int, dict[int, float]], path: list[int]) -> float:
    cost = 0.0
    for src, dst in zip(path[:-1], path[1:]):
        cost += float(graph.get(src, {}).get(dst, 9999.0))
    return cost


def _history_penalty(
    records: list[dict],
    *,
    loss_threshold: float,
    avg_latency_threshold_ms: float,
    window: int,
) -> tuple[float, str | None]:
    recent = records[-window:]
    if not recent:
        return 0.0, None
    loss_rate = len([item for item in recent if not item.get("success")]) / len(recent)
    latencies = [float(item["point_to_point_ms"]) for item in recent if item.get("success") and item.get("point_to_point_ms") is not None]
    avg_latency = statistics.fmean(latencies) if latencies else None
    penalty = 0.0
    reasons = []
    if loss_rate >= loss_threshold:
        penalty += 1000.0 + 500.0 * loss_rate
        reasons.append(f"loss_rate>={loss_threshold:.0%}")
    if avg_latency is not None and avg_latency >= avg_latency_threshold_ms:
        penalty += 500.0 + avg_latency
        reasons.append(f"avg_latency>={avg_latency_threshold_ms:g}ms")
    return penalty, ",".join(reasons) if reasons else None


def _select_dijkstra_pair_route(
    topology: Topology,
    source: int,
    target: int,
    route_mode: str,
    path_history: dict[tuple[int, int, tuple[int, ...]], list[dict]],
    *,
    loss_threshold: float,
    avg_latency_threshold_ms: float,
    window: int,
    avoid_nodes: set[int] | None = None,
    candidate_limit: int = 5,
    max_hops: int = 4,
):
    graph = topology.graph(route_mode=route_mode)
    fallback_used = False
    paths = _candidate_paths(graph, source, target, candidate_limit, max_hops=max_hops)
    if not paths and route_mode != BASELINE_ROUTE_MODE:
        graph = topology.graph(route_mode=BASELINE_ROUTE_MODE)
        paths = _candidate_paths(graph, source, target, candidate_limit, max_hops=max_hops)
        fallback_used = bool(paths)
    if not paths:
        return topology.route(source, target, route_mode=route_mode, fallback=False), fallback_used, None
    scored = []
    for path in paths:
        # A4: 跳过包含环路的路径
        if len(path) != len(set(path)):
            continue
        
        history_key = (source, target, _path_key(path))
        penalty, reason = _history_penalty(
            path_history.get(history_key, []),
            loss_threshold=loss_threshold,
            avg_latency_threshold_ms=avg_latency_threshold_ms,
            window=window,
        )
        # A1: 移除 avoid_nodes 惩罚
        # A3: 增加跳数惩罚权重 (0.25 -> 2.0)
        hop_penalty = max(0, len(path) - 2) * 2.0
        
        # 计算路径中弱 RSSI 链路的惩罚
        weak_rssi_penalty = 0.0
        min_rssi_in_path = 0
        for src, dst in zip(path[:-1], path[1:]):
            edge = topology.edges.get(f"{src:02X}:{dst:02X}")
            if edge is not None:
                if edge.rssi < -80:
                    weak_rssi_penalty += 10.0  # 强惩罚弱链路
                elif edge.rssi < -75:
                    weak_rssi_penalty += 5.0   # 中等惩罚
                if min_rssi_in_path == 0 or edge.rssi < min_rssi_in_path:
                    min_rssi_in_path = edge.rssi
        
        total_cost = _path_base_cost(graph, path) + penalty + hop_penalty + weak_rssi_penalty
        scored.append((total_cost, path, reason, min_rssi_in_path))
    
    if not scored:
        return Route(source, target, [], float("inf"), "unreachable"), fallback_used, "no_valid_path"
    
    # 按总成本排序，优先选择成本低、跳数少、RSSI 强的路径
    scored.sort(key=lambda item: (item[0], len(item[1]), item[1]))
    cost, path, reason, min_rssi = scored[0]
    return Route(source, target, path, cost, "valid"), fallback_used, reason


def _recollect_topology(
    *,
    client: SerialClient,
    logger: BenchmarkLogger,
    log_path: Path,
    rssi_seconds: float,
    rssi_requests: int,
    nodes: list[int],
    gateway: int,
    dongle_addr: int | None = None,
    min_rssi: int = -85,
    max_hops: int = 4,
    route_mode: str,
    reason: str,
    old_topology: Topology | None = None,
) -> tuple[Topology, float, float]:
    logger.event("topology_recollect_start", reason=reason)
    started = time.perf_counter()
    new_topology = collect_topology(client, logger, rssi_seconds, rssi_requests, nodes, gateway, dongle_addr=dongle_addr, min_rssi=min_rssi, max_hops=max_hops)
    
    # 拓扑稳定性检查：如果新拓扑质量下降，保留旧拓扑
    if old_topology is not None:
        old_edges = len(old_topology.edges)
        new_edges = len(new_topology.edges)
        old_reachable = sum(1 for node in nodes if old_topology.route(gateway, node).status == "valid")
        new_reachable = sum(1 for node in nodes if new_topology.route(gateway, node).status == "valid")
        
        # 如果新拓扑边数减少超过30%或可达节点减少，保留旧拓扑
        if new_edges < old_edges * 0.7 or new_reachable < old_reachable:
            logger.event(
                "topology_recollect_rejected",
                reason="quality_degradation",
                old_edges=old_edges,
                new_edges=new_edges,
                old_reachable=old_reachable,
                new_reachable=new_reachable,
            )
            topology = old_topology
        else:
            topology = new_topology
    else:
        topology = new_topology
    
    save_topology(_json_dir(log_path) / "state.json", topology)
    route_compute_ms = _write_routes_json(log_path, topology, route_mode)
    elapsed_ms = (time.perf_counter() - started) * 1000.0
    logger.event("topology_recollect_complete", reason=reason, topology_recollect_ms=elapsed_ms, algorithm_compute_latency_ms=route_compute_ms)
    return topology, elapsed_ms, route_compute_ms


def _resolve_matrix_routes(topology: Topology, gateway: int, source: int, target: int, route_mode: str):
    gateway_route = topology.route(gateway, source, route_mode=route_mode, fallback=False)
    pair_route = topology.route(source, target, route_mode=route_mode, fallback=False)
    route_fallback = False
    if pair_route.status != "valid" and route_mode != BASELINE_ROUTE_MODE:
        pair_route = topology.route(source, target, route_mode=BASELINE_ROUTE_MODE, fallback=False)
        route_fallback = pair_route.status == "valid"
    full_path = _combine_gateway_path(gateway_route.path, pair_route.path)
    return gateway_route, pair_route, full_path, route_fallback


def _resolve_healthy_matrix_routes(
    topology: Topology,
    gateway: int,
    source: int,
    target: int,
    route_mode: str,
    path_history: dict[tuple[int, int, tuple[int, ...]], list[dict]],
    *,
    loss_threshold: float,
    avg_latency_threshold_ms: float,
    window: int,
    max_hops: int = 4,
):
    gateway_route = topology.route(gateway, source, route_mode=route_mode, fallback=False)
    avoid_nodes = set(gateway_route.path[:-1]) - {target}
    
    # 尝试从网关开始搜索到目标的直接路径
    gateway_to_target_route = topology.route(gateway, target, route_mode=route_mode, fallback=False)
    
    # 如果网关到目标的路径存在且包含源节点，直接使用
    if gateway_to_target_route.status == "valid" and source in gateway_to_target_route.path:
        pair_route = gateway_to_target_route
        full_path = gateway_to_target_route.path
        route_fallback = False
        health_reason = None
    else:
        # 否则，从源节点开始搜索到目标的路径
        pair_route, route_fallback, health_reason = _select_dijkstra_pair_route(
            topology,
            source,
            target,
            route_mode,
            path_history,
            loss_threshold=loss_threshold,
            avg_latency_threshold_ms=avg_latency_threshold_ms,
            window=window,
            avoid_nodes=avoid_nodes,
            max_hops=max_hops,
        )
        full_path = _combine_gateway_path(gateway_route.path, pair_route.path)
    return gateway_route, pair_route, full_path, route_fallback, health_reason


def write_report(log_dir: Path, summary: dict, topology: Topology) -> None:
    routes = routes_to_dict(topology.routes())
    metrics = summary.get("metrics", {})
    lines = [
        "# Dijkstra Hardware Benchmark Report",
        "",
        f"- Generated: {datetime.now().isoformat(timespec='seconds')}",
        f"- Total sent: {summary['total']['sent']}",
        f"- Total success: {summary['total']['success']}",
        f"- Planned rounds: {summary['total'].get('planned_rounds')}",
        f"- Route failed/unreachable: {summary['total'].get('route_failed')}",
        f"- Total loss rate: {summary['total']['loss_rate']:.2%}" if summary["total"]["loss_rate"] is not None else "- Total loss rate: n/a",
        f"- Inference time avg: {_fmt_ms(metrics.get('inference_latency', {}).get('avg_ms'))}",
        f"- Source-to-target latency avg: {_fmt_ms(metrics.get('source_to_target_latency', {}).get('avg_ms'))}",
        "",
        "## Routes",
    ]
    for key, route in sorted(routes.items()):
        path = " -> ".join(f"{addr:02X}" for addr in route["path"])
        lines.append(f"- {key}: cost={route['cost']} path={path}")
    lines.extend(["", "## Source-Target Pairs"])
    for node, item in sorted(_summary_items(summary).items()):
        latency = item["latency"]
        loss_rate = item["loss_rate"]
        inference = item.get("inference_latency", {})
        source_to_target = item.get("source_to_target_latency", {})
        lines.append(
            f"- {item.get('source', '00')}->{item.get('destination', node)}: sent={item['sent']} success={item['success']} "
            f"ack_timeout={item.get('ack_timeout_loss')} route_failed={item.get('route_failed')} loss={_fmt_rate(loss_rate)} avg={_fmt_ms(latency['avg_ms'])} "
            f"min={_fmt_ms(latency['min_ms'])} max={_fmt_ms(latency['max_ms'])} p95={_fmt_ms(latency['p95_ms'])} "
            f"inference={_fmt_ms(inference.get('avg_ms'))} source_to_target={_fmt_ms(source_to_target.get('avg_ms'))} total={_fmt_ms(item.get('total_latency_ms'))} "
            f"route={' -> '.join(f'{addr:02X}' for addr in item['last_route'])} "
            f"min_rssi={item.get('path_rssi', {}).get('min_rssi')}"
        )
    lines.extend([
        "",
        "## Files",
        "- 原始串口日志.log",
        "- 原始JSON数据/events.jsonl",
        "- 原始JSON数据/topology_snapshots.jsonl",
        "- 原始JSON数据/routes.json",
        "- 原始JSON数据/rounds.jsonl",
        "- 原始JSON数据/summary.json",
        "- 原始JSON数据/state.json",
        "- 拓扑图.svg",
        "- 拓扑图.txt",
        "- 测试指标汇总.xlsx",
    ])
    (log_dir / "report.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def _fmt_ms(value: float | None) -> str:
    return "n/a" if value is None else f"{value:.1f}ms"


def _fmt_seconds(value: float | None) -> str:
    return "n/a" if value is None else f"{value:.4f}s"


def _fmt_rate(value: float | None) -> str:
    return "n/a" if value is None else f"{value:.2%}"


def _fmt_number(value) -> str:
    if value is None:
        return "n/a"
    if isinstance(value, float):
        return f"{value:.4f}".rstrip("0").rstrip(".")
    return str(value)


def _fmt_metric_value(field: dict) -> str:
    value = field.get("value")
    unit = field.get("unit")
    if unit == "ratio":
        return _fmt_rate(value)
    if unit in {"seconds", "seconds_per_hop"}:
        return _fmt_seconds(value)
    return _fmt_number(value)


def _fmt_path(route: list[int]) -> str:
    return " -> ".join(format_addr(addr) for addr in route)


def _summary_items(summary: dict) -> dict:
    return summary.get("pairs") or summary.get("targets", {})


def _target_status(loss_rate: float | None, ack_timeout: float | None, low: float = 0.04, high: float = 0.06) -> str:
    if ack_timeout != 2.0:
        return "否，非 2.0s 真实口径"
    if loss_rate is None:
        return "否，暂无数据"
    return "是" if low <= loss_rate <= high else "否"


def _new_target_status(summary: dict) -> str:
    total = summary.get("total", {})
    loss_rate = total.get("loss_rate")
    avg_ms = total.get("latency", {}).get("avg_ms")
    if loss_rate is None or avg_ms is None:
        return "否，暂无有效 SEND 数据"
    return "是" if loss_rate < 0.10 and avg_ms < 220.0 else "否"


def _json_dir(log_dir: Path) -> Path:
    path = log_dir / "原始JSON数据"
    path.mkdir(parents=True, exist_ok=True)
    return path


def next_run_dir(root: str | Path, prefix: str = "第") -> Path:
    root_path = Path(root)
    root_path.mkdir(parents=True, exist_ok=True)
    used = []
    for item in root_path.iterdir():
        if not item.is_dir():
            continue
        name = item.name
        if name.startswith(prefix) and name.endswith("次测试"):
            middle = name[len(prefix) : -len("次测试")]
            if middle.isdigit():
                used.append(int(middle))
    return root_path / f"{prefix}{(max(used) if used else 0) + 1}次测试"


def resolve_benchmark_log_dir(log_dir: str | Path) -> Path:
    path = Path(log_dir)
    return next_run_dir(path) if path.name == "dijkstra_hw" else path


def _field(value, source: str, unit: str | None = None) -> dict:
    item = {"value": value, "source": source}
    if unit is not None:
        item["unit"] = unit
    return item


def _avg_route_hops(summary: dict) -> float:
    hops = []
    for item in _summary_items(summary).values():
        route = item.get("last_route", [])
        if route:
            hops.append(max(0, len(route) - 1))
    return statistics.fmean(hops) if hops else 0.0


def _avg_delay_per_hop(total_delay_s: float | None, route_hops: float) -> float:
    if total_delay_s is None or route_hops <= 0:
        return 0.0
    return total_delay_s / route_hops


def build_simulation_aligned_metrics(summary: dict, log_dir: str | Path, run_id: str = "benchmark") -> dict:
    total = summary["total"]
    latency_avg_ms = total["latency"]["avg_ms"]
    total_delay_s = None if latency_avg_ms is None else latency_avg_ms / 1000.0
    packet_loss_rate = total["loss_rate"]
    route_hops = _avg_route_hops(summary)
    mean_delay_per_hop = _avg_delay_per_hop(total_delay_s, route_hops)
    train_score = None
    if total_delay_s is not None and packet_loss_rate is not None:
        train_score = total_delay_s + 10.0 * packet_loss_rate

    return {
        "schema": "simulation_aligned_metrics.v1",
        "algorithm": "dijkstra",
        "run_id": run_id,
        "source_log_dir": str(log_dir),
        "step": 0,
        "metrics": {
            "packet_loss_rate": _field(packet_loss_rate, "real_ack", "ratio"),
            "total_delay": _field(total_delay_s, "real_ack_latency", "seconds"),
            "link_utilization": _field(0.0, "default", "ratio"),
            "global_link_utilization": _field(0.0, "default", "ratio"),
            "max_link_utilization": _field(0.0, "default", "ratio"),
            "congested_link_ratio": _field(0.0, "default", "ratio"),
            "propagation_delay": _field(0.0, "default", "seconds"),
            "transmission_delay": _field(0.0, "default", "seconds"),
            "queueing_delay": _field(0.0, "default", "seconds"),
            "decomposed_total_delay": _field(total_delay_s, "derived", "seconds"),
            "route_length_hops": _field(route_hops, "derived", "hops"),
            "mean_delay_per_hop": _field(mean_delay_per_hop, "derived", "seconds_per_hop"),
            "route_distance": _field(0.0, "default", "distance_proxy"),
            "train_score": _field(train_score, "derived", "score"),
        },
        "field_sources": {
            "real_ack": "derived from real ACK success and timeout counts",
            "real_ack_latency": "derived from SEND write time to ACK receive time",
            "real_rssi": "derived from RSSI_REQ and RSSI_REPORT",
            "derived": "computed from real benchmark records",
            "default": "not directly measurable on current hardware",
        },
    }


def build_simulation_aligned_rows(summary: dict, results: list[RoundResult], ack_timeout: float) -> list[dict]:
    rows = []
    for target, item in sorted(_summary_items(summary).items()):
        latency_avg_ms = item["latency"]["avg_ms"]
        total_delay_s = None if latency_avg_ms is None else latency_avg_ms / 1000.0
        route_hops = max(0, len(item.get("last_route", [])) - 1)
        train_score = None
        if total_delay_s is not None and item["loss_rate"] is not None:
            train_score = total_delay_s + 10.0 * item["loss_rate"]
        rows.append(
            {
                "schema": "simulation_aligned_metrics.v1",
                "scope": "target",
                "source": item.get("source", "00"),
                "target": item.get("destination", target),
                "packet_loss_rate": item["loss_rate"],
                "total_delay": total_delay_s,
                "link_utilization": 0.0,
                "global_link_utilization": 0.0,
                "max_link_utilization": 0.0,
                "congested_link_ratio": 0.0,
                "propagation_delay": 0.0,
                "transmission_delay": 0.0,
                "queueing_delay": 0.0,
                "decomposed_total_delay": total_delay_s,
                "route_length_hops": route_hops,
                "mean_delay_per_hop": _avg_delay_per_hop(total_delay_s, float(route_hops)),
                "route_distance": 0.0,
                "train_score": train_score,
                "sources": {
                    "packet_loss_rate": "real_ack",
                    "total_delay": "real_ack_latency",
                    "link_utilization": "default",
                    "decomposed_total_delay": "derived",
                    "route_length_hops": "derived",
                },
            }
        )

    for result in results:
        route_hops = max(0, len(result.route) - 1)
        success_delay_s = None if result.latency_ms is None else result.latency_ms / 1000.0
        counts_as_transmission = _counts_as_transmission(result)
        delay_s = success_delay_s if result.success else (ack_timeout if counts_as_transmission else None)
        loss = 0.0 if result.success else (1.0 if counts_as_transmission else None)
        train_score = None if delay_s is None or loss is None else delay_s + 10.0 * loss
        rows.append(
            {
                "schema": "simulation_aligned_metrics.v1",
                "scope": "round",
                "source": f"{result.source:02X}",
                "target": f"{result.target:02X}",
                "round": result.round_index,
                "success": result.success,
                "packet_loss_rate": loss,
                "total_delay": delay_s,
                "status": result.status,
                "route_length_hops": route_hops,
                "mean_delay_per_hop": _avg_delay_per_hop(delay_s, float(route_hops)),
                "train_score": train_score,
                "sources": {
                    "packet_loss_rate": "real_ack" if counts_as_transmission else "route_failed_excluded",
                    "total_delay": "real_ack_latency" if result.success else ("ack_timeout" if counts_as_transmission else "route_failed_excluded"),
                    "route_length_hops": "derived",
                },
            }
        )
    return rows


def write_simulation_aligned_metrics(
    log_dir: Path,
    summary: dict,
    results: list[RoundResult],
    ack_timeout: float,
    run_id: str = "benchmark",
) -> None:
    metrics = build_simulation_aligned_metrics(summary, log_dir, run_id=run_id)
    (log_dir / "simulation_aligned_metrics.json").write_text(
        json.dumps(metrics, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    rows = build_simulation_aligned_rows(summary, results, ack_timeout)
    with (log_dir / "simulation_aligned_metrics.jsonl").open("w", encoding="utf-8") as file_obj:
        for row in rows:
            file_obj.write(json.dumps(row, ensure_ascii=False, sort_keys=True) + "\n")


def build_hardware_test_record(
    summary: dict,
    topology: Topology,
    port: str,
    baud: int,
    nodes: list[int],
    rounds: int,
    payload: str,
    log_dir: str | Path,
    boot_wait: float,
    rssi_seconds: float,
    rssi_requests: int,
    ack_timeout: float,
    interval: float,
    gateway: int,
    run_id: str = "benchmark",
    route_mode: str = BASELINE_ROUTE_MODE,
    optimization_target: dict | None = None,
    sources: list[int] | None = None,
    recollect_consecutive_failures: int = 3,
    path_loss_degrade_threshold: float = 0.10,
    path_avg_degrade_ms: float = 220.0,
    path_health_window: int = 5,
) -> dict:
    routes = routes_to_dict(topology.routes(route_mode=route_mode))
    payload_bytes = max(1, len("".join(payload.split())) // 2)
    return {
        "schema": "hardware_test_record.v1",
        "run_id": run_id,
        "algorithm": "dijkstra",
        "source_log_dir": str(log_dir),
        "test_config": {
            "serial": {
                "port": port,
                "baud": baud,
                "data_bits": 8,
                "parity": "N",
                "stop_bits": 1,
                "dtr": False,
                "rts": False,
            },
            "nodes": {
                "gateway": f"{gateway:02X}",
                "sources": [f"{node:02X}" for node in (sources if sources is not None else nodes)],
                "targets": [f"{node:02X}" for node in nodes],
            },
            "commands": {
                "rssi_command": "RSSI_REQ",
                "send_format": "SEND <dst> <path_len> <path...> <hex_payload>",
                "ack_format": "ACK <dst> <seq>",
                "payload_hex": payload,
                "payload_bytes": payload_bytes,
                "rounds_per_target": rounds,
                "rounds_per_source_target_pair": rounds,
                "ack_timeout_s": ack_timeout,
                "command_interval_s": interval,
                "boot_wait_s": boot_wait,
                "rssi_collect_seconds": rssi_seconds,
                "rssi_requests": rssi_requests,
                "route_mode": route_mode,
                "matrix_mode": "source in sources, target in nodes-source",
                "two_send_latency_policy": "first SEND gateway->source, second SEND gateway->source->target; point_to_point_ms=second_ack_ms-first_ack_ms",
                "recollect_consecutive_failures": recollect_consecutive_failures,
                "optimization_target": optimization_target or {"loss_rate_min": 0.04, "loss_rate_max": 0.06, "ack_timeout_s": 2.0},
            },
        },
        "firmware_params": {
            "source": "sample/sle_mesh_test",
            "protocol": {
                "magic0": "0x4D",
                "magic1": "0x53",
                "version": "0x01",
                "frame_types": {
                    "RSSI_REPORT": "0x01",
                    "DATA": "0x02",
                    "PATH_REQ": "0x03",
                    "PATH_RESP": "0x04",
                    "ACK": "0x05",
                },
            },
            "address": {
                "dongle": "0x00",
                "invalid": "0xFF",
                "sle_mac_prefix": "11:00:00:00:00",
                "node_addr_location": "own_addr.addr[5]",
            },
            "capacity_limits": {
                "adv_handle": 1,
                "adv_data_len_max": 200,
                "max_frame_len": 200,
                "max_neighbors": 32,
                "dedup_table_size": 32,
                "path_cache_size": 8,
                "relay_queue_size": 8,
                "rssi_max_entries": 16,
                "rssi_store_slots": 16,
                "uart_rx_ring_size": 512,
                "uart_line_max": 256,
            },
            "timing_ms": {
                "rssi_report_period_ms": 5000,
                "adv_duration_ms": 125,
                "scan_collect_window_ms": 3000,
                "neighbor_timeout_ms": 15000,
                "dedup_timeout_ms": 2000,
                "worker_sleep_ms": 50,
                "rssi_collect_window_ms": 200,
            },
            "advertising": {
                "announce_mode": "SLE_ANNOUNCE_MODE_NONCONN_SCANABLE",
                "announce_role": "SLE_ANNOUNCE_ROLE_T_CAN_NEGO",
                "announce_level": "SLE_ANNOUNCE_LEVEL_NORMAL",
                "announce_channel_map": "0x07",
                "announce_interval_min": "0xC8",
                "announce_interval_max": "0xC8",
                "announce_interval_note": "source comment maps 0xC8 to 25 ms",
                "defined_sm_adv_tx_power": 14,
                "actual_announce_tx_power": 20,
                "scan_rsp_tx_power_field": 20,
            },
            "connection_params_present_but_not_primary_for_broadcast": {
                "conn_interval_min": "0x14",
                "conn_interval_max": "0x14",
                "conn_max_latency": "0x1F3",
                "conn_supervision_timeout": "0x1F4",
            },
            "scan": {
                "own_addr_type": 0,
                "seek_filter_policy": 0,
                "seek_phys": 1,
                "seek_type_0": 0,
                "seek_interval_0": 160,
                "seek_window_0": 48,
            },
            "uart_commands": {
                "pc_to_dongle": ["RSSI_REQ", "SEND", "PATH_RESP", "TOPO?"],
                "dongle_to_pc": ["RSSI_REPORT", "HEX", "ACK", "DATA", "PATH_REQ", "NO DATA"],
                "note": "ACK is a dongle-to-PC event, not a PC command",
            },
        },
        "routing_params": {
            "algorithm": "dijkstra",
            "route_mode": route_mode,
            "edge_direction": topology.edge_direction,
            "graph_display": "undirected visual presentation; routing calculation keeps directed src_to_neighbor edges",
            "gateway": f"{gateway:02X}",
            "rssi_weight": [
                {"min_rssi": -55, "max_rssi": None, "weight": 1},
                {"min_rssi": -65, "max_rssi": -56, "weight": 3},
                {"min_rssi": -75, "max_rssi": -66, "weight": 6},
                {"min_rssi": -85, "max_rssi": -76, "weight": 12},
                {"min_rssi": None, "max_rssi": -86, "weight": None, "routing": "filtered"},
            ],
            "weak_link_recording": "RSSI below -85 is kept in raw logs but excluded from Dijkstra",
            "path_downlink_format": "SEND <dst> <path_len> <addr0> ... <payload>",
            "node_route_storage": "nodes do not store a fixed gateway route; paths are task-scoped",
        },
        "topology_summary": {
            "nodes": [f"{node:02X}" for node in sorted(topology.nodes)],
            "edges": [asdict(edge) for edge in sorted(topology.edges.values(), key=lambda item: (item.src, item.dst))],
            "routes": routes,
        },
        "result_summary": {
            "total": summary["total"],
            "targets": summary["targets"],
            "pairs": summary.get("pairs", {}),
            "topology_recollect_count": summary.get("topology_recollect_count", 0),
            "latency_policy": {
                "success_latency_only": True,
                "point_to_point_ms": "gateway_to_target_ms - gateway_to_source_ms",
                "total_latency_ms": "point_to_point_ms for Dijkstra",
                "timeout_latency_for_simulation_aligned_round_record": "ack_timeout_s",
                "ack_match_rule": "ACK dst equals current target; seq is recorded but not required to match",
            },
        },
        "field_sources": {
            "real_serial": "raw TX/RX serial bytes",
            "real_rssi": "RSSI_REQ and RSSI_REPORT",
            "real_ack": "SEND followed by ACK or timeout",
            "firmware_source": "sample/sle_mesh_test source constants",
            "derived": "computed from real benchmark records",
            "default": "not directly measurable on current hardware",
        },
    }


def write_hardware_test_record(
    log_dir: Path,
    summary: dict,
    topology: Topology,
    port: str,
    baud: int,
    nodes: list[int],
    rounds: int,
    payload: str,
    boot_wait: float,
    rssi_seconds: float,
    rssi_requests: int,
    ack_timeout: float,
    interval: float,
    gateway: int,
    run_id: str = "benchmark",
) -> None:
    record = build_hardware_test_record(
        summary=summary,
        topology=topology,
        port=port,
        baud=baud,
        nodes=nodes,
        rounds=rounds,
        payload=payload,
        log_dir=log_dir,
        boot_wait=boot_wait,
        rssi_seconds=rssi_seconds,
        rssi_requests=rssi_requests,
        ack_timeout=ack_timeout,
        interval=interval,
        gateway=gateway,
        run_id=run_id,
    )
    (log_dir / "hardware_test_record.json").write_text(
        json.dumps(record, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


def build_readable_report(summary: dict, hardware_record: dict, aligned_metrics: dict, log_dir: str | Path) -> str:
    total = summary["total"]
    test_config = hardware_record["test_config"]
    commands = test_config["commands"]
    serial = test_config["serial"]
    routing = hardware_record["routing_params"]
    firmware = hardware_record["firmware_params"]
    advertising = firmware["advertising"]
    timing = firmware["timing_ms"]
    scan = firmware["scan"]
    targets = test_config["nodes"]["targets"]

    lines = [
        "# Dijkstra 真实硬件测试汇总报告",
        "",
        f"- 生成时间：{datetime.now().isoformat(timespec='seconds')}",
        f"- 日志目录：`{log_dir}`",
        f"- 测试对象：网关 `{test_config['nodes']['gateway']}`，目标节点 `{', '.join(targets)}`",
        "- 地址说明：CLI 按十六进制地址解析，因此目标 `10` 表示地址 `0x10`。",
        f"- 丢包率目标：`<10%`，平均点到点延时目标：`<220ms`，ACK timeout：`{commands['ack_timeout_s']}s`，是否达标：`{_new_target_status(summary)}`",
        f"- 算法模式：`{commands.get('route_mode', routing.get('route_mode', BASELINE_ROUTE_MODE))}`",
        f"- 最优参数组合：`interval={commands['command_interval_s']}s, rssi_requests={commands['rssi_requests']}, route_mode={commands.get('route_mode', routing.get('route_mode', BASELINE_ROUTE_MODE))}`",
        f"- 发包间隔：`{commands['command_interval_s']}s`",
        f"- 计划轮次：`{total.get('planned_rounds')}`，实际SEND：`{total['sent']}`，成功：`{total['success']}`，ACK timeout：`{total.get('ack_timeout_loss')}`，路由不可达：`{total.get('route_failed')}`，实际丢包率：`{_fmt_rate(total['loss_rate'])}`",
        (
            f"- 总体成功 ACK 延时：平均 `{_fmt_ms(total['latency']['avg_ms'])}`，"
            f"最小 `{_fmt_ms(total['latency']['min_ms'])}`，最大 `{_fmt_ms(total['latency']['max_ms'])}`，"
            f"P95 `{_fmt_ms(total['latency']['p95_ms'])}`"
        ),
        "",
        "## 拓扑图",
        "",
        "![Dijkstra RSSI 拓扑图](拓扑图.svg)",
        "",
        "文本拓扑文件：[`拓扑图.txt`](拓扑图.txt)",
        "",
        "Excel 汇总文件：[`测试指标汇总.xlsx`](测试指标汇总.xlsx)",
        "",
        "## 测试结果",
        "",
        "| 出发点 | 目标点 | 路径 | 成功/实际SEND | ACK timeout | 路由不可达 | 丢包率 | 点到点平均 | P95 | 网关到源 | 网关到目标 | 总延时 | 重采 | 最弱 RSSI |",
        "|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|",
    ]
    for target, item in sorted(_summary_items(summary).items()):
        latency = item["latency"]
        lines.append(
            f"| `{item.get('source', '00')}` | `{item.get('destination', target)}` | `{_fmt_path(item['last_route'])}` | `{item['success']}/{item['sent']}` | `{item.get('ack_timeout_loss')}` | `{item.get('route_failed')}` | "
            f"`{_fmt_rate(item['loss_rate'])}` | `{_fmt_ms(latency['avg_ms'])}` | "
            f"`{_fmt_ms(latency['p95_ms'])}` | `{_fmt_ms(item.get('gateway_to_source_ms'))}` | `{_fmt_ms(item.get('gateway_to_target_ms'))}` | "
            f"`{_fmt_ms(item.get('total_latency_ms'))}` | `{item.get('recollect_count')}` | `{item.get('path_rssi', {}).get('min_rssi')}` |"
        )

    metrics = summary.get("metrics", {})
    rssi = metrics.get("rssi_fluctuation", {})
    jitter = metrics.get("latency_jitter", {})
    lines.extend(
        [
            "",
            "## 指标总结对比",
            "",
            "| 指标 | 当前值 | 单位 | 说明 |",
            "|---|---:|---|---|",
            f"| 算法计算延时 | `{_fmt_ms(metrics.get('algorithm_compute_latency_ms'))}` | ms | 网关/上位机算出 Dijkstra 路由路径的耗时 |",
            f"| 指令下发延时 | `{_fmt_ms(metrics.get('command_downlink_latency_ms'))}` | ms | 当前硬件无中间节点时间戳，第一版用 SEND 到 ACK 总延时近似记录 |",
            f"| 端到端实际传输平均延时 | `{_fmt_ms(metrics.get('end_to_end_avg_latency_ms'))}` | ms | 现有统计总 ACK 时延 |",
            f"| 全局平均丢包率 | `{_fmt_rate(metrics.get('global_avg_loss_rate'))}` | ratio | 总 timeout / 总发送 |",
            f"| 单路径平均跳数 | `{_fmt_number(metrics.get('avg_route_hops'))}` | hops | 各目标最终路径跳数平均值 |",
            f"| 平均单跳传输耗时 | `{_fmt_ms(metrics.get('avg_single_hop_latency_ms'))}` | ms/hop | 端到端平均延时 / 跳数折算 |",
            f"| RSSI 实时波动范围 | `{_fmt_number(rssi.get('range'))}` | dB | 当前拓扑边 RSSI 最大值减最小值 |",
            f"| RSSI 标准差 | `{_fmt_number(rssi.get('stddev'))}` | dB | 当前拓扑边 RSSI 标准差 |",
            f"| 时延抖动均值 | `{_fmt_ms(jitter.get('jitter_ms'))}` | ms | 相邻成功 ACK 延时差值均值 |",
            f"| 时延标准差 | `{_fmt_ms(jitter.get('stddev_ms'))}` | ms | 成功 ACK 延时标准差 |",
            "",
            "## 完整指标汇总",
            "",
            "| 出发点 | 目标点 | 路径 | 算法计算延时 | 指令下发延时 | 端到端实际传输平均延时 | 节点丢包率 | 全局平均丢包率 | 路由跳数 | 单路径平均跳数 | 平均单跳传输耗时 | RSSI 实时波动 | 时延抖动变化 |",
            "|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---|---|",
        ]
    )
    for target, item in sorted(_summary_items(summary).items()):
        lines.append(
            f"| `{item.get('source', '00')}` | `{item.get('destination', target)}` | `{_fmt_path(item['last_route'])}` | "
            f"`{_fmt_ms(item.get('algorithm_compute_latency_ms', metrics.get('algorithm_compute_latency_ms')))}` | "
            f"`{_fmt_ms(item.get('command_downlink_latency_ms'))}` | "
            f"`{_fmt_ms(item.get('end_to_end_avg_latency_ms'))}` | "
            f"`{_fmt_rate(item.get('loss_rate'))}` | "
            f"`{_fmt_rate(metrics.get('global_avg_loss_rate'))}` | "
            f"`{item.get('route_hops')}` | "
            f"`{_fmt_number(metrics.get('avg_route_hops'))}` | "
            f"`{_fmt_ms(item.get('avg_single_hop_latency_ms'))}` | "
            f"`min {_fmt_number(rssi.get('min'))} / max {_fmt_number(rssi.get('max'))} / range {_fmt_number(rssi.get('range'))}dB / std {_fmt_number(rssi.get('stddev'))}` | "
            f"`节点 {_fmt_ms(item.get('latency_jitter', {}).get('jitter_ms'))} / 全局 {_fmt_ms(jitter.get('jitter_ms'))}` |"
        )

    lines.extend(
        [
            "",
            "## 路径指标",
            "",
            "| 目标点 | 路由跳数 | 单路径丢包率 | 平均单跳传输耗时 | 时延抖动 | RSSI 均值 | 最弱 RSSI |",
            "|---|---:|---:|---:|---:|---:|---:|",
        ]
    )
    for target, item in sorted(_summary_items(summary).items()):
        lines.append(
            f"| `{item.get('destination', target)}` | `{item.get('route_hops')}` | `{_fmt_rate(item.get('loss_rate'))}` | "
            f"`{_fmt_ms(item.get('avg_single_hop_latency_ms'))}` | `{_fmt_ms(item.get('latency_jitter', {}).get('jitter_ms'))}` | "
            f"`{_fmt_number(item.get('path_rssi', {}).get('mean_rssi'))}` | `{item.get('path_rssi', {}).get('min_rssi')}` |"
        )

    lines.extend(
        [
            "",
            "来源说明：",
            "",
            "| 来源 | 含义 |",
            "|---|---|",
            "| `real_ack` | 由真实 ACK 成功/timeout 统计得到 |",
            "| `real_ack_latency` | 由 SEND 写入到 ACK 接收的真实时间差计算得到 |",
            "| `real_rssi` | 由 RSSI_REQ 返回的 RSSI_REPORT 得到 |",
            "| `derived` | 由真实测试记录派生计算得到 |",
            "| `default` | 当前硬件不可直接测量，使用默认值占位 |",
            "",
            "## 关键测试参数",
            "",
            "| 参数 | 当前值 |",
            "|---|---|",
            f"| 串口 | `{serial['port']}` |",
            f"| 波特率 | `{serial['baud']}` |",
            f"| 串口格式 | `{serial['data_bits']}{serial['parity']}{serial['stop_bits']}` |",
            f"| DTR / RTS | `{serial['dtr']}` / `{serial['rts']}` |",
            f"| RSSI 命令 | `{commands['rssi_command']}` |",
            f"| SEND 格式 | `{commands['send_format']}` |",
            f"| ACK 格式 | `{commands['ack_format']}` |",
            f"| Payload | `{commands['payload_hex']}`，`{commands['payload_bytes']}` bytes |",
            f"| 每节点轮数 | `{commands['rounds_per_target']}` |",
            f"| ACK timeout | `{commands['ack_timeout_s']}s` |",
            f"| 命令间隔 | `{commands['command_interval_s']}s` |",
            f"| 启动等待 | `{commands['boot_wait_s']}s` |",
            f"| RSSI 采集窗口 | `{commands['rssi_collect_seconds']}s` |",
            f"| RSSI_REQ 次数 | `{commands['rssi_requests']}` |",
            f"| 路由模式 | `{commands.get('route_mode', routing.get('route_mode', BASELINE_ROUTE_MODE))}` |",
            "",
            "## 路由参数",
            "",
            "| 参数 | 当前值 |",
            "|---|---|",
            f"| 算法 | `{routing['algorithm']}` |",
            f"| 算法模式 | `{routing.get('route_mode', BASELINE_ROUTE_MODE)}` |",
            f"| 网关 | `{routing['gateway']}` |",
            f"| 边方向 | `{routing['edge_direction']}` |",
            f"| 图展示方式 | `{routing.get('graph_display', 'undirected visual presentation')}` |",
            f"| 下发路径格式 | `{routing['path_downlink_format']}` |",
            f"| 节点路由保存策略 | `{routing['node_route_storage']}` |",
            "",
            "RSSI 权重规则：",
            "",
            "| RSSI 范围 | Dijkstra 权重 |",
            "|---|---:|",
            "| `>= -55` | `1` |",
            "| `-56 ~ -65` | `3` |",
            "| `-66 ~ -75` | `6` |",
            "| `-76 ~ -85` | `12` |",
            "| `< -85` | 不参与路由 |",
            "",
            "可靠模式权重规则：",
            "",
            "| RSSI 范围 | reliable_dijkstra_v1 权重 |",
            "|---|---:|",
            "| `>= -55` | `1 + 0.5 hop penalty` |",
            "| `-56 ~ -65` | `3 + 0.5 hop penalty` |",
            "| `-66 ~ -75` | `6 + 0.5 hop penalty` |",
            "| `-76 ~ -80` | `16 + 0.5 hop penalty` |",
            "| `-81 ~ -85` | `32 + 0.5 hop penalty` |",
            "| `< -85` | 不参与路由 |",
            "",
            "## 当前广播与扫描参数",
            "",
            "| 参数 | 当前值 |",
            "|---|---|",
            f"| 广播模式 | `{advertising['announce_mode']}` |",
            f"| 广播角色 | `{advertising['announce_role']}` |",
            f"| 广播等级 | `{advertising['announce_level']}` |",
            f"| 广播信道图 | `{advertising['announce_channel_map']}` |",
            f"| 广播间隔 min/max | `{advertising['announce_interval_min']}` / `{advertising['announce_interval_max']}`，源码注释约 `25ms` |",
            f"| 实际广播功率 | `{advertising['actual_announce_tx_power']}` |",
            f"| 宏定义广播功率 | `{advertising['defined_sm_adv_tx_power']}` |",
            f"| Scan response TX power 字段 | `{advertising['scan_rsp_tx_power_field']}` |",
            f"| 单次广播窗口 | `{timing['adv_duration_ms']}ms` |",
            f"| RSSI 周期上报间隔 | `{timing['rssi_report_period_ms']}ms` |",
            f"| RSSI 聚合窗口 | `{timing['rssi_collect_window_ms']}ms` |",
            f"| 扫描采集窗口 | `{timing['scan_collect_window_ms']}ms` |",
            f"| 邻居超时 | `{timing['neighbor_timeout_ms']}ms` |",
            f"| 去重超时 | `{timing['dedup_timeout_ms']}ms` |",
            f"| Worker sleep | `{timing['worker_sleep_ms']}ms` |",
            f"| 扫描 interval/window | `{scan['seek_interval_0']}` / `{scan['seek_window_0']}` |",
            f"| 扫描 PHY / type | `{scan['seek_phys']}` / `{scan['seek_type_0']}` |",
            "",
            "## 结论",
            "",
            _build_readable_conclusion(summary),
        ]
    )
    return "\n".join(lines) + "\n"


def _build_readable_conclusion(summary: dict) -> str:
    total = summary["total"]
    high_loss = [
        (target, item)
        for target, item in sorted(_summary_items(summary).items())
        if item.get("loss_rate") is not None and item["loss_rate"] >= 0.3
    ]
    stable = [
        item.get("destination", target)
        for target, item in sorted(_summary_items(summary).items())
        if item.get("loss_rate") is not None and item["loss_rate"] <= 0.15
    ]
    parts = [
        f"本轮测试完成，总发送 `{total['sent']}` 次，成功 `{total['success']}` 次，总丢包率 `{_fmt_rate(total['loss_rate'])}`。"
    ]
    if high_loss:
        details = []
        for target, item in high_loss:
            min_rssi = item.get("path_rssi", {}).get("min_rssi")
            details.append(f"`{target}`({ _fmt_rate(item['loss_rate']) }, 最弱 RSSI `{min_rssi}`)")
        parts.append(f"丢包偏高节点为 {', '.join(details)}。")
    if stable:
        parts.append(f"相对稳定节点为 `{('/'.join(stable))}`，丢包率不高于 `15.00%`。")
    return "".join(parts)


def write_readable_report(log_dir: Path, summary: dict, hardware_record: dict, aligned_metrics: dict) -> None:
    (log_dir / "测试结果汇报.md").write_text(
        build_readable_report(summary, hardware_record, aligned_metrics, log_dir),
        encoding="utf-8",
    )


def run_benchmark(
    port: str,
    baud: int,
    nodes: list[int],
    rounds: int,
    payload: str,
    log_dir: str | Path,
    boot_wait: float = 5.0,
    rssi_seconds: float = 8.0,
    rssi_requests: int = 5,
    ack_timeout: float = 2.0,
    interval: float = 1.0,
    gateway: int = 0x00,
    dongle_addr: int | None = None,
    min_rssi: int = -85,
    max_hops: int = 4,
    route_mode: str = BASELINE_ROUTE_MODE,
    optimization_target: dict | None = None,
    sources: list[int] | None = None,
    recollect_consecutive_failures: int = 3,
    path_loss_degrade_threshold: float = 0.10,
    path_avg_degrade_ms: float = 220.0,
    path_health_window: int = 5,
) -> dict:
    log_path = resolve_benchmark_log_dir(log_dir)
    logger = BenchmarkLogger(log_path)
    client: SerialClient | None = None
    results: list[RoundResult] = []
    topology: Topology | None = None
    demand = demand_for_payload(payload)
    try:
        client = SerialClient(port, baud, raw_callback=logger.raw_callback)
        pairs = source_target_pairs(nodes, sources)
        logger.event(
            "bench_start",
            port=port,
            baud=baud,
            nodes=nodes,
            sources=sources,
            pairs=[{"source": source, "target": target} for source, target in pairs],
            rounds=rounds,
            payload=payload,
            demand=demand,
            matrix_mode=True,
        )
        _drain_messages(client, logger, boot_wait)
        topology = collect_topology(client, logger, rssi_seconds, rssi_requests, nodes, gateway, dongle_addr=dongle_addr, min_rssi=min_rssi, max_hops=max_hops)
        # 验证路由表包含所有节点
        expected_nodes = set(nodes)
        actual_nodes = set(topology.nodes)
        if not expected_nodes.issubset(actual_nodes):
            missing = expected_nodes - actual_nodes
            missing_hex = [f"0x{n:02X}" for n in sorted(missing)]
            logger.event("route_table_incomplete", missing=missing_hex, expected=len(expected_nodes), actual=len(actual_nodes))
            raise RuntimeError(f"failed: 路由表缺少节点 {missing_hex}，测试终止")
        json_path = _json_dir(log_path)
        save_topology(json_path / "state.json", topology)
        route_compute_ms = _write_routes_json(log_path, topology, route_mode)
        routes = routes_to_dict(topology.routes(route_mode=route_mode))
        logger.event("routes", routes=routes, algorithm_compute_latency_ms=route_compute_ms, route_mode=route_mode)

        consecutive_failures: dict[tuple[int, int], int] = {}
        path_history: dict[tuple[int, int, tuple[int, ...]], list[dict]] = {}
        for source, target in pairs:
            for round_index in range(1, rounds + 1):
                topology_recollected = False
                if _should_recollect(consecutive_failures, source, target, recollect_consecutive_failures):
                    topology, _, route_compute_ms = _recollect_topology(
                        client=client,
                        logger=logger,
                        log_path=log_path,
                        rssi_seconds=rssi_seconds,
                        rssi_requests=rssi_requests,
                        nodes=nodes,
                        gateway=gateway,
                        dongle_addr=dongle_addr,
                        min_rssi=min_rssi,
                        max_hops=max_hops,
                        route_mode=route_mode,
                        reason=f"consecutive_failures:{source:02X}:{target:02X}",
                        old_topology=topology,
                    )
                    routes = routes_to_dict(topology.routes(route_mode=route_mode))
                    _reset_recollect_counter(consecutive_failures, source, target)
                    topology_recollected = True

                gateway_route, pair_route, full_path, route_fallback, health_reason = _resolve_healthy_matrix_routes(
                    topology,
                    gateway,
                    source,
                    target,
                    route_mode,
                    path_history,
                    loss_threshold=path_loss_degrade_threshold,
                    avg_latency_threshold_ms=path_avg_degrade_ms,
                    window=path_health_window,
                    max_hops=max_hops,
                )
                if health_reason:
                    logger.event(
                        "dijkstra_path_health_penalty",
                        source=source,
                        target=target,
                        route=pair_route.path,
                        reason=health_reason,
                    )
                if gateway_route.status != "valid" or pair_route.status != "valid" or not full_path:
                    logger.event(
                        "route_unreachable",
                        source=source,
                        target=target,
                        gateway_route_status=gateway_route.status,
                        pair_route_status=pair_route.status,
                    )
                    _record_unreachable_rounds(
                        logger=logger,
                        results=results,
                        source=source,
                        target=target,
                        round_index=round_index,
                        payload=payload,
                        demand=demand,
                        interval=interval,
                        route_mode=route_mode,
                        error="gateway_to_source_or_source_to_target_unreachable",
                        topology_recollected=topology_recollected,
                    )
                    _drain_messages(client, logger, interval)
                    continue

                gateway_success, gateway_to_source_ms, _, _, gateway_send_ts, gateway_retries, ack1_monotonic = _send_and_wait_ack_with_retry(
                    client,
                    logger,
                    topology,
                    dst=source,
                    path=gateway_route.path,
                    payload=payload,
                    ack_timeout=ack_timeout,
                    max_retries=1,
                    retry_interval=0.5,
                    event_payload={
                        "phase": "gateway_to_source",
                        "source": source,
                        "pair_target": target,
                        "round": round_index,
                        "route": gateway_route.path,
                        "cost": gateway_route.cost,
                        "demand": demand,
                        "interval_s": interval,
                        "route_mode": route_mode,
                    },
                )
                if not gateway_success:
                    result = RoundResult(
                        target=target,
                        round_index=round_index,
                        route=full_path,
                        cost=pair_route.cost,
                        command=build_send_command(source, gateway_route.path, payload).rstrip(),
                        success=False,
                        latency_ms=None,
                        ack_seq=None,
                        payload=payload,
                        demand=demand,
                        send_ts=gateway_send_ts,
                        ack_ts=None,
                        interval_s=interval,
                        status="gateway_to_source_timeout",
                        error="gateway_to_source_timeout",
                        route_mode=route_mode,
                        route_fallback=route_fallback,
                        source=source,
                        gateway_to_source_ms=gateway_to_source_ms,
                        topology_recollected=topology_recollected,
                    )
                    results.append(result)
                    logger.round(result)
                    path_history.setdefault((source, target, _path_key(pair_route.path)), []).append({"success": False, "point_to_point_ms": None})
                    _mark_pair_result(consecutive_failures, source, target, False)
                    _drain_messages(client, logger, interval)
                    continue

                target_success, gateway_to_target_ms, ack_seq, ack_ts, send_ts, target_retries, ack2_monotonic = _send_and_wait_ack_with_retry(
                    client,
                    logger,
                    topology,
                    dst=target,
                    path=full_path,
                    payload=payload,
                    ack_timeout=ack_timeout,
                    max_retries=1,
                    retry_interval=0.5,
                    event_payload={
                        "phase": "gateway_to_target_via_source",
                        "source": source,
                        "pair_target": target,
                        "round": round_index,
                        "route": full_path,
                        "source_to_target_route": pair_route.path,
                        "cost": pair_route.cost,
                        "demand": demand,
                        "interval_s": interval,
                        "route_mode": route_mode,
                        "route_fallback": route_fallback,
                    },
                )
                point_to_point_ms = None
                if gateway_to_source_ms is not None and gateway_to_target_ms is not None:
                    point_to_point_ms = max(0.0, gateway_to_target_ms - gateway_to_source_ms)
                
                # 推理时间 = Dijkstra路由计算耗时
                inference_ms = locals().get("route_compute_ms")
                
                # 源到目标延时 = 下发路径→收到目标ACK的时间 - 网关到源的时间
                source_to_target_ms = None
                total_latency_ms = None
                if gateway_to_source_ms is not None and gateway_to_target_ms is not None:
                    source_to_target_ms = max(0.0, gateway_to_target_ms - gateway_to_source_ms)
                    if inference_ms is not None:
                        total_latency_ms = inference_ms + source_to_target_ms
                
                success = target_success and point_to_point_ms is not None
                result = RoundResult(
                    target=target,
                    round_index=round_index,
                    route=full_path,
                    cost=pair_route.cost,
                    command=build_send_command(target, full_path, payload).rstrip(),
                    success=success,
                    latency_ms=point_to_point_ms,
                    ack_seq=ack_seq,
                    payload=payload,
                    demand=demand,
                    send_ts=send_ts,
                    ack_ts=ack_ts,
                    interval_s=interval,
                    status="success" if success else ("latency_estimation_error" if target_success else "gateway_to_target_timeout"),
                    error=None if success else ("latency_estimation_error" if target_success else "gateway_to_target_timeout"),
                    route_mode=route_mode,
                    route_fallback=route_fallback,
                    source=source,
                    gateway_to_source_ms=gateway_to_source_ms,
                    gateway_to_target_ms=gateway_to_target_ms,
                    point_to_point_ms=point_to_point_ms,
                    total_latency_ms=total_latency_ms,
                    inference_ms=inference_ms,
                    source_to_target_ms=source_to_target_ms,
                    topology_recollected=topology_recollected,
                    ack1_monotonic=ack1_monotonic,
                    send2_monotonic=send2_monotonic,
                )
                results.append(result)
                logger.round(result)
                path_history.setdefault((source, target, _path_key(pair_route.path)), []).append({"success": success, "point_to_point_ms": point_to_point_ms})
                _mark_pair_result(consecutive_failures, source, target, success)
                _drain_messages(client, logger, interval)
    except Exception as exc:
        logger.event("bench_error", error=str(exc))
        (log_path / "bench_error.json").write_text(
            json.dumps(
                {
                    "ts": datetime.now().isoformat(timespec="milliseconds"),
                    "algorithm": "dijkstra",
                    "status": "error",
                    "error": str(exc),
                    "log_dir": str(log_path),
                },
                ensure_ascii=False,
                indent=2,
                sort_keys=True,
            )
            + "\n",
            encoding="utf-8",
        )
        raise
    finally:
        if client is not None:
            client.close()
        logger.event("bench_end")
        logger.close()

    if topology is None:
        relay_excluded = {dongle_addr} if dongle_addr is not None else set()
        topology = Topology(stale_seconds=None, edge_direction="src_to_neighbor", gateway=gateway, relay_excluded=relay_excluded, min_rssi=min_rssi)
    summary = summarize(results, nodes, sources)
    enrich_summary_with_rssi(summary, topology)
    summary["rounds"] = [asdict(result) for result in results]
    summary["config"] = {
        "port": port,
        "baud": baud,
        "nodes": nodes,
        "sources": sources,
        "test_pairs": [[source, target] for source, target in source_target_pairs(nodes, sources)],
        "rounds": rounds,
        "matrix_mode": True,
        "latency_policy": "two_send_point_to_point=gateway_to_target_ms-gateway_to_source_ms",
        "recollect_consecutive_failures": recollect_consecutive_failures,
        "path_loss_degrade_threshold": path_loss_degrade_threshold,
        "path_avg_degrade_ms": path_avg_degrade_ms,
        "path_health_window": path_health_window,
        "payload": payload,
        "gateway": gateway,
        "ack_timeout": ack_timeout,
        "interval": interval,
        "rssi_seconds": rssi_seconds,
        "rssi_requests": rssi_requests,
        "route_mode": route_mode,
        "optimization_target": optimization_target or {"loss_rate_min": 0.04, "loss_rate_max": 0.06, "ack_timeout": 2.0},
        "log_dir": str(log_path),
        "defaults": DEFAULTS.to_dict(),
        "field_sources": {
            "rssi": "real_rssi",
            "ack_latency": "real_ack",
            "demand": "derived",
            "unmeasured_simulation_params": "default",
        },
    }
    enrich_summary_with_metrics(summary, topology, route_compute_ms=locals().get("route_compute_ms"))
    summary["log_dir"] = str(log_path)
    json_path = _json_dir(log_path)
    (json_path / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    aligned_metrics = build_simulation_aligned_metrics(summary, log_path)
    (json_path / "simulation_aligned_metrics.json").write_text(
        json.dumps(aligned_metrics, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    rows = build_simulation_aligned_rows(summary, results, ack_timeout)
    with (json_path / "simulation_aligned_metrics.jsonl").open("w", encoding="utf-8") as file_obj:
        for row in rows:
            file_obj.write(json.dumps(row, ensure_ascii=False, sort_keys=True) + "\n")
    hardware_record = build_hardware_test_record(
        summary=summary,
        topology=topology,
        port=port,
        baud=baud,
        nodes=nodes,
        rounds=rounds,
        payload=payload,
        log_dir=log_path,
        boot_wait=boot_wait,
        rssi_seconds=rssi_seconds,
        rssi_requests=rssi_requests,
        ack_timeout=ack_timeout,
        interval=interval,
        gateway=gateway,
        route_mode=route_mode,
        optimization_target=optimization_target,
        sources=sources,
        recollect_consecutive_failures=recollect_consecutive_failures,
    )
    (json_path / "hardware_test_record.json").write_text(
        json.dumps(hardware_record, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    write_topology_svg(log_path / "拓扑图.svg", topology, summary)
    write_text_topology(log_path / "拓扑图.txt", topology, summary, gateway=gateway)
    write_excel_summary(log_path / "测试指标汇总.xlsx", summary, topology, summary["config"])
    write_readable_report(log_path, summary, hardware_record, aligned_metrics)
    return summary


def run_interval_sweep(
    port: str,
    baud: int,
    nodes: list[int],
    rounds: int,
    payload: str,
    log_dir: str | Path,
    intervals: list[float],
    boot_wait: float = 5.0,
    rssi_seconds: float = 15.0,
    rssi_requests: int = 5,
    ack_timeout: float = 2.0,
    gateway: int = 0x00,
    route_mode: str = BASELINE_ROUTE_MODE,
) -> dict:
    root = Path(log_dir)
    root.mkdir(parents=True, exist_ok=True)
    results = []
    for interval in intervals:
        interval_dir = root / f"interval_{interval:g}s"
        summary = run_benchmark(
            port=port,
            baud=baud,
            nodes=nodes,
            rounds=rounds,
            payload=payload,
            log_dir=interval_dir,
            boot_wait=boot_wait,
            rssi_seconds=rssi_seconds,
            rssi_requests=rssi_requests,
            ack_timeout=ack_timeout,
            interval=interval,
            gateway=gateway,
            route_mode=route_mode,
        )
        results.append(
            {
                "interval": interval,
                "log_dir": str(interval_dir),
                "sent": summary["total"]["sent"],
                "success": summary["total"]["success"],
                "loss_rate": summary["total"]["loss_rate"],
                "latency": summary["total"]["latency"],
                "targets": summary["targets"],
                "route_mode": route_mode,
                "rssi_requests": rssi_requests,
            }
        )

    sweep = {
        "config": {
            "port": port,
            "baud": baud,
            "nodes": nodes,
            "rounds": rounds,
            "payload": payload,
            "intervals": intervals,
            "gateway": gateway,
            "ack_timeout": ack_timeout,
            "rssi_seconds": rssi_seconds,
            "rssi_requests": rssi_requests,
            "route_mode": route_mode,
        },
        "results": results,
    }
    (root / "sweep_summary.json").write_text(json.dumps(sweep, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    write_sweep_report(root, sweep)
    return sweep


def write_sweep_report(log_dir: Path, sweep: dict) -> None:
    lines = [
        "# Dijkstra Interval Sweep Report",
        "",
        f"- Generated: {datetime.now().isoformat(timespec='seconds')}",
        f"- Nodes: {','.join(str(node) for node in sweep['config']['nodes'])}",
        f"- Rounds per target: {sweep['config']['rounds']}",
        "",
        "| Interval | Sent | Success | Loss | Avg | Min | Max | P95 | Log |",
        "|---:|---:|---:|---:|---:|---:|---:|---:|---|",
    ]
    for item in sweep["results"]:
        latency = item["latency"]
        lines.append(
            f"| {item['interval']:g}s | {item['sent']} | {item['success']} | {item['loss_rate']:.2%} | "
            f"{_fmt_ms(latency['avg_ms'])} | {_fmt_ms(latency['min_ms'])} | {_fmt_ms(latency['max_ms'])} | "
            f"{_fmt_ms(latency['p95_ms'])} | {item['log_dir']} |"
        )
    (log_dir / "sweep_report.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def run_optimization_sweep(
    port: str,
    baud: int,
    nodes: list[int],
    rounds: int,
    payload: str,
    log_dir: str | Path,
    intervals: list[float],
    route_modes: list[str],
    rssi_requests_values: list[int],
    boot_wait: float = 5.0,
    rssi_seconds: float = 8.0,
    ack_timeout: float = 2.0,
    gateway: int = 0x00,
    target_min: float = 0.04,
    target_max: float = 0.06,
) -> dict:
    root = Path(log_dir)
    root.mkdir(parents=True, exist_ok=True)
    target = {"loss_rate_min": target_min, "loss_rate_max": target_max, "ack_timeout": ack_timeout}
    results = []
    for route_mode in route_modes:
        for rssi_requests in rssi_requests_values:
            for interval in intervals:
                combo_dir = root / f"{route_mode}_rssi{rssi_requests}_interval_{interval:g}s"
                summary = run_benchmark(
                    port=port,
                    baud=baud,
                    nodes=nodes,
                    rounds=rounds,
                    payload=payload,
                    log_dir=combo_dir,
                    boot_wait=boot_wait,
                    rssi_seconds=rssi_seconds,
                    rssi_requests=rssi_requests,
                    ack_timeout=ack_timeout,
                    interval=interval,
                    gateway=gateway,
                    route_mode=route_mode,
                    optimization_target=target,
                )
                loss_rate = summary["total"]["loss_rate"]
                in_target = loss_rate is not None and target_min <= loss_rate <= target_max
                distance = min(abs(loss_rate - target_min), abs(loss_rate - target_max)) if loss_rate is not None and not in_target else 0.0
                results.append(
                    {
                        "interval": interval,
                        "route_mode": route_mode,
                        "rssi_requests": rssi_requests,
                        "log_dir": str(combo_dir),
                        "sent": summary["total"]["sent"],
                        "success": summary["total"]["success"],
                        "loss_rate": loss_rate,
                        "in_target": in_target,
                        "distance_to_target": distance,
                        "latency": summary["total"]["latency"],
                    }
                )
    best = select_best_optimization_result(results, target_min, target_max)
    sweep = {
        "config": {
            "port": port,
            "baud": baud,
            "nodes": nodes,
            "rounds": rounds,
            "payload": payload,
            "intervals": intervals,
            "route_modes": route_modes,
            "rssi_requests_values": rssi_requests_values,
            "gateway": gateway,
            "ack_timeout": ack_timeout,
            "target_min": target_min,
            "target_max": target_max,
        },
        "results": results,
        "best": best,
    }
    (root / "optimization_summary.json").write_text(json.dumps(sweep, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    write_optimization_report(root, sweep)
    return sweep


def select_best_optimization_result(results: list[dict], target_min: float = 0.04, target_max: float = 0.06) -> dict | None:
    if not results:
        return None
    in_target = [item for item in results if item.get("in_target")]
    if in_target:
        return sorted(in_target, key=lambda item: (item.get("latency", {}).get("avg_ms") or float("inf"), item["loss_rate"]))[0]
    return sorted(results, key=lambda item: (abs((item.get("loss_rate") or 1.0) - ((target_min + target_max) / 2)), item.get("loss_rate") or 1.0))[0]


def write_optimization_report(log_dir: Path, sweep: dict) -> None:
    best = sweep.get("best")
    lines = [
        "# Dijkstra Optimization Sweep Report",
        "",
        f"- Generated: {datetime.now().isoformat(timespec='seconds')}",
        f"- Target loss: {sweep['config']['target_min']:.2%}~{sweep['config']['target_max']:.2%}",
        f"- ACK timeout: {sweep['config']['ack_timeout']}s",
        f"- Best: {best['route_mode']} interval={best['interval']:g}s rssi_requests={best['rssi_requests']} loss={best['loss_rate']:.2%}" if best else "- Best: n/a",
        "",
        "| Route mode | RSSI requests | Interval | Sent | Success | Loss | In target | Avg | P95 | Log |",
        "|---|---:|---:|---:|---:|---:|---|---:|---:|---|",
    ]
    for item in sweep["results"]:
        latency = item["latency"]
        lines.append(
            f"| {item['route_mode']} | {item['rssi_requests']} | {item['interval']:g}s | {item['sent']} | {item['success']} | "
            f"{item['loss_rate']:.2%} | {'yes' if item['in_target'] else 'no'} | {_fmt_ms(latency['avg_ms'])} | {_fmt_ms(latency['p95_ms'])} | {item['log_dir']} |"
        )
    (log_dir / "optimization_report.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
