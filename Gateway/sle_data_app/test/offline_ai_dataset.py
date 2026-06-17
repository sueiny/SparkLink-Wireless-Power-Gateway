#!/usr/bin/env python3
"""
Generate offline-AI training data and replayable ST frames for the two-root
Gateway topology.

PC usage:
    py -3 offline_ai_dataset.py --hours 24 --out-dir ai_dataset

Outputs:
    training.csv        Feature rows for model training.
    labels.jsonl        Labeled samples with raw telemetry values.
    replay.jsonl        Replay records consumable by dtu_root_run_sender.py.
"""

import argparse
import csv
import json
import math
from pathlib import Path
from typing import Dict, Iterable, List, Tuple

import dtu_root_run_sender as st

RISK_TYPES = [
    "meter_voltage_risk",
    "meter_current_risk",
    "meter_energy_risk",
    "env_risk",
    "dtu_stability_risk",
    "relay_state_risk",
]

FEATURE_COLUMNS = [
    "voltage",
    "current",
    "active_power",
    "power_factor",
    "frequency",
    "energy",
    "temperature",
    "humidity",
    "relay_state",
    "online",
    "voltage_deviation_ratio",
    "frequency_deviation_hz",
    "power_factor_drop",
    "voltage_slope_per_min",
    "current_slope_per_min",
    "temperature_slope_per_min",
    "humidity_slope_per_min",
    "energy_delta",
    "energy_freeze",
    "sample_gap_ms",
    "sample_gap_ratio",
]

SCENARIOS = [
    "normal",
    "daily_peak",
    "voltage_sag",
    "voltage_swell",
    "over_current_gradual",
    "current_surge",
    "power_factor_drop",
    "energy_jump_or_freeze",
    "high_temp",
    "high_humidity",
    "sensor_stuck",
    "relay_state_anomaly",
    "dtu_offline",
    "dtu_jitter",
]


def root_for_node(node_id: int) -> int:
    current = node_id
    seen = set()
    while current and current not in seen:
        seen.add(current)
        parent, _ = st.TOPOLOGY_NODES.get(current, (0, ()))
        if parent == 0:
            return current
        current = parent
    return 1 if node_id <= 11 else 12


def u16be(value: int) -> bytes:
    return st.u16be(max(0, min(0xFFFF, value)))


def u32be(value: int) -> bytes:
    return st.u32be(max(0, min(0xFFFFFFFF, value)))


def modbus_crc(frame: bytes) -> bytes:
    crc = st.crc16_modbus(frame)
    return bytes([crc & 0xFF, (crc >> 8) & 0xFF])


def build_meter_rtu(values: Dict[str, float], slave_addr: int = 1) -> bytes:
    data = (
        u16be(int(values["voltage"] * 10)) +
        u16be(int(values["current"] * 100)) +
        u16be(int(values["active_power"])) +
        u16be(int(values["power_factor"] * 1000)) +
        u16be(int(values["frequency"] * 100)) +
        u32be(int(values["energy"] * 100)) +
        u16be(0x55 if values.get("relay_status", 1) else 0x00)
    )
    frame = bytes([slave_addr, 0x04, len(data)]) + data
    return frame + modbus_crc(frame)


def build_env_rtu(values: Dict[str, float], slave_addr: int = 1) -> bytes:
    data = u16be(int(values["humidity"] * 10)) + u16be(int(values["temperature"] * 10))
    frame = bytes([slave_addr, 0x03, len(data)]) + data
    return frame + modbus_crc(frame)


def build_relay_rtu(values: Dict[str, float], slave_addr: int = 1) -> bytes:
    coil_byte = 0x01 if int(values["relay_state"]) else 0x00
    frame = bytes([slave_addr, 0x01, 0x01, coil_byte])
    return frame + modbus_crc(frame)


