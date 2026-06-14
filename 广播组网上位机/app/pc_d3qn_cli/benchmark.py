from __future__ import annotations

import json
import statistics
import time
from dataclasses import asdict, dataclass
from datetime import datetime
from pathlib import Path
from typing import Iterable

from .defaults import DEFAULTS
from .model import D3QNDecision, D3QNPredictor, D3QNUnavailable, demand_for_payload
from .protocol import Ack, RssiReport, build_send_command, format_addr, parse_addr
from .reporting import enrich_summary, write_excel_summary, write_report, write_text_topology, write_topology_svg
from .serial_client import RawSerialEvent, SerialClient
from .state import build_d3qn_state, k_candidate_paths, write_d3qn_state
from .topology import Topology, save_topology


@dataclass
class RoundResult:
    source: int
    target: int
    round_index: int
    route: list[int]
    command: str
    success: bool
    latency_ms: float | None
    ack_seq: int | None
    payload: str
    demand: int
    send_ts: str | None
    ack_ts: str | None
    interval_s: float
    status: str
    error: str | None
    selected_action: int | None
    candidate_path_count: int
    model_q_values: list[float]
    gateway_to_source_ms: float | None = None
    gateway_to_target_ms: float | None = None
    point_to_point_ms: float | None = None
    total_latency_ms: float | None = None
    inference_ms: float | None = None
    source_to_target_ms: float | None = None
    d3qn_total_latency_ms: float | None = None
    topology_recollected: bool = False
    path_switch_reason: str | None = None
    all_candidates_degraded: bool = False
    ack1_monotonic: float | None = None
    send2_monotonic: float | None = None
    online_learn_ms: float = 0.0  # 本轮触发在线学习更新的耗时（计入 inference_ms，单独记录供分析）
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
        self.decisions_file = (self.json_dir / "model_decisions.jsonl").open("w", encoding="utf-8")
        self.topology_file = (self.json_dir / "topology_snapshots.jsonl").open("w", encoding="utf-8")

    def close(self) -> None:
        self.raw_file.close()
        self.events_file.close()
        self.rounds_file.close()
        self.decisions_file.close()
        self.topology_file.close()

    def raw_callback(self, event: RawSerialEvent) -> None:
        text = "".join(chr(b) if b in (9, 10, 13) or 32 <= b <= 126 else "." for b in event.data)
        self.raw_file.write(
            f"{datetime.fromtimestamp(event.timestamp).isoformat(timespec='milliseconds')} "
            f"{event.direction.upper()} len={len(event.data)} hex={event.data.hex(' ')} text={text!r}\n"
        )
        self.raw_file.flush()

    def event(self, event_type: str, **payload) -> None:
        record = {"ts": datetime.now().isoformat(timespec="milliseconds"), "type": event_type, **payload}
        self.events_file.write(json.dumps(record, ensure_ascii=False, sort_keys=True) + "\n")
        self.events_file.flush()

    def round(self, result: RoundResult) -> None:
        self.rounds_file.write(json.dumps(asdict(result), ensure_ascii=False, sort_keys=True) + "\n")
        self.rounds_file.flush()

    def decision(self, decision: D3QNDecision, compute_ms: float, extra: dict | None = None) -> None:
        record = {"ts": datetime.now().isoformat(timespec="milliseconds"), "compute_ms": compute_ms, **decision.to_dict()}
        if extra:
            record.update(extra)
        self.decisions_file.write(json.dumps(record, ensure_ascii=False, sort_keys=True) + "\n")
        self.decisions_file.flush()

    def topology_snapshot(self, topology: Topology, label: str) -> None:
        record = {"ts": datetime.now().isoformat(timespec="milliseconds"), "label": label, "topology": topology.to_dict()}
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


def resolve_log_dir(log_dir: str | Path) -> Path:
    path = Path(log_dir)
    return next_run_dir(path) if path.name == "d3qn_hw" else path


def _drain_messages(client: SerialClient, logger: BenchmarkLogger, duration: float, topology: Topology | None = None, label: str = "drain") -> list[object]:
    deadline = time.monotonic() + duration
    messages = []
    while time.monotonic() < deadline:
        for message in client.read_available():
            messages.append(message)
            if isinstance(message, RssiReport):
                logger.event("rssi_report", src=message.src_addr, neighbors=[asdict(neighbor) for neighbor in message.neighbors])
                if topology is not None:
                    updated = topology.update_from_rssi_report(message)
                    logger.event("topology_update", src=message.src_addr, edges=[asdict(edge) for edge in updated])
                    logger.topology_snapshot(topology, label)
            elif isinstance(message, Ack):
                logger.event("ack_observed", src=message.src_addr, seq=message.seq)
    return messages


