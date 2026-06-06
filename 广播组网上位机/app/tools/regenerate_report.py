#!/usr/bin/env python3
"""Regenerate test reports (拓扑图.svg, 测试指标汇总.xlsx, 测试结果汇报.md) from raw data."""
import json
import sys
from dataclasses import asdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

from pc_dijkstra_cli.benchmark import (
    RoundResult, summarize, enrich_summary_with_rssi, enrich_summary_with_metrics,
    write_report, write_readable_report, build_simulation_aligned_metrics,
    build_simulation_aligned_rows, build_hardware_test_record,
    BASELINE_ROUTE_MODE,
)
from pc_dijkstra_cli.reporting import write_excel_summary
from pc_dijkstra_cli.topology import Topology, Edge
from pc_dijkstra_cli.topology_svg import write_topology_svg


def regenerate(test_dir: Path):
    json_dir = test_dir / "原始JSON数据"
    if not json_dir.exists():
        print(f"ERROR: {json_dir} not found")
        return

    # Read events.jsonl for config
    config = {}
    with (json_dir / "events.jsonl").open() as f:
        for line in f:
            rec = json.loads(line)
            if rec.get("type") == "bench_start":
                config = rec
                break

    nodes = config.get("nodes", list(range(1, 11)))
    sources = config.get("sources")
    port = config.get("port", "/dev/ttyUSB0")
    baud = config.get("baud", 115200)
    rounds = config.get("rounds", 1)
    payload = config.get("payload", "AABBCC")
    ack_timeout = config.get("ack_timeout", 5.0)
    interval = config.get("interval", 2.0)
    route_mode = config.get("route_mode", "sample_dijkstra")

    # Read rounds.jsonl → RoundResult list
    results = []
    with (json_dir / "rounds.jsonl").open() as f:
        for line in f:
            d = json.loads(line)
            # Remove keys not in RoundResult
            valid_keys = {fld.name for fld in RoundResult.__dataclass_fields__.values()}
            filtered = {k: v for k, v in d.items() if k in valid_keys}
            results.append(RoundResult(**filtered))

    print(f"  Loaded {len(results)} round results, {len(nodes)} nodes, mode={route_mode}")

    # Reconstruct Topology from state.json + topology_snapshots.jsonl
    state = json.loads((json_dir / "state.json").read_text())
    topology = Topology(
        stale_seconds=state.get("stale_seconds"),
        edge_direction=state.get("edge_direction", "src_to_neighbor"),
        gateway=state.get("gateway", 0),
        relay_excluded=set(state.get("relay_excluded", [])),
        min_rssi=state.get("min_rssi", -85),
    )
    # Load edges from the last topology snapshot
    topo_file = json_dir / "topology_snapshots.jsonl"
    if topo_file.exists():
        last_topo = None
        with topo_file.open() as f:
            for line in f:
                rec = json.loads(line)
                if "topology" in rec:
                    last_topo = rec["topology"]
        if last_topo:
            for edge_data in last_topo.get("edges", []):
                src = edge_data["src"]
                dst = edge_data["dst"]
                key = f"{src}:{dst}"
                topology.edges[key] = Edge(
                    src=src, dst=dst,
                    rssi=edge_data["rssi"],
                    weight=edge_data.get("weight", 0),
                    updated_at=edge_data.get("updated_at", 0),
                    direction=edge_data.get("direction", "src_to_neighbor"),
                    source=edge_data.get("source", "real_rssi"),
                    samples=edge_data.get("samples"),
                )
                topology.nodes.add(src)
                topology.nodes.add(dst)

    # Build summary
    summary = summarize(results, nodes, sources)
    enrich_summary_with_rssi(summary, topology)
    summary["rounds"] = [asdict(r) for r in results]
    summary["config"] = {
        "port": port, "baud": baud, "nodes": nodes, "sources": sources,
        "test_pairs": [[s, t] for s in (sources or [0]) for t in nodes if t != s],
        "rounds": rounds, "matrix_mode": True,
        "payload": payload, "gateway": 0, "ack_timeout": ack_timeout,
        "interval": interval, "route_mode": route_mode,
        "log_dir": str(test_dir),
    }
    enrich_summary_with_metrics(summary, topology)
    summary["log_dir"] = str(test_dir)

    # Write reports
    write_report(test_dir, summary, topology)

    aligned = build_simulation_aligned_metrics(summary, test_dir)
    rows = build_simulation_aligned_rows(summary, results, ack_timeout)
    hw_record = build_hardware_test_record(
        summary=summary, topology=topology, port=port, baud=baud,
        nodes=nodes, rounds=rounds, payload=payload, log_dir=test_dir,
        boot_wait=2, rssi_seconds=20, rssi_requests=10,
        ack_timeout=ack_timeout, interval=interval, gateway=0,
        route_mode=route_mode, optimization_target=None, sources=sources,
    )
    write_readable_report(test_dir, summary, hw_record, aligned)
    write_topology_svg(test_dir / "拓扑图.svg", topology, summary)
    write_excel_summary(test_dir / "测试指标汇总.xlsx", summary, topology, summary["config"])

    total = summary["total"]
    print(f"  Loss: {total['loss_rate']:.2%}  Sent: {total['sent']}  Success: {total['success']}")
    print(f"  Reports written to {test_dir}")


if __name__ == "__main__":
    base = Path("/home/sueiny/rk3506_linux6.1_v1.2.0/app/广播组网上位机/app/logs/dijkstra_hw")
    for name in sys.argv[1:] or ["第140次测试", "第141次测试"]:
        test_dir = base / name
        print(f"=== {name} ===")
        regenerate(test_dir)