def build_device_frame(device: Dict[str, object], seq: int, values: Dict[str, float]) -> bytes:
    kind = str(device["kind"])
    if kind == "meter":
        rtu = build_meter_rtu(values, int(device["modbus_addr"]))
    elif kind == "env":
        rtu = build_env_rtu(values, int(device["modbus_addr"]))
    elif kind == "relay":
        rtu = build_relay_rtu(values, int(device["modbus_addr"]))
    else:
        raise ValueError(f"unsupported device kind: {kind}")

    payload = bytes([int(device["modbus_type"]), len(rtu)]) + rtu
    dtu_id = int(device["dtu_id"])
    return st.build_sle_frame(
        st.SLE_FRAME_TYPE_DATA,
        st.dtu_role(dtu_id),
        dtu_id,
        st.DEFAULT_DST_NODE,
        seq,
        payload,
    )


def base_values(device: Dict[str, object], sample_index: int, interval_sec: int) -> Dict[str, float]:
    dtu_id = int(device["dtu_id"])
    day_phase = (sample_index % max(1, int(86400 / interval_sec))) * interval_sec / 86400.0
    daily = math.sin(day_phase * math.tau)
    kind = str(device["kind"])

    if kind == "meter":
        voltage = 220.0 + daily * 2.5 + (dtu_id % 3) * 0.4
        current = 4.0 + max(0.0, daily) * 6.0 + dtu_id * 0.25
        return {
            "voltage": voltage,
            "current": current,
            "active_power": voltage * current * 0.95,
            "power_factor": 0.96,
            "frequency": 50.0 + daily * 0.03,
            "energy": 1000.0 + dtu_id * 10.0 + sample_index * current * interval_sec / 3600.0,
            "relay_status": 1.0,
        }

    if kind == "env":
        return {
            "temperature": 28.0 + daily * 4.0 + (dtu_id % 2),
            "humidity": 60.0 + daily * 10.0,
        }

    return {"relay_state": 1.0}


def apply_scenario(values: Dict[str, float],
                   kind: str,
                   scenario: str,
                   sample_index: int,
                   total_samples: int) -> Tuple[Dict[str, float], List[str], str]:
    result = dict(values)
    risks: List[str] = []
    level = "normal"
    progress = sample_index / max(1, total_samples - 1)

    if kind == "meter":
        if scenario == "daily_peak":
            result["current"] *= 1.45
        elif scenario == "voltage_sag":
            result["voltage"] = 185.0 + progress * 10.0
            risks.append("meter_voltage_risk")
            level = "high"
        elif scenario == "voltage_swell":
            result["voltage"] = 236.0 + progress * 12.0
            risks.append("meter_voltage_risk")
            level = "high"
        elif scenario == "over_current_gradual":
            result["current"] = 30.0 + progress * 75.0
            risks.append("meter_current_risk")
            level = "high" if result["current"] > 66.0 else "medium"
        elif scenario == "current_surge":
            result["current"] = 95.0 if sample_index % 6 in (0, 1) else result["current"]
            risks.append("meter_current_risk")
            level = "high"
        elif scenario == "power_factor_drop":
            result["power_factor"] = 0.68 + progress * 0.05
            risks.append("meter_current_risk")
            level = "medium"
        elif scenario == "energy_jump_or_freeze":
            if sample_index % 2 == 0:
                result["energy"] = values["energy"] - sample_index * 0.1
            else:
                result["energy"] = values["energy"] + 1000.0
            risks.append("meter_energy_risk")
            level = "high"
        result["active_power"] = result["voltage"] * result["current"] * result["power_factor"]

    elif kind == "env":
        if scenario == "high_temp":
            result["temperature"] = 50.0 + progress * 18.0
            risks.append("env_risk")
            level = "high"
        elif scenario == "high_humidity":
            result["humidity"] = 82.0 + progress * 16.0
            risks.append("env_risk")
            level = "high"
        elif scenario == "sensor_stuck":
            result["temperature"] = 31.0
            result["humidity"] = 63.0
            risks.append("env_risk")
            level = "medium"

    elif kind == "relay" and scenario == "relay_state_anomaly":
        result["relay_state"] = 0.0 if sample_index % 3 != 0 else 1.0
        risks.append("relay_state_risk")
        level = "medium"

    return result, risks, level


