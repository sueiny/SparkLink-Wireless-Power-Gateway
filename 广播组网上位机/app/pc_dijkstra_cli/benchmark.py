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
from .topology_svg import write_topology_svg, write_topology_svgs_by_source
from .routing import SAMPLE_ROUTE_MODE, Route, dijkstra

try:
    from serial.serialutil import SerialException
except ImportError:
    SerialException = Exception


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
    route_mode: str = SAMPLE_ROUTE_MODE
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
    phase: int = 1
    ack_timeout_s: float | None = None


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
    unsupported = [item for item in values if item not in {SAMPLE_ROUTE_MODE, SAMPLE_ROUTE_MODE, SAMPLE_ROUTE_MODE}]
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
    idle_timeout: float = 3.0,
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
) -> Topology:
    relay_excluded = {dongle_addr} if dongle_addr is not None else set()
    topology = Topology(stale_seconds=None, edge_direction="src_to_neighbor", gateway=gateway, relay_excluded=relay_excluded)
    collected = 0
    attempts = 0
    max_attempts = requests * 2  # 最多尝试次数，防止死循环
    while collected < requests and attempts < max_attempts:
        attempts += 1
        print(f"\r📡 采集 RSSI [{collected+1}/{requests}]...", end="", flush=True)
        try:
            command = client.send_rssi_req()
        except OSError as exc:
            print(f"\n⚠️  {exc}", flush=True)
            raise SystemExit(1) from exc
        logger.event("send_command", command=command.rstrip(), request=collected+1, attempt=attempts)
        messages = _drain_messages_until_quiet(client, logger, idle_timeout=3.0, max_seconds=10.0)
        has_report = False
        for message in messages:
            if isinstance(message, RssiReport):
                has_report = True
                updated = topology.update_from_rssi_report(message)
                logger.event(
                    "topology_update",
                    src=message.src_addr,
                    edges=[asdict(edge) for edge in updated],
                )
                logger.topology_snapshot(topology, f"rssi_req_{collected+1}")
        if has_report:
            collected += 1
        else:
            print(f"\r⚠️  未收到RSSI报告，重试...          ", flush=True)
    print(f"\r✅ RSSI 采集完成 ({collected}次)          ", flush=True)
    logger.event("topology_ready", request=collected, attempts=attempts)
    logger.topology_snapshot(topology, "post_collection")
    return topology


def _combine_gateway_path(gateway_route: list[int], pair_route: list[int]) -> list[int]:
    if not gateway_route or not pair_route:
        return []
    if gateway_route[-1] != pair_route[0]:
        return []
    return gateway_route + pair_route[1:]


def _build_gateway_table(topology: Topology, gateway: int,
                          sources: list[int], route_mode: str) -> dict[int, Route]:
    return {src: topology.route(gateway, src, route_mode=route_mode) for src in sources}


def _build_p2p_table(topology: Topology, sources: list[int],
                      nodes: list[int], route_mode: str) -> dict[tuple[int, int], Route]:
    gateway = topology.gateway if topology.gateway is not None else 0
    graph = topology.graph(route_mode=route_mode)
    graph_no_gw_relay = {k: v for k, v in graph.items() if k != gateway}
    table: dict[tuple[int, int], Route] = {}
    for src in sources:
        for dst in nodes:
            if src == dst:
                continue
            table[(src, dst)] = dijkstra(graph_no_gw_relay, src, dst)
    return table


def _resolve_from_tables(
    gw_table: dict[int, Route],
    p2p_table: dict[tuple[int, int], Route],
    source: int,
    target: int,
) -> tuple[Route, Route, list[int]]:
    _unreach = Route(source, target, [], float("inf"), "unreachable")
    gw_route = gw_table.get(source, Route(source, source, [], float("inf"), "unreachable"))
    p2p_route = p2p_table.get((source, target), _unreach)
    if gw_route.status != "valid" or p2p_route.status != "valid":
        return gw_route, p2p_route, []
    full_path = _combine_gateway_path(gw_route.path, p2p_route.path)
    return gw_route, p2p_route, full_path


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