def _drain_until_quiet(client: SerialClient, logger: BenchmarkLogger, topology: Topology, idle_timeout: float = 1.0, max_seconds: float = 10.0) -> list[object]:
    deadline = time.monotonic() + max_seconds
    idle_deadline = time.monotonic() + idle_timeout
    messages = []
    while time.monotonic() < deadline and time.monotonic() < idle_deadline:
        chunk = client.read_available()
        if chunk:
            idle_deadline = time.monotonic() + idle_timeout
        for message in chunk:
            messages.append(message)
            if isinstance(message, RssiReport):
                logger.event("rssi_report", src=message.src_addr, neighbors=[asdict(neighbor) for neighbor in message.neighbors])
                updated = topology.update_from_rssi_report(message)
                logger.event("topology_update", src=message.src_addr, edges=[asdict(edge) for edge in updated])
            elif isinstance(message, Ack):
                logger.event("ack_observed", src=message.src_addr, seq=message.seq)
    return messages


def collect_topology(client: SerialClient, logger: BenchmarkLogger, rssi_requests: int, nodes: list[int], gateway: int, dongle_addr: int | None = None) -> Topology:
    relay_excluded = {dongle_addr} if dongle_addr is not None else set()
    topology = Topology(stale_seconds=None, edge_direction="src_to_neighbor", gateway=gateway, relay_excluded=relay_excluded)
    collected = 0
    attempts = 0
    max_attempts = rssi_requests * 2
    while collected < rssi_requests and attempts < max_attempts:
        attempts += 1
        print(f"\r📡 采集 RSSI [{collected + 1}/{rssi_requests}]...", end="", flush=True)
        command = client.send_rssi_req()
        logger.event("send_command", command=command.rstrip(), request=collected + 1, attempt=attempts)
        messages = _drain_until_quiet(client, logger, topology, idle_timeout=3.0, max_seconds=10.0)
        has_report = any(isinstance(m, RssiReport) for m in messages)
        logger.topology_snapshot(topology, f"rssi_req_{collected + 1}")
        if has_report:
            collected += 1
        else:
            print(f"\r⚠️  未收到RSSI报告，重试...          ", flush=True)
    print(f"\r✅ RSSI 采集完成 ({collected}次)          ", flush=True)
    logger.event("topology_ready", request=collected, attempts=attempts)
    logger.topology_snapshot(topology, "post_collection")
    return topology


def _latency_stats(values: list[float]) -> dict:
    if not values:
        return {"avg_ms": None, "min_ms": None, "max_ms": None, "p95_ms": None}
    sorted_values = sorted(values)
    p95_index = min(len(sorted_values) - 1, max(0, int(0.95 * len(sorted_values) + 0.999999) - 1))
    return {"avg_ms": statistics.fmean(values), "min_ms": min(values), "max_ms": max(values), "p95_ms": sorted_values[p95_index]}


def _combine_gateway_path(gateway_route: list[int], pair_route: list[int]) -> list[int]:
    if not gateway_route or not pair_route or gateway_route[-1] != pair_route[0]:
        return []
    return list(gateway_route) + list(pair_route[1:])


def _result_latency_ms(result: RoundResult) -> float | None:
    return result.point_to_point_ms if result.point_to_point_ms is not None else result.latency_ms


def _counts_as_transmission(result: RoundResult) -> bool:
    return result.status != "d3qn_route_failed"


def _is_ack_timeout(result: RoundResult) -> bool:
    return _counts_as_transmission(result) and not result.success


_TIMEOUT_PENALTY_MS = 3000.0


def _effective_latency_ms(result: RoundResult) -> float | None:
    """实测延时（成功）或超时惩罚值（ACK timeout 按 3s 计），d3qn_route_failed 返回 None。"""
    if not _counts_as_transmission(result):
        return None
    if result.success:
        return _result_latency_ms(result)
    if result.ack_timeout_s is not None:
        return _TIMEOUT_PENALTY_MS
    return None


def _path_key(path: list[int]) -> tuple[int, ...]:
    return tuple(int(item) for item in path)


def _gateway_route(topology: Topology, gateway: int, source: int) -> list[int]:
    graph = topology.graph()
    if source not in graph and source != gateway:
        return []
    import heapq
    queue = [(0.0, gateway, [gateway])]
    visited = set()
    while queue:
        cost, current, path = heapq.heappop(queue)
        if current in visited:
            continue
        visited.add(current)
        if current == source:
            return path
        for neighbor, weight in graph.get(current, {}).items():
            if neighbor not in visited:
                heapq.heappush(queue, (cost + float(weight), neighbor, path + [neighbor]))
    return []