def feature_row(current: Dict[str, float],
                first: Dict[str, float],
                gap_ms: float,
                interval_ms: float) -> Dict[str, float]:
    row = {key: 0.0 for key in FEATURE_COLUMNS}
    for key, value in current.items():
        if key in row:
            row[key] = value
    row["online"] = current.get("online", 1.0)
    row["voltage_deviation_ratio"] = abs(current.get("voltage", 220.0) - 220.0) / 220.0
    row["frequency_deviation_hz"] = abs(current.get("frequency", 50.0) - 50.0)
    row["power_factor_drop"] = max(0.0, 1.0 - current.get("power_factor", 1.0))
    span_min = max(1.0, gap_ms / 60000.0)
    row["voltage_slope_per_min"] = (current.get("voltage", 0.0) - first.get("voltage", 0.0)) / span_min
    row["current_slope_per_min"] = (current.get("current", 0.0) - first.get("current", 0.0)) / span_min
    row["temperature_slope_per_min"] = (
        current.get("temperature", 0.0) - first.get("temperature", 0.0)) / span_min
    row["humidity_slope_per_min"] = (
        current.get("humidity", 0.0) - first.get("humidity", 0.0)) / span_min
    row["energy_delta"] = current.get("energy", 0.0) - first.get("energy", 0.0)
    row["energy_freeze"] = 1.0 if current.get("energy", 0.0) and row["energy_delta"] <= 0.0 else 0.0
    row["sample_gap_ms"] = gap_ms
    row["sample_gap_ratio"] = gap_ms / interval_ms if interval_ms > 0 else 0.0
    return row


def iter_samples(args: argparse.Namespace) -> Iterable[Tuple[Dict[str, object], Dict[str, object], bytes]]:
    interval_ms = args.interval_sec * 1000
    total_samples = max(1, int(args.hours * 3600 / args.interval_sec))
    base_ts = args.start_ts_ms
    seq = 1

    for scenario in args.scenarios:
        first_by_device: Dict[str, Dict[str, float]] = {}

        for sample_index in range(total_samples):
            ts_ms = base_ts + sample_index * interval_ms

            # Heartbeats are replayed to exercise DTU stability features.
            if scenario not in {"dtu_offline"}:
                for node_id in sorted(st.TOPOLOGY_NODES):
                    if scenario == "dtu_jitter" and node_id in {13, 14, 23} and sample_index % 5 == 0:
                        continue
                    role = st.dtu_role(node_id)
                    frame = st.build_sle_frame(
                        st.SLE_FRAME_TYPE_HEARTBEAT,
                        role,
                        node_id,
                        st.DEFAULT_DST_NODE,
                        seq,
                        bytes([role & 0xFF]),
                    )
                    seq += 1
                    root_id = root_for_node(node_id)
                    values = {"online": 1.0, "sample_gap_ms": interval_ms}
                    if scenario == "dtu_jitter" and node_id >= 12:
                        values["sample_gap_ms"] = interval_ms * 2.5
                    labels = ["dtu_stability_risk"] if scenario == "dtu_jitter" and node_id >= 12 else []
                    level = "medium" if labels else "normal"
                    record = {
                        "kind": "heartbeat",
                        "scenario": scenario,
                        "device_id": f"DTU_{node_id:03d}",
                        "dtu_id": node_id,
                        "root_id": root_id,
                        "ts_ms": ts_ms,
                        "risk_type": ",".join(labels),
                        "risk_level": level,
                        "label": 1 if labels else 0,
                        "values": values,
                        "features": feature_row(values, values, values["sample_gap_ms"], interval_ms),
                    }
                    yield record, {"frame_hex": frame.hex().upper()}, frame

            for device in st.EXTERNAL_DEVICES:
                kind = str(device["kind"])
                values = base_values(device, sample_index, args.interval_sec)
                values, risks, level = apply_scenario(values, kind, scenario, sample_index, total_samples)
                device_id = str(device["device_id"])
                first = first_by_device.setdefault(device_id, dict(values))
                gap_ms = interval_ms
                features = feature_row(values, first, gap_ms, interval_ms)
                frame = build_device_frame(device, seq, values)
                seq += 1

                record = {
                    "kind": "data",
                    "scenario": scenario,
                    "device_id": device_id,
                    "dtu_id": int(device["dtu_id"]),
                    "root_id": root_for_node(int(device["dtu_id"])),
                    "ts_ms": ts_ms,
                    "risk_type": ",".join(risks),
                    "risk_level": level,
                    "label": 1 if risks else 0,
                    "values": values,
                    "features": features,
                }
                yield record, {"frame_hex": frame.hex().upper()}, frame