_TIMEOUT_PENALTY_MS = 3000.0


def _effective_latency_ms(result: RoundResult) -> float | None:
    """实测延时（成功）或超时惩罚值（ACK timeout 按 3s 计），unreachable 返回 None。"""
    if not _counts_as_transmission(result):
        return None
    if result.success:
        return _result_latency_ms(result)
    if result.ack_timeout_s is not None:
        return _TIMEOUT_PENALTY_MS
    return None


def summarize(results: Iterable[RoundResult], nodes: list[int], sources: list[int] | None = None) -> dict:
    result_list = list(results)
    pairs = {}
    pair_keys = sorted({(result.source, result.target) for result in result_list} | set(source_target_pairs(nodes, sources)))
    for source, target in pair_keys:
        pair_results = [result for result in result_list if result.source == source and result.target == target]
        sent_results = [result for result in pair_results if _counts_as_transmission(result)]
        success_results = [result for result in pair_results if result.success and _result_latency_ms(result) is not None]
        latencies = [v for result in sent_results if (v := _effective_latency_ms(result)) is not None]
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
            "gateway_to_source_ms": statistics.fmean(v for r in success_results if (v := r.gateway_to_source_ms) is not None) if any(r.gateway_to_source_ms is not None for r in success_results) else None,
            "gateway_to_target_ms": statistics.fmean(v for r in success_results if (v := r.gateway_to_target_ms) is not None) if any(r.gateway_to_target_ms is not None for r in success_results) else None,
            "point_to_point_ms": statistics.fmean(v for r in success_results if (v := r.point_to_point_ms) is not None) if any(r.point_to_point_ms is not None for r in success_results) else None,
            "total_latency_ms": statistics.fmean(v for r in sent_results if (v := _effective_latency_ms(r)) is not None) if sent_results else None,
            "inference_ms": statistics.fmean(v for r in pair_results if (v := r.inference_ms) is not None) if any(r.inference_ms is not None for r in pair_results) else None,
            "source_to_target_ms": statistics.fmean(v for r in success_results if (v := r.source_to_target_ms) is not None) if any(r.source_to_target_ms is not None for r in success_results) else None,
            "route_mode": last.route_mode if last else SAMPLE_ROUTE_MODE,
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
        latencies = [v for result in sent_results if (v := _effective_latency_ms(result)) is not None]
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
            "gateway_to_source_ms": statistics.fmean(v for r in success_results if (v := r.gateway_to_source_ms) is not None) if any(r.gateway_to_source_ms is not None for r in success_results) else None,
            "gateway_to_target_ms": statistics.fmean(v for r in success_results if (v := r.gateway_to_target_ms) is not None) if any(r.gateway_to_target_ms is not None for r in success_results) else None,
            "point_to_point_ms": statistics.fmean(v for r in success_results if (v := r.point_to_point_ms) is not None) if any(r.point_to_point_ms is not None for r in success_results) else None,
            "total_latency_ms": statistics.fmean(v for r in sent_results if (v := _effective_latency_ms(r)) is not None) if sent_results else None,
            "inference_ms": statistics.fmean(v for r in node_results if (v := r.inference_ms) is not None) if any(r.inference_ms is not None for r in node_results) else None,
            "source_to_target_ms": statistics.fmean(v for r in success_results if (v := r.source_to_target_ms) is not None) if any(r.source_to_target_ms is not None for r in success_results) else None,
            "route_mode": node_results[-1].route_mode if node_results else SAMPLE_ROUTE_MODE,
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
    all_latencies = [v for result in result_list if (v := _effective_latency_ms(result)) is not None]
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


