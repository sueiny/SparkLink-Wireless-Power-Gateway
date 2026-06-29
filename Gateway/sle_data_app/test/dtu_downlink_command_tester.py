#!/usr/bin/env python3
"""Listen to DTU root COM output and verify gateway downlink ST commands.

Windows usage:
    py -3 dtu_downlink_command_tester.py COM23 --expect meter-trip --expect-dtu-id 1
    py -3 dtu_downlink_command_tester.py COM19 COM23 COM36 --expect relay-open --expect-dtu-id 9
"""

from __future__ import annotations

import argparse
import json
import re
import time
from dataclasses import dataclass
from typing import Iterable, Optional

try:
    import serial  # type: ignore
except ImportError as exc:  # pragma: no cover - environment check
    raise SystemExit("pyserial is required: py -3 -m pip install pyserial") from exc


SLE_HEADER_LEN = 13
SLE_FRAME_MAX_PAYLOAD = 1011


@dataclass
class ParsedFrame:
    port: str
    raw_hex: str
    dst_node_id: int
    modbus_rtu: bytes
    crc_ok: bool


def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc & 0xFFFF


def modbus_crc_ok(frame: bytes) -> bool:
    if len(frame) < 4:
        return False
    expected = crc16_modbus(frame[:-2])
    actual = frame[-2] | (frame[-1] << 8)
    return expected == actual


def u16le(data: bytes, offset: int) -> int:
    return data[offset] | (data[offset + 1] << 8)


def extract_hex_candidates(line: bytes) -> Iterable[bytes]:
    if b"ST" in line:
        start = line.find(b"ST")
        yield line[start:].rstrip(b"\r\n")

    text = line.decode("ascii", errors="ignore")
    compact_matches = re.findall(r"(?:53\s*54\s*)[0-9A-Fa-f\s:,-]{20,}", text)
    for match in compact_matches:
        hex_text = re.sub(r"[^0-9A-Fa-f]", "", match)
        if len(hex_text) >= SLE_HEADER_LEN * 2 and len(hex_text) % 2 == 0:
            yield bytes.fromhex(hex_text)


def parse_st_frame(port: str, raw: bytes) -> Optional[ParsedFrame]:
    start = raw.find(b"ST")
    if start < 0:
        return None
    raw = raw[start:]
    if len(raw) < SLE_HEADER_LEN or raw[2] != 0x01:
        return None

    payload_len = u16le(raw, 11)
    if payload_len > SLE_FRAME_MAX_PAYLOAD:
        return None
    total_len = SLE_HEADER_LEN + payload_len
    if len(raw) < total_len:
        return None

    frame = raw[:total_len]
    payload = frame[SLE_HEADER_LEN:]
    if len(payload) < 4:
        return None

    modbus_rtu = payload
    return ParsedFrame(
        port=port,
        raw_hex=frame.hex().upper(),
        dst_node_id=u16le(frame, 7),
        modbus_rtu=modbus_rtu,
        crc_ok=modbus_crc_ok(modbus_rtu),
    )


def frame_matches(frame: ParsedFrame, args: argparse.Namespace) -> bool:
    if frame.dst_node_id != args.expect_dtu_id or not frame.crc_ok:
        return False

    rtu = frame.modbus_rtu
    if args.expect == "meter-trip":
        return (
            len(rtu) >= 11
            and rtu[1] == 0x10
            and rtu[2:4] == b"\x00\x10"
            and rtu[7:9] == b"\xAA\xAA"
        )

    if args.expect in {"relay-open", "relay-close"}:
        expected_value = b"\x00\x00" if args.expect == "relay-open" else b"\xFF\x00"
        return (
            len(rtu) >= 8
            and rtu[1] == 0x05
            and rtu[2:4] == bytes([0x00, args.channel & 0xFF])
            and rtu[4:6] == expected_value
        )

    return False


def listen(args: argparse.Namespace) -> dict:
    deadline = time.time() + args.duration
    serials = []
    frames_seen = 0
    matched: Optional[ParsedFrame] = None
    seen_frames: list[dict] = []

    for port in args.ports:
        serials.append(serial.Serial(port, 115200, timeout=0.2))

    try:
        while time.time() < deadline and matched is None:
            for ser in serials:
                line = ser.readline()
                if not line:
                    continue
                for candidate in extract_hex_candidates(line):
                    frame = parse_st_frame(ser.port, candidate)
                    if frame is None:
                        continue
                    frames_seen += 1
                    if len(seen_frames) < 8:
                        seen_frames.append({
                            "port": frame.port,
                            "dst_node_id": frame.dst_node_id,
                            "modbus_rtu_hex": frame.modbus_rtu.hex().upper(),
                            "crc_ok": frame.crc_ok,
                            "raw_hex": frame.raw_hex,
                        })
                    if frame_matches(frame, args):
                        matched = frame
                        break
                if matched is not None:
                    break
    finally:
        for ser in serials:
            ser.close()

    result = {
        "matched": matched is not None,
        "expect": args.expect,
        "expect_dtu_id": args.expect_dtu_id,
        "channel": args.channel,
        "ports": args.ports,
        "frames_seen": frames_seen,
        "seen_frames": seen_frames,
    }
    if matched is not None:
        result.update({
            "matched_port": matched.port,
            "matched_frame_hex": matched.raw_hex,
            "modbus_rtu_hex": matched.modbus_rtu.hex().upper(),
            "crc_ok": matched.crc_ok,
        })
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ports", nargs="+", help="COM ports, e.g. COM23 COM36")
    parser.add_argument("--expect", required=True,
                        choices=["meter-trip", "relay-open", "relay-close"])
    parser.add_argument("--expect-dtu-id", type=int, default=None,
                        help="Expected ST dst_node_id, i.e. target DTU node id")
    parser.add_argument("--expect-root-id", type=int, default=None,
                        help="Deprecated alias for --expect-dtu-id")
    parser.add_argument("--dtu-id", type=int, default=None,
                        help="Deprecated alias for --expect-dtu-id")
    parser.add_argument("--channel", type=int, default=0)
    parser.add_argument("--duration", type=float, default=60.0)
    args = parser.parse_args()
    if args.expect_dtu_id is None:
        if args.dtu_id is not None:
            args.expect_dtu_id = args.dtu_id
        elif args.expect_root_id is not None:
            args.expect_dtu_id = args.expect_root_id
        else:
            parser.error("--expect-dtu-id is required")

    result = listen(args)
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result["matched"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