def write_outputs(args: argparse.Namespace) -> None:
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    training_path = out_dir / "training.csv"
    labels_path = out_dir / "labels.jsonl"
    replay_path = out_dir / "replay.jsonl"

    fieldnames = [
        "scenario", "device_id", "dtu_id", "root_id", "ts_ms",
        "risk_type", "risk_level", "label",
    ] + FEATURE_COLUMNS + [f"label_{risk}" for risk in RISK_TYPES]

    count = 0
    with training_path.open("w", newline="", encoding="utf-8") as csv_fp, \
            labels_path.open("w", encoding="utf-8") as labels_fp, \
            replay_path.open("w", encoding="utf-8") as replay_fp:
        writer = csv.DictWriter(csv_fp, fieldnames=fieldnames)
        writer.writeheader()

        for record, replay_extra, _ in iter_samples(args):
            row = {
                "scenario": record["scenario"],
                "device_id": record["device_id"],
                "dtu_id": record["dtu_id"],
                "root_id": record["root_id"],
                "ts_ms": record["ts_ms"],
                "risk_type": record["risk_type"],
                "risk_level": record["risk_level"],
                "label": record["label"],
            }
            row.update(record["features"])
            risk_set = set(str(record["risk_type"]).split(",")) if record["risk_type"] else set()
            for risk in RISK_TYPES:
                row[f"label_{risk}"] = 1 if risk in risk_set else 0
            writer.writerow(row)

            labels_fp.write(json.dumps(record, ensure_ascii=True) + "\n")
            replay_record = {
                "root_id": record["root_id"],
                "device_id": record["device_id"],
                "dtu_id": record["dtu_id"],
                "kind": record["kind"],
                "scenario": record["scenario"],
                "ts_ms": record["ts_ms"],
            }
            replay_record.update(replay_extra)
            replay_fp.write(json.dumps(replay_record, ensure_ascii=True) + "\n")
            count += 1

    print(json.dumps({
        "out_dir": str(out_dir),
        "training_csv": str(training_path),
        "labels_jsonl": str(labels_path),
        "replay_jsonl": str(replay_path),
        "records": count,
        "scenarios": args.scenarios,
    }, ensure_ascii=True, indent=2))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate Gateway offline AI datasets.")
    parser.add_argument("--out-dir", default="ai_dataset", help="Output directory")
    parser.add_argument("--hours", type=float, default=24.0, help="Simulated hours per scenario")
    parser.add_argument("--interval-sec", type=int, default=300, help="Sampling interval")
    parser.add_argument("--start-ts-ms", type=int, default=1717200000000)
    parser.add_argument("--scenarios", nargs="*", default=SCENARIOS,
                        help="Scenario names to generate")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    unknown = [item for item in args.scenarios if item not in SCENARIOS]
    if unknown:
        print(f"unknown scenarios: {unknown}", flush=True)
        return 2
    if args.hours <= 0 or args.interval_sec <= 0:
        print("--hours and --interval-sec must be positive", flush=True)
        return 2
    write_outputs(args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