def _write_routes_json(log_path: Path, gw_table: dict[int, Route],
                        p2p_table: dict[tuple[int, int], Route]) -> float:
    json_path = _json_dir(log_path)
    start = time.perf_counter()
    gateway_routes = {
        f"{src:02X}": {"path": r.path, "cost": r.cost, "status": r.status}
        for src, r in sorted(gw_table.items())
    }
    pair_routes = {
        f"{src:02X}:{dst:02X}": {"path": r.path, "cost": r.cost, "status": r.status}
        for (src, dst), r in sorted(p2p_table.items())
    }
    (json_path / "routes.json").write_text(
        json.dumps({"gateway_routes": gateway_routes, "pair_routes": pair_routes},
                   ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    return (time.perf_counter() - start) * 1000.0


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
    phase: int = 1,
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
        phase=phase,
    )
    results.append(result)
    logger.round(result)




def _path_key(path: list[int]) -> tuple[int, ...]:
    return tuple(int(item) for item in path)


def _candidate_paths(graph: dict[int, dict[int, float]], src: int, dst: int, limit: int = 5, avoid_nodes: set[int] | None = None) -> list[list[int]]:
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
        for neighbor, weight in sorted(graph.get(current, {}).items()):
            if neighbor in path_tuple:
                continue
            if avoid_nodes and neighbor in avoid_nodes and neighbor != dst:
                continue
            heapq.heappush(queue, (cost + float(weight), path_tuple + (neighbor,)))
    return paths


def _path_base_cost(graph: dict[int, dict[int, float]], path: list[int]) -> float:
    cost = 0.0
    for src, dst in zip(path[:-1], path[1:]):
        cost += float(graph.get(src, {}).get(dst, 9999.0))
    return cost


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
    route_mode: str,
    reason: str,
    old_topology: Topology | None = None,
) -> tuple[Topology, float, float]:
    logger.event("topology_recollect_start", reason=reason)
    started = time.perf_counter()
    new_topology = collect_topology(client, logger, rssi_seconds, rssi_requests, nodes, gateway, dongle_addr=dongle_addr)
    
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
        f"- Pause count: {summary.get('pause_count', 0)}",
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
    route_mode: str = SAMPLE_ROUTE_MODE,
    optimization_target: dict | None = None,
    sources: list[int] | None = None,
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


