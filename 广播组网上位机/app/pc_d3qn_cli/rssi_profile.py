from __future__ import annotations

import json
import statistics
from pathlib import Path


def _edge_key(src: int, dst: int) -> str:
    a, b = sorted((int(src), int(dst)))
    return f"{a:02X}:{b:02X}"


def rssi_to_weight(rssi: float) -> int | None:
    if rssi >= -55:
        return 1
    if rssi >= -65:
        return 3
    if rssi >= -75:
        return 6
    if rssi >= -85:
        return 12
    return None


def rssi_to_capacity(rssi: float) -> float:
    weight = rssi_to_weight(rssi)
    if weight is None:
        return 0.0
    return 200.0 / float(weight)


def rssi_to_packet_loss(rssi: float) -> float:
    if rssi >= -55:
        return 0.015
    if rssi >= -65:
        return 0.04
    if rssi >= -75:
        return 0.08
    if rssi >= -80:
        return 0.16
    if rssi >= -85:
        return 0.28
    return 0.45


def rssi_to_delay(rssi: float) -> float:
    if rssi >= -55:
        return 0.055
    if rssi >= -65:
        return 0.075
    if rssi >= -75:
        return 0.105
    if rssi >= -80:
        return 0.155
    if rssi >= -85:
        return 0.235
    return 0.35


def _read_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def _samples_from_state(state: dict) -> dict[str, list[int]]:
    samples: dict[str, list[int]] = {}
    for edge in state.get("edges", []):
        src = int(edge["src"])
        dst = int(edge["dst"])
        key = _edge_key(src, dst)
        values = [int(value) for value in edge.get("samples", [])] or [int(edge["rssi"])]
        samples.setdefault(key, []).extend(values)
    for sample in state.get("raw_rssi_samples", []):
        key = _edge_key(int(sample["report_src"]), int(sample["neighbor"]))
        samples.setdefault(key, []).append(int(sample["rssi"]))
    return samples


def _samples_from_snapshots(path: Path) -> dict[str, list[int]]:
    samples: dict[str, list[int]] = {}
    if not path.exists():
        return samples
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        record = json.loads(line)
        topology = record.get("topology", {})
        for key, values in _samples_from_state(topology).items():
            samples.setdefault(key, []).extend(values)
    return samples


def build_profile_from_run(run_dir: str | Path) -> dict:
    root = Path(run_dir)
    json_dir = root / "原始JSON数据"
    state_path = json_dir / "state.json"
    samples: dict[str, list[int]] = {}
    if state_path.exists():
        for key, values in _samples_from_state(_read_json(state_path)).items():
            samples.setdefault(key, []).extend(values)
    for key, values in _samples_from_snapshots(json_dir / "topology_snapshots.jsonl").items():
        samples.setdefault(key, []).extend(values)
    if not samples:
        raise ValueError(f"no RSSI samples found under {run_dir}")

    nodes = {0}
    edges = []
    for key, values in sorted(samples.items()):
        src_text, dst_text = key.split(":")
        src = int(src_text, 16)
        dst = int(dst_text, 16)
        nodes.update({src, dst})
        median = float(statistics.median(values))
        mean = float(statistics.fmean(values))
        stddev = float(statistics.pstdev(values)) if len(values) > 1 else 0.0
        edges.append(
            {
                "src": src,
                "dst": dst,
                "samples": values,
                "rssi_median": median,
                "rssi_mean": mean,
                "rssi_min": min(values),
                "rssi_max": max(values),
                "rssi_stddev": stddev,
                "capacity": rssi_to_capacity(median),
                "packet_loss": rssi_to_packet_loss(median),
                "delay": rssi_to_delay(median),
                "queueing_delay": 0.0,
                "field_sources": {
                    "rssi": "real_rssi",
                    "capacity": "derived",
                    "packet_loss": "derived",
                    "delay": "derived",
                    "queueing_delay": "default",
                    "humidity": "default",
                },
            }
        )
    return {
        "schema": "pc_d3qn_cli.hardware_rssi_profile.v1",
        "source_run_dir": str(root),
        "nodes": sorted(nodes),
        "edges": edges,
        "field_sources": {
            "rssi": "real_rssi",
            "capacity": "derived_from_rssi",
            "packet_loss": "derived_from_rssi_segment",
            "delay": "derived_from_rssi_segment",
            "queueing_delay": "default",
            "environment": "default",
        },
    }


def write_profile_from_run(run_dir: str | Path, output: str | Path) -> dict:
    profile = build_profile_from_run(run_dir)
    path = Path(output)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(profile, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return profile