def _build_gateway_table(topology: Topology, gateway: int, sources: list[int]) -> dict[int, list[int]]:
    return {src: _gateway_route(topology, gateway, src) for src in sources}


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


def _write_runtime_state(log_path: Path, topology: Topology, summary: dict | None = None) -> dict:
    json_path = log_path / "原始JSON数据"
    save_topology(json_path / "state.json", topology)
    state = write_d3qn_state(json_path / "d3qn_state.json", topology, summary)
    routes = {
        key: {
            "src": int(key.split(":")[0], 16),
            "dst": int(key.split(":")[1], 16),
            "candidate_paths": value,
            "source": "d3qn_candidate_paths",
        }
        for key, value in state["candidate_paths"].items()
    }
    (json_path / "routes.json").write_text(json.dumps(routes, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return state



def _recollect_topology(
    *,
    client: SerialClient,
    logger: BenchmarkLogger,
    log_path: Path,
    rssi_requests: int,
    nodes: list[int],
    gateway: int,
    dongle_addr: int | None = None,
    reason: str,
    old_topology: Topology | None = None,
) -> tuple[Topology, float]:
    logger.event("topology_recollect_start", reason=reason)
    started = time.perf_counter()
    new_topology = collect_topology(client, logger, rssi_requests, nodes, gateway, dongle_addr=dongle_addr)
    
    # 拓扑稳定性检查：如果新拓扑质量下降，保留旧拓扑
    if old_topology is not None:
        old_edges = len(old_topology.edges)
        new_edges = len(new_topology.edges)
        old_reachable = sum(1 for node in nodes if len(k_candidate_paths(old_topology.graph(), gateway, node, 1)) > 0)
        new_reachable = sum(1 for node in nodes if len(k_candidate_paths(new_topology.graph(), gateway, node, 1)) > 0)
        
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
    
    _write_runtime_state(log_path, topology)
    elapsed_ms = (time.perf_counter() - started) * 1000.0
    logger.event("topology_recollect_complete", reason=reason, topology_recollect_ms=elapsed_ms)
    return topology, elapsed_ms


def summarize(results: Iterable[RoundResult], nodes: list[int], gateway: int, sources: list[int] | None = None) -> dict:
    result_list = list(results)
    pairs = {}
    pair_keys = sorted({(result.source, result.target) for result in result_list} | set(source_target_pairs(nodes, sources)))
    for source, target in pair_keys:
        pair_results = [result for result in result_list if result.source == source and result.target == target]
        sent_results = [result for result in pair_results if _counts_as_transmission(result)]
        success_results = [result for result in pair_results if result.success and _result_latency_ms(result) is not None]
        sent = len(sent_results)
        success = len(success_results)
        latencies = [v for result in sent_results if (v := _effective_latency_ms(result)) is not None]
        inference_values = [float(result.inference_ms) for result in pair_results if result.inference_ms is not None]
        d3qn_total_values = [
            float(result.d3qn_total_latency_ms) if result.success and result.d3qn_total_latency_ms is not None
            else (_TIMEOUT_PENALTY_MS + (result.inference_ms or 0.0)) if (not result.success and result.ack_timeout_s is not None)
            else None
            for result in sent_results
        ]
        d3qn_total_values = [v for v in d3qn_total_values if v is not None]
        last = pair_results[-1] if pair_results else None
        pairs[f"{source:02X}:{target:02X}"] = {
            "source": f"{source:02X}",
            "destination": f"{target:02X}",
            "sent": sent,
            "success": success,
            "lost": sent - success,
            "loss_rate": (sent - success) / sent if sent else None,
            "latency": _latency_stats(latencies),
            "inference_latency": _latency_stats(inference_values),
            "source_to_target_latency": _latency_stats([float(result.source_to_target_ms) for result in success_results if result.source_to_target_ms is not None]),
            "d3qn_total_latency": _latency_stats(d3qn_total_values),
            "last_route": last.route if last else [],
            "last_action": last.selected_action if last else None,
            "candidate_path_count": last.candidate_path_count if last else 0,
            "gateway_to_source_ms": last.gateway_to_source_ms if last else None,
            "gateway_to_target_ms": last.gateway_to_target_ms if last else None,
            "point_to_point_ms": last.point_to_point_ms if last else None,
            "total_latency_ms": last.total_latency_ms if last else None,
            "inference_ms": last.inference_ms if last else None,
            "source_to_target_ms": last.source_to_target_ms if last else None,
            "d3qn_total_latency_ms": last.d3qn_total_latency_ms if last else None,
            "path_switch_count": len([result for result in pair_results if result.path_switch_reason]),
            "path_switch_reasons": sorted({result.path_switch_reason for result in pair_results if result.path_switch_reason}),
            "recollect_count": len([result for result in pair_results if result.topology_recollected]),
            "d3qn_route_failures": len([result for result in pair_results if result.status == "d3qn_route_failed"]),
            "planned_rounds": len(pair_results),
            "route_failed": len([result for result in pair_results if result.status == "d3qn_route_failed"]),
            "ack_timeout_loss": len([result for result in pair_results if _is_ack_timeout(result)]),
            "route_failed_rate": len([result for result in pair_results if result.status == "d3qn_route_failed"]) / len(pair_results) if pair_results else None,
            "path_rssi": {"route": last.route if last else [], "mean_rssi": None, "min_rssi": None, "source": "unavailable"},
        }
    targets = {}
    for node in nodes:
        node_results = [result for result in result_list if result.target == node]
        sent_results = [result for result in node_results if _counts_as_transmission(result)]
        success_results = [result for result in node_results if result.success and _result_latency_ms(result) is not None]
        sent = len(sent_results)
        success = len(success_results)
        latencies = [v for result in sent_results if (v := _effective_latency_ms(result)) is not None]
        last = node_results[-1] if node_results else None
        targets[f"{node:02X}"] = {
            "source": f"{last.source:02X}" if last else f"{gateway:02X}",
            "destination": f"{node:02X}",
            "sent": sent,
            "success": success,
            "lost": sent - success,
            "loss_rate": (sent - success) / sent if sent else None,
            "latency": _latency_stats(latencies),
            "last_route": last.route if last else [],
            "last_action": last.selected_action if last else None,
            "candidate_path_count": last.candidate_path_count if last else 0,
            "gateway_to_source_ms": last.gateway_to_source_ms if last else None,
            "gateway_to_target_ms": last.gateway_to_target_ms if last else None,
            "point_to_point_ms": last.point_to_point_ms if last else None,
            "total_latency_ms": last.total_latency_ms if last else None,
            "inference_ms": last.inference_ms if last else None,
            "source_to_target_ms": last.source_to_target_ms if last else None,
            "d3qn_total_latency_ms": last.d3qn_total_latency_ms if last else None,
            "inference_latency": _latency_stats([float(result.inference_ms) for result in node_results if result.inference_ms is not None]),
            "source_to_target_latency": _latency_stats([float(result.source_to_target_ms) for result in success_results if result.source_to_target_ms is not None]),
            "d3qn_total_latency": _latency_stats([
                float(r.d3qn_total_latency_ms) if r.success and r.d3qn_total_latency_ms is not None
                else (_TIMEOUT_PENALTY_MS + (r.inference_ms or 0.0)) if (not r.success and r.ack_timeout_s is not None)
                else None
                for r in sent_results
                if (r.success and r.d3qn_total_latency_ms is not None) or (not r.success and r.ack_timeout_s is not None)
            ]),
            "path_switch_count": len([result for result in node_results if result.path_switch_reason]),
            "recollect_count": len([result for result in node_results if result.topology_recollected]),
            "d3qn_route_failures": len([result for result in node_results if result.status == "d3qn_route_failed"]),
            "planned_rounds": len(node_results),
            "route_failed": len([result for result in node_results if result.status == "d3qn_route_failed"]),
            "ack_timeout_loss": len([result for result in node_results if _is_ack_timeout(result)]),
            "route_failed_rate": len([result for result in node_results if result.status == "d3qn_route_failed"]) / len(node_results) if node_results else None,
            "path_rssi": {"route": last.route if last else [], "mean_rssi": None, "min_rssi": None, "source": "unavailable"},
        }
    total_sent = len([result for result in result_list if _counts_as_transmission(result)])
    total_success = len([result for result in result_list if result.success])
    all_latencies = [v for result in result_list if (v := _effective_latency_ms(result)) is not None]
    all_inference = [float(result.inference_ms) for result in result_list if result.inference_ms is not None]
    all_source_to_target = [float(result.source_to_target_ms) for result in result_list if result.success and result.source_to_target_ms is not None]
    all_d3qn_total = [
        float(result.d3qn_total_latency_ms) if result.success and result.d3qn_total_latency_ms is not None
        else (result.ack_timeout_s * 1000.0 + (result.inference_ms or 0.0)) if (_counts_as_transmission(result) and not result.success and result.ack_timeout_s is not None)
        else None
        for result in result_list
    ]
    all_d3qn_total = [v for v in all_d3qn_total if v is not None]
    all_learn = [float(result.online_learn_ms) for result in result_list if result.online_learn_ms > 0]
    learn_events = [
        {"round_index": result.round_index, "source": result.source, "target": result.target, "ms": float(result.online_learn_ms)}
        for result in result_list if result.online_learn_ms > 0
    ]
    return {
        "total": {
            "sent": total_sent,
            "success": total_success,
            "lost": total_sent - total_success,
            "loss_rate": (total_sent - total_success) / total_sent if total_sent else None,
            "latency": _latency_stats(all_latencies),
            "inference_latency": _latency_stats(all_inference),
            "online_learn_latency": {
                "update_count": len(all_learn),
                "total_ms": sum(all_learn),
                "avg_per_update_ms": sum(all_learn) / len(all_learn) if all_learn else None,
                "amortized_per_round_ms": sum(all_learn) / len(result_list) if result_list else None,
                "events": learn_events,
            },
            "source_to_target_latency": _latency_stats(all_source_to_target),
            "d3qn_total_latency": _latency_stats(all_d3qn_total),
            "planned_rounds": len(result_list),
            "route_failed": len([result for result in result_list if result.status == "d3qn_route_failed"]),
            "ack_timeout_loss": len([result for result in result_list if _is_ack_timeout(result)]),
            "route_failed_rate": len([result for result in result_list if result.status == "d3qn_route_failed"]) / len(result_list) if result_list else None,
        },
        "targets": targets,
        "pairs": pairs,
        "d3qn_route_failures": len([result for result in result_list if result.status == "d3qn_route_failed"]),
        "path_switch_count": len([result for result in result_list if result.path_switch_reason]),
        "topology_recollect_count": len([result for result in result_list if result.topology_recollected]),
    }


def build_hardware_record(summary: dict, topology: Topology, config: dict, checkpoint: str) -> dict:
    return {
        "schema": "d3qn_hardware_test_record.v1",
        "algorithm": "D3QN_MPNN",
        "test_config": {
            "serial": {
                "port": config["port"],
                "baud": config["baud"],
                "data_bits": 8,
                "parity": "N",
                "stop_bits": 1,
                "dtr": False,
                "rts": False,
            },
            "nodes": {
                "gateway": format_addr(config["gateway"]),
                "sources": [format_addr(node) for node in (config.get("sources") or config["nodes"])],
                "targets": [format_addr(node) for node in config["nodes"]],
            },
            "commands": {
                "rssi_command": "RSSI_REQ",
                "send_format": "SEND <dst> <path_len> <path...> <hex_payload>",
                "ack_format": "ACK <dst> <seq>",
                "payload_hex": config["payload"],
                "rounds_per_target": config["rounds"],
                "rounds_per_source_target_pair": config["rounds"],
                "ack_timeout_s": config["ack_timeout"],
                "command_interval_s": config["interval"],
                "rssi_requests": config["rssi_requests"],
                "matrix_mode": "source in sources, target in nodes-source",
                "two_send_latency_policy": "first SEND gateway->source, second SEND gateway->source->target; point_to_point_ms=second_ack_ms-first_ack_ms",
            },
        },
        "routing_params": {
            "algorithm": "D3QN_MPNN",
            "checkpoint": checkpoint,
            "k_paths": DEFAULTS.k_paths,
            "edge_direction": topology.edge_direction,
            "fallback_policy": "no Dijkstra fallback; D3QN route failures are reported separately and excluded from ACK timeout loss",
        },
        "result_summary": summary,
        "topology_summary": topology.to_dict(),
        "field_sources": {
            "real_serial": "raw TX/RX serial bytes",
            "real_rssi": "RSSI_REQ and RSSI_REPORT",
            "real_ack": "SEND followed by ACK or timeout",
            "derived": "computed from real benchmark records",
            "default": "not directly measurable on current hardware",
        },
    }


def run_benchmark(
    port: str,
    baud: int,
    nodes: list[int],
    rounds: int,
    payload: str,
    log_dir: str | Path,
    checkpoint: str | Path,
    boot_wait: float = 5.0,
    rssi_requests: int = 5,
    ack_timeout: float = 2.0,
    interval: float = 0.5,
    gateway: int = 0x00,
    dongle_addr: int | None = None,
    sources: list[int] | None = None,
    enable_online_learn: bool = False,
    online_interval: int = 50,
    online_lr: float = 1e-4,
    online_epochs: int = 2,
    online_nudge: float = 0.5,
    enable_pause: bool = False,
    dynamic_pause: bool = False,
) -> dict:
    log_path = resolve_log_dir(log_dir)
    logger = BenchmarkLogger(log_path)
    client: SerialClient | None = None
    predictor = D3QNPredictor(checkpoint)
    results: list[RoundResult] = []
    topology: Topology | None = None
    model_compute_values: list[float] = []
    graph_build_ms: float = 0.0
    demand = demand_for_payload(payload)
    try:
        client = SerialClient(port, baud, raw_callback=logger.raw_callback)
        pairs = source_target_pairs(nodes, sources)
        logger.event(
            "bench_start",
            algorithm="D3QN_MPNN",
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
        if boot_wait > 0:
            print(f"⏳ 等待网络稳定 ({boot_wait:.0f}s)...", flush=True)
        _drain_messages(client, logger, boot_wait)
        while True:
            topology = collect_topology(client, logger, rssi_requests, nodes, gateway, dongle_addr=dongle_addr)
            missing = set(nodes) - set(topology.nodes)
            if not missing:
                missing_hex: list[str] = []
                break
            missing_hex = [f"0x{n:02X}" for n in sorted(missing)]
            print(f"⚠️  不可达节点: {missing_hex}", flush=True)
            try:
                choice = input("  [c] 继续测试  [r] 重新采集RSSI  [q] 退出: ").strip().lower()
            except (KeyboardInterrupt, EOFError):
                raise SystemExit(0)
            if choice == 'q':
                raise SystemExit(0)
            elif choice != 'r':
                break
        source_nodes = sorted({src for src, _ in pairs})
        gw_table = _build_gateway_table(topology, gateway, source_nodes)
        _write_runtime_state(log_path, topology)
        if missing_hex:
            logger.event("route_table_incomplete", missing=missing_hex, expected=len(nodes), actual=len(topology.nodes))
        try:
            metadata = predictor.validate_topology(topology)
            logger.event("d3qn_checkpoint_preflight_ok", **metadata)
        except D3QNUnavailable as exc:
            logger.event("d3qn_checkpoint_preflight_failed", error=str(exc), checkpoint=str(checkpoint))
            raise RuntimeError(f"D3QN checkpoint preflight failed: {exc}") from exc

        # 预建拓扑缓存：第一次建图耗时（betweenness + edge features）不计入任何轮次的推理时间
        graph_build_start = time.perf_counter()
        predictor._refresh_topo_cache(topology)
        graph_build_ms = (time.perf_counter() - graph_build_start) * 1000.0
        logger.event("d3qn_graph_build_complete", graph_build_ms=graph_build_ms)

        online_learner = None
        if enable_online_learn:
            from .online_learn import OnlineD3QNLearner
            online_learner = OnlineD3QNLearner(
                predictor,
                save_dir=log_path,
                base_checkpoint=checkpoint,
                interval=online_interval,
                lr=online_lr,
                epochs=online_epochs,
                nudge_step=online_nudge,
                latency_norm_ms=max(1000.0, ack_timeout * 1000.0),
                logger=logger,
            )
            logger.event(
                "d3qn_online_learn_enabled",
                interval=online_interval, lr=online_lr, epochs=online_epochs,
                nudge_step=online_nudge, base_checkpoint=str(checkpoint),
            )

        total_phases = 2 if dynamic_pause else 1
        for phase in range(1, total_phases + 1):
            if dynamic_pause and phase > 1:
                from .pause import wait_for_dynamic_continue
                wait_for_dynamic_continue(phase - 1, total_phases)
            phase_offset = (phase - 1) * rounds
            for source, target in pairs:
                for round_index in range(phase_offset + 1, phase_offset + rounds + 1):
                    topology_recollected = False

                    decision_started = time.perf_counter()
                    try:
                        decision = predictor.decide(topology, source, target, demand)
                    except D3QNUnavailable as exc:
                        decision = D3QNDecision("model_unavailable", source, target, demand, None, [], [], [], str(checkpoint), str(exc))
                    compute_ms = (time.perf_counter() - decision_started) * 1000.0
                    model_compute_values.append(compute_ms)

                    if not decision.success:
                        logger.decision(decision, compute_ms, {"path_switch_reason": "d3qn_route_failed"})
                        result = RoundResult(
                            source=source,
                            target=target,
                            round_index=round_index,
                            route=[],
                            command="",
                            success=False,
                            latency_ms=None,
                            ack_seq=None,
                            payload=payload,
                            demand=demand,
                            send_ts=None,
                            ack_ts=None,
                            interval_s=interval,
                            status="d3qn_route_failed",
                            error=decision.error or decision.status,
                            selected_action=decision.selected_action,
                            candidate_path_count=len(decision.candidate_paths),
                            model_q_values=decision.q_values,
                            inference_ms=compute_ms,
                            topology_recollected=topology_recollected,
                            path_switch_reason="d3qn_route_failed",
                            phase=phase,
                        )
                        results.append(result)
                        logger.round(result)
                        _drain_messages(client, logger, interval, topology, f"failed_{target:02X}_{round_index}")
                        continue

                    gateway_route = gw_table.get(source, [])
                    # 纯 D3QN 决策：直接用 argmax(Q)，与 sample 评估模式一致
                    selected_action = decision.selected_action if decision.selected_action is not None else 0
                    selected_path = decision.selected_path
                    path_switch_reason = None
                    all_candidates_degraded = False
                    logger.decision(
                        decision,
                        compute_ms,
                        {
                            "selected_runtime_action": selected_action,
                            "selected_runtime_path": selected_path,
                            "path_switch_reason": path_switch_reason,
                            "all_candidates_degraded": all_candidates_degraded,
                        },
                    )

                    # 单次SEND模式：使用网关到目标的完整路径
                    full_path = _combine_gateway_path(gateway_route, selected_path)
                    if not gateway_route or not selected_path or not full_path:
                        result = RoundResult(
                            source=source,
                            target=target,
                            round_index=round_index,
                            route=full_path or selected_path or [],
                            command="",
                            success=False,
                            latency_ms=None,
                            ack_seq=None,
                            payload=payload,
                            demand=demand,
                            send_ts=None,
                            ack_ts=None,
                            interval_s=interval,
                            status="d3qn_route_failed",
                            error="gateway_to_target_unreachable",
                            selected_action=selected_action,
                            candidate_path_count=len(decision.candidate_paths),
                            model_q_values=decision.q_values,
                            inference_ms=compute_ms,
                            topology_recollected=topology_recollected,
                            path_switch_reason=path_switch_reason or "gateway_route_missing",
                            all_candidates_degraded=all_candidates_degraded,
                            phase=phase,
                        )
                        results.append(result)
                        logger.round(result)
                        _drain_messages(client, logger, interval, topology, f"failed_{source:02X}_{target:02X}_{round_index}")
                        continue

                    # 发送路径 = full_path (网关→源→目标)
                    send_path = full_path
                
                    # 单次SEND：网关直接到目标
                    target_success, e2e_latency_ms, ack_seq, ack_ts, send_ts, ack_monotonic = _send_and_wait_ack(
                        client,
                        logger,
                        topology,
                        dst=target,
                        path=send_path,
                        payload=payload,
                        ack_timeout=ack_timeout,
                        event_payload={
                            "phase": "gateway_to_target_direct",
                            "source": source,
                            "pair_target": target,
                            "round": round_index,
                            "route": send_path,
                            "selected_action": selected_action,
                            "q_values": decision.q_values,
                            "demand": demand,
                            "send_mode": "single_send",
                        },
                    )

                    # 推理时间 = 模型推理耗时
                    inference_ms = compute_ms
                
                    # 端到端延时 = SEND到ACK时间
                    e2e_ms = e2e_latency_ms if target_success else None
                
                    # 总延时 = 推理时间 + 端到端延时
                    total_latency_ms = None
                    if inference_ms is not None and e2e_ms is not None:
                        total_latency_ms = inference_ms + e2e_ms

                    success = target_success
                    point_to_point_ms = e2e_ms
                    result = RoundResult(
                        source=source,
                        target=target,
                        round_index=round_index,
                        route=send_path,
                        command=build_send_command(target, send_path, payload).rstrip(),
                        success=success,
                        latency_ms=e2e_ms,
                        ack_seq=ack_seq,
                        payload=payload,
                        demand=demand,
                        send_ts=send_ts,
                        ack_ts=ack_ts,
                        interval_s=interval,
                        status="success" if success else "target_timeout",
                        error=None if success else "target_timeout",
                        selected_action=selected_action,
                        candidate_path_count=len(decision.candidate_paths),
                        model_q_values=decision.q_values,
                        gateway_to_source_ms=None,
                        gateway_to_target_ms=e2e_ms,
                        point_to_point_ms=e2e_ms,
                        total_latency_ms=total_latency_ms,
                        inference_ms=inference_ms,
                        source_to_target_ms=e2e_ms,
                        d3qn_total_latency_ms=total_latency_ms,
                        topology_recollected=topology_recollected,
                        path_switch_reason=path_switch_reason,
                        all_candidates_degraded=all_candidates_degraded,
                        ack1_monotonic=None,
                        send2_monotonic=None,
                        phase=phase,
                        ack_timeout_s=ack_timeout,
                    )
                    if online_learner is not None:
                        # 记录本轮经验，到点触发学习更新；更新耗时计入 inference/总延时，并单独记录在 online_learn_ms
                        online_learner.record(result.success, _result_latency_ms(result))
                        update_ms = online_learner.maybe_update()
                        if update_ms > 0.0:
                            result.online_learn_ms = update_ms
                            if result.inference_ms is not None:
                                result.inference_ms += update_ms
                            if result.total_latency_ms is not None:
                                result.total_latency_ms += update_ms
                            if result.d3qn_total_latency_ms is not None:
                                result.d3qn_total_latency_ms += update_ms
                    results.append(result)
                    logger.round(result)
                    # 成功收到目标 ACK 后无需继续 drain（后续 ACK 都是中继/旁路帧，记录无意义）
                    # 只在失败时 drain 清缓冲区，避免残留帧干扰下一轮
                    if not result.success:
                        _drain_messages(client, logger, interval, topology, f"post_round_{source:02X}_{target:02X}_{round_index}")
                    # 键盘控制：s=暂停/恢复，y=重采 RSSI 后继续
                    if enable_pause:
                        from .pause import poll_key, wait_for_key
                        key = poll_key()
                        if key == "y" or key == "s":
                            if key == "s":
                                print(f"\n⏸  已暂停（轮次 {len(results)}）。按 s 继续，按 y 重采 RSSI 后继续...", flush=True)
                                key = wait_for_key()
                            if key == "y":
                                print("📡  重新采集 RSSI...", flush=True)
                                logger.event("pause_rssi_collect", completed_rounds=len(results))
                                new_topo = collect_topology(client, logger, rssi_requests, nodes, gateway, dongle_addr=dongle_addr)
                                topology = new_topo
                                predictor._cached_graph = None  # 拓扑变了，下轮重建缓存
                                _write_runtime_state(log_path, topology)
                                logger.event("pause_rssi_complete", completed_rounds=len(results))
                                print("▶  RSSI 已更新，继续测试...", flush=True)
                            else:
                                print("▶  继续测试...", flush=True)
    except Exception as exc:
        logger.event("bench_error", error=str(exc))
        (log_path / "bench_error.json").write_text(
            json.dumps(
                {
                    "ts": datetime.now().isoformat(timespec="milliseconds"),
                    "algorithm": "D3QN_MPNN",
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
    summary = summarize(results, nodes, gateway, sources)
    summary["rounds"] = [asdict(result) for result in results]
    summary["config"] = {
        "algorithm": "D3QN_MPNN",
        "checkpoint": str(checkpoint),
        "port": port,
        "baud": baud,
        "nodes": nodes,
        "sources": sources,
        "test_pairs": [[source, target] for source, target in source_target_pairs(nodes, sources)],
        "rounds": rounds,
        "matrix_mode": True,
        "latency_policy": "two_send_point_to_point=gateway_to_target_ms-gateway_to_source_ms",
        "d3qn_total_latency_policy": "inference_ms + point_to_point_ms",
        "payload": payload,
        "gateway": gateway,
        "ack_timeout": ack_timeout,
        "interval": interval,
        "rssi_requests": rssi_requests,
        "graph_build_ms": graph_build_ms,
        "log_dir": str(log_path),
        "defaults": DEFAULTS.to_dict(),
        "field_sources": {
            "rssi": "real_rssi",
            "ack_latency": "real_ack",
            "demand": "derived",
            "d3qn_unmeasured_params": "default",
        },
    }
    model_compute_ms = statistics.fmean(model_compute_values) if model_compute_values else None
    enrich_summary(summary, topology, model_compute_ms=model_compute_ms)
    json_path = log_path / "原始JSON数据"
    _write_runtime_state(log_path, topology, summary)
    (json_path / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    hardware_record = build_hardware_record(summary, topology, summary["config"], str(checkpoint))
    (json_path / "hardware_test_record.json").write_text(json.dumps(hardware_record, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    write_topology_svg(log_path / "拓扑图.svg", topology, summary, gateway=gateway)
    write_text_topology(log_path / "拓扑图.txt", topology, summary, gateway=gateway)
    write_excel_summary(log_path / "测试指标汇总.xlsx", summary, topology, summary["config"])
    write_report(log_path / "测试结果汇报.md", summary, hardware_record)

    if dynamic_pause and results:
        phase_names = {1: "阶段1_暂停前", 2: "阶段2_暂停后"}
        for phase_num, phase_label in phase_names.items():
            phase_results = [r for r in results if getattr(r, "phase", 1) == phase_num]
            if not phase_results:
                continue
            phase_summary = summarize(phase_results, nodes, gateway, sources)
            phase_summary["rounds"] = [asdict(r) for r in phase_results]
            enrich_summary(phase_summary, topology, model_compute_ms=statistics.fmean(model_compute_values) if model_compute_values else None)
            phase_summary["config"] = {**summary["config"], "phase": phase_num, "phase_label": phase_label}
            phase_summary["log_dir"] = str(log_path)
            phase_hw = build_hardware_record(phase_summary, topology, phase_summary["config"], str(checkpoint))
            write_report(log_path / f"测试结果汇报_{phase_label}.md", phase_summary, phase_hw)

    summary["log_dir"] = str(log_path)
    return summary