def _build_route_rssi_section(log_dir: Path, summary: dict) -> list[str]:
    """每对 (源→目标) 展示合并路径各跳的 RSSI / 权重，便于手动验证算法选路逻辑。"""
    def _bucket(rssi: int) -> float:
        if rssi >= -55: return 4
        if rssi >= -65: return 5
        if rssi >= -75: return 6
        if rssi >= -85: return 12
        if rssi >= -90: return 18
        if rssi >= -95: return 28
        if rssi >= -100: return 45
        if rssi >= -105: return 65
        return 90

    json_dir = log_dir / "原始JSON数据"
    state_path = json_dir / "state.json"
    routes_path = json_dir / "routes.json"
    if not state_path.exists() or not routes_path.exists():
        return []

    with state_path.open(encoding="utf-8") as f:
        state = json.load(f)
    with routes_path.open(encoding="utf-8") as f:
        routes_data = json.load(f)

    # raw_rssi[(a, b)] = b 测量到 a 的信号强度（即 A→B 链路质量，在 B 端测得）
    raw_rssi: dict[tuple[int, int], int] = {
        (e["src"], e["dst"]): e["rssi"] for e in state.get("edges", [])
    }
    gw_routes = routes_data.get("gateway_routes", {})
    pair_routes = routes_data.get("pair_routes", {})

    lines: list[str] = [
        "",
        "## 路由跳步 RSSI 详情",
        "",
        "说明：**gw路径** = 网关→源节点（第一张表），**p2p路径** = 源节点→目标节点（第二张表），**合并路径** = 实际下发的完整路径。",
        "RSSI(→) 表示该方向链路在接收端测得的信号强度；权重 = 两方向分桶值的平均，即 Dijkstra 实际使用的边权。",
        "⚠️ 标记表示组合路径出现环路（某节点在路径中重复出现，固件靠 hop 计数器处理）。",
        "",
        "| 源 | 目标 | gw路径 | p2p路径 | 合并路径 | 跳# | A | B | RSSI(A→B) | RSSI(B→A) | 均值RSSI | 权重 | 备注 |",
        "|---|---|---|---|---|---:|---:|---:|---:|---:|---:|---:|---|",
    ]

    for pair_key, item in sorted(_summary_items(summary).items()):
        src_hex = item.get("source", "00")
        dst_hex = item.get("destination", pair_key)
        combined: list[int] = item.get("last_route", [])
        if not combined:
            continue

        gw_path: list[int] = gw_routes.get(src_hex, {}).get("path", [])
        p2p_path: list[int] = pair_routes.get(f"{src_hex}:{dst_hex}", {}).get("path", [])

        # 标记 gw/p2p 的分段点（gw 最后一个节点 = p2p 第一个节点 = 源节点）
        gw_join = gw_path[-1] if gw_path else None
        gw_str = " → ".join(f"{x:02X}" for x in gw_path) if gw_path else "—"
        p2p_str = " → ".join(f"{x:02X}" for x in p2p_path) if p2p_path else "—"

        has_loop = len(combined) != len(set(combined))
        loop_mark = " ⚠️" if has_loop else ""
        combined_str = " → ".join(f"{x:02X}" for x in combined) + loop_mark

        for hop_idx, (a, b) in enumerate(zip(combined[:-1], combined[1:]), 1):
            rssi_ab = raw_rssi.get((a, b))  # B 听到 A 的强度
            rssi_ba = raw_rssi.get((b, a))  # A 听到 B 的强度

            ab_str = str(rssi_ab) if rssi_ab is not None else "❌"
            ba_str = str(rssi_ba) if rssi_ba is not None else "❌"

            if rssi_ab is not None and rssi_ba is not None:
                avg_rssi = f"{(rssi_ab + rssi_ba) / 2:.1f}"
                weight = f"{(_bucket(rssi_ab) + _bucket(rssi_ba)) / 2:.1f}"
            else:
                avg_rssi = ab_str if rssi_ab is not None else "n/a"
                weight = "n/a"

            # 标注该跳是 gw 段还是 p2p 段（以 gw_join 为分界）
            segment = "gw" if gw_join is not None and b == gw_join else ("p2p" if a == gw_join else ("gw" if hop_idx < len(gw_path) else "p2p"))
            note = f"{segment} 段"
            if gw_join is not None and a == gw_join:
                note = "p2p 段（含源节点）"
            if a == combined[0]:
                note = "gw 段起点"

            if hop_idx == 1:
                lines.append(
                    f"| `{src_hex}` | `{dst_hex}` | `{gw_str}` | `{p2p_str}` | `{combined_str}` |"
                    f" `{hop_idx}` | `{a:02X}` | `{b:02X}` | `{ab_str}` | `{ba_str}` | `{avg_rssi}` | `{weight}` | {note} |"
                )
            else:
                lines.append(
                    f"| | | | | |"
                    f" `{hop_idx}` | `{a:02X}` | `{b:02X}` | `{ab_str}` | `{ba_str}` | `{avg_rssi}` | `{weight}` | {note} |"
                )

    return lines


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
        f"- 算法模式：`{commands.get('route_mode', routing.get('route_mode', SAMPLE_ROUTE_MODE))}`",
        f"- 最优参数组合：`interval={commands['command_interval_s']}s, rssi_requests={commands['rssi_requests']}, route_mode={commands.get('route_mode', routing.get('route_mode', SAMPLE_ROUTE_MODE))}`",
        f"- 发包间隔：`{commands['command_interval_s']}s`",
        f"- 计划轮次：`{total.get('planned_rounds')}`，实际SEND：`{total['sent']}`，成功：`{total['success']}`，ACK timeout：`{total.get('ack_timeout_loss')}`，路由不可达：`{total.get('route_failed')}`，实际丢包率：`{_fmt_rate(total['loss_rate'])}`",
        f"- 暂停次数：`{summary.get('pause_count', 0)}`",
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
        "### 按源节点的拓扑图",
        "",
        "每个源节点的路由拓扑图：",
        "",
        "| 源节点 | 拓扑图 |",
        "|---|---|",
        *[f"| `{source:02X}` | [`拓扑图/源节点{source:02X}.svg`](拓扑图/源节点{source:02X}.svg) |" for source in range(1, 11)],
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

    lines.extend(_build_route_rssi_section(Path(log_dir), summary))

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
            f"| 路由模式 | `{commands.get('route_mode', routing.get('route_mode', SAMPLE_ROUTE_MODE))}` |",
            "",
            "## 路由参数",
            "",
            "| 参数 | 当前值 |",
            "|---|---|",
            f"| 算法 | `{routing['algorithm']}` |",
            f"| 算法模式 | `{routing.get('route_mode', SAMPLE_ROUTE_MODE)}` |",
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


def _write_phase_reports(
    log_path: Path,
    results: list,
    nodes: list[int],
    sources,
    topology,
    hardware_record: dict,
    aligned_metrics: dict,
    summary_config: dict,
) -> None:
    phase_names = {1: "阶段1_暂停前", 2: "阶段2_暂停后"}
    for phase_num, phase_label in phase_names.items():
        phase_results = [r for r in results if getattr(r, "phase", 1) == phase_num]
        if not phase_results:
            continue
        phase_summary = summarize(phase_results, nodes, sources)
        enrich_summary_with_rssi(phase_summary, topology)
        phase_summary["rounds"] = [asdict(r) for r in phase_results]
        phase_summary["config"] = {**summary_config, "phase": phase_num, "phase_label": phase_label}
        phase_summary["log_dir"] = str(log_path)
        enrich_summary_with_metrics(phase_summary, topology)
        report_text = build_readable_report(phase_summary, hardware_record, aligned_metrics, log_path)
        (log_path / f"测试结果汇报_{phase_label}.md").write_text(report_text, encoding="utf-8")


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
    route_mode: str = SAMPLE_ROUTE_MODE,
    optimization_target: dict | None = None,
    sources: list[int] | None = None,
    hop_penalty: float = 0.0,
    enable_congestion: bool = False,
    no_retry: bool = False,
    enable_pause: bool = False,
    dynamic_pause: bool = False,
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
        topology = None
        print("⏳ 等待网络稳定 (10s)...", flush=True)
        _drain_messages(client, logger, 10.0)
        while True:
            topology = collect_topology(client, logger, rssi_seconds, 2, nodes, gateway, dongle_addr=dongle_addr)
            unreachable = [n for n in nodes if topology.route(gateway, n).status != "valid"]
            if not unreachable:
                break
            print(f"⚠️  不可达节点: {[f'0x{n:02X}' for n in unreachable]}", flush=True)
            try:
                choice = input("  [c] 继续测试  [r] 重新采集RSSI  [q] 退出: ").strip().lower()
            except (KeyboardInterrupt, EOFError):
                raise SystemExit(0)
            if choice == 'q':
                raise SystemExit(0)
            elif choice != 'r':
                break
        json_path = _json_dir(log_path)
        save_topology(json_path / "state.json", topology)
        # 构建两张独立路由表
        source_nodes = [s for s, _ in pairs]
        source_nodes = sorted(set(source_nodes))
        route_compute_start = time.perf_counter()
        gw_table = _build_gateway_table(topology, gateway, source_nodes, route_mode)
        p2p_table = _build_p2p_table(topology, source_nodes, nodes, route_mode)
        route_compute_ms = (time.perf_counter() - route_compute_start) * 1000.0
        _write_routes_json(log_path, gw_table, p2p_table)
        logger.event("routes", algorithm_compute_latency_ms=route_compute_ms, route_mode=route_mode)

        max_retries_val = 0 if no_retry else 1
        pause_count = 0
        dongle_dead = False
        if enable_pause:
            from .pause import check_pause_key, wait_for_resume, consume_pause
        total_phases = 2 if dynamic_pause else 1
        for phase in range(1, total_phases + 1):
            if dynamic_pause and phase > 1:
                from .pause import wait_for_dynamic_continue
                wait_for_dynamic_continue(phase - 1, total_phases)
                # 暂停结束后验证串口连接是否还活着
                try:
                    client.ping()
                except Exception:
                    print("⚠️  串口守护进程连接已断开，请重启 serial-daemon 后重新测试", flush=True)
                    break
            phase_offset = (phase - 1) * rounds
            for source, target in pairs:
                if dongle_dead:
                    break
                for round_index in range(phase_offset + 1, phase_offset + rounds + 1):
                    topology_recollected = False

                    # 从预建路由表查询路径（查表计时，作为每轮推理耗时）
                    lookup_start = time.perf_counter()
                    gateway_route, pair_route, full_path = _resolve_from_tables(
                        gw_table, p2p_table, source, target
                    )
                    inference_ms = (time.perf_counter() - lookup_start) * 1000.0
                    route_fallback = False

                    # 单次SEND模式：使用网关到目标的完整路径
                    if gateway_route.status != "valid" or pair_route.status != "valid" or not full_path:
                        print(f"  [{round_index:3d}] {source:02X}→{target:02X}  ⚠️  路由不可达 (gw={gateway_route.status}, p2p={pair_route.status})", flush=True)
                        logger.event("route_unreachable", source=source, target=target,
                                     gateway_route_status=gateway_route.status, pair_route_status=pair_route.status)
                        _record_unreachable_rounds(
                            logger=logger, results=results, source=source, target=target,
                            round_index=round_index, payload=payload, demand=demand,
                            interval=interval, route_mode=route_mode,
                            error="route_unreachable", topology_recollected=topology_recollected,
                            phase=phase,
                        )
                        _drain_messages(client, logger, interval)
                        continue

                    # 发送路径 = full_path (网关→源→目标)
                    send_path = full_path

                    target_success, e2e_latency_ms, ack_seq, ack_ts, send_ts, target_retries, ack_monotonic = _send_and_wait_ack_with_retry(
                        client, logger, topology,
                        dst=target, path=send_path, payload=payload,
                        ack_timeout=ack_timeout, max_retries=max_retries_val, retry_interval=0.5,
                        event_payload={
                            "phase": "gateway_to_target_direct",
                            "source": source, "pair_target": target, "round": round_index,
                            "route": send_path, "cost": pair_route.cost,
                            "demand": demand, "interval_s": interval,
                            "route_mode": route_mode, "send_mode": "single_send",
                        },
                    )

                    # 总延时 = SEND 到 ACK 的端到端时间，不含建图时间
                    e2e_ms = e2e_latency_ms if target_success else None
                    total_latency_ms = e2e_ms

                    success = target_success
                    result = RoundResult(
                        target=target, round_index=round_index, route=send_path,
                        cost=pair_route.cost,
                        command=build_send_command(target, send_path, payload).rstrip(),
                        success=success, latency_ms=e2e_ms, ack_seq=ack_seq,
                        payload=payload, demand=demand, send_ts=send_ts, ack_ts=ack_ts,
                        interval_s=interval,
                        status="success" if success else "target_timeout",
                        error=None if success else "target_timeout",
                        route_mode=route_mode, route_fallback=route_fallback,
                        source=source,
                        gateway_to_source_ms=None, gateway_to_target_ms=e2e_ms,
                        point_to_point_ms=e2e_ms, total_latency_ms=total_latency_ms,
                        inference_ms=inference_ms, source_to_target_ms=e2e_ms,
                        topology_recollected=topology_recollected,
                        ack1_monotonic=None, send2_monotonic=None,
                        phase=phase,
                        ack_timeout_s=ack_timeout,
                    )
                    results.append(result)
                    logger.round(result)
                    p2p_ms = e2e_ms
                    try:
                        _drain_messages(client, logger, interval)
                    except SerialException as exc:
                        logger.event("dongle_disconnect", error=str(exc),
                                     pair=f"{source:02X}->{target:02X}", round=round_index,
                                     completed=len(results))
                        print(f"\n⚠️  Dongle 断开: {exc}", flush=True)
                        print(f"   已完成 {len(results)} 轮，保存已有结果...", flush=True)
                        dongle_dead = True
                        break

                    if enable_pause:
                        if check_pause_key():
                            pause_count += 1
                            logger.event("bench_pause", pause_count=pause_count,
                                         pair=f"{source:02X}->{target:02X}", round=round_index)
                            wait_for_resume(pause_count)
                            consume_pause()
                            old_edges = len(topology.edges)
                            new_topology = None
                            for attempt in range(3):
                                print(f"🔄 重新采集 RSSI (尝试 {attempt+1}/3)...", flush=True)
                                try:
                                    _drain_messages(client, logger, boot_wait)
                                    new_topology = collect_topology(
                                        client, logger, rssi_seconds, rssi_requests,
                                        nodes, gateway, dongle_addr,
                                    )
                                except SerialException as exc:
                                    logger.event("dongle_disconnect", error=str(exc),
                                                 context="pause_resume", completed=len(results))
                                    print(f"\n⚠️  Dongle 断开: {exc}", flush=True)
                                    dongle_dead = True
                                    break
                                unreachable = [n for n in nodes if new_topology.route(gateway, n).status != "valid"]
                                if unreachable:
                                    print(f"⚠️  不可达节点: {[f'0x{n:02X}' for n in unreachable]}", flush=True)
                            if dongle_dead:
                                print(f"   已完成 {len(results)} 轮，保存已有结果...", flush=True)
                                break
                            if new_topology and len(new_topology.edges) > 0:
                                topology = new_topology
                                print(f"✅ 拓扑更新: {old_edges} → {len(topology.edges)} 条边", flush=True)
                            else:
                                print(f"⚠️  RSSI 采集无数据，保留旧拓扑 ({old_edges} 条边)", flush=True)
                            save_topology(json_path / "state.json", topology)
                            routes = routes_to_dict(topology.routes(
                                route_mode=route_mode, hop_penalty=hop_penalty,
                                congestion_penalty=congestion_penalty,
                            ))
                            logger.event("bench_resume", pause_count=pause_count,
                                         edges=len(topology.edges))
                            _drain_messages(client, logger, boot_wait)
                            consume_pause()  # 清空采集期间的残留 stdin
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
        topology = Topology(stale_seconds=None, edge_direction="src_to_neighbor", gateway=gateway, relay_excluded=relay_excluded)
    summary = summarize(results, nodes, sources)
    enrich_summary_with_rssi(summary, topology)
    summary["rounds"] = [asdict(result) for result in results]
    summary["pause_count"] = pause_count
    summary["config"] = {
        "port": port,
        "baud": baud,
        "nodes": nodes,
        "sources": sources,
        "test_pairs": [[source, target] for source, target in source_target_pairs(nodes, sources)],
        "rounds": rounds,
        "matrix_mode": True,
        "latency_policy": "two_send_point_to_point=gateway_to_target_ms-gateway_to_source_ms",        "payload": payload,
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
    )
    (json_path / "hardware_test_record.json").write_text(
        json.dumps(hardware_record, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    write_topology_svg(log_path / "拓扑图.svg", topology, summary)
    write_topology_svgs_by_source(log_path, topology, summary)
    write_text_topology(log_path / "拓扑图.txt", topology, summary, gateway=gateway)
    write_excel_summary(log_path / "测试指标汇总.xlsx", summary, topology, summary["config"])
    write_readable_report(log_path, summary, hardware_record, aligned_metrics)

    if dynamic_pause and results:
        _write_phase_reports(
            log_path=log_path,
            results=results,
            nodes=nodes,
            sources=sources,
            topology=topology,
            hardware_record=hardware_record,
            aligned_metrics=aligned_metrics,
            summary_config=summary["config"],
        )

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
    route_mode: str = SAMPLE_ROUTE_MODE,
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
