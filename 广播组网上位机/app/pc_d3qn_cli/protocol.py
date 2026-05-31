from __future__ import annotations

import re
from dataclasses import dataclass
from typing import Iterable, List


MAGIC = b"MS"
VERSION = 0x01
TYPE_RSSI_REPORT = 0x01
TYPE_ACK = 0x05


class ProtocolError(ValueError):
    pass


@dataclass(frozen=True)
class RssiNeighbor:
    addr: int
    rssi: int


@dataclass(frozen=True)
class RssiReport:
    src_addr: int
    neighbors: List[RssiNeighbor]


@dataclass(frozen=True)
class Ack:
    src_addr: int
    dst_addr: int
    seq: int


_TEXT_ACK_RE = re.compile(r"^ACK\s+([0-9A-Fa-fx]+)\s+(\d+)\s*$")
_TEXT_RSSI_RE = re.compile(r"^RSSI_REPORT\s+src=([0-9A-Fa-fx]+)\s+count=(\d+)\s*(.*)$")
_TEXT_NEIGHBOR_RE = re.compile(r"\[([0-9A-Fa-fx]+):(-?\d+)\]")


def _to_int8(value: int) -> int:
    return value - 256 if value >= 128 else value


def _validate_header(frame: bytes, expected_type: int) -> None:
    if len(frame) < 4:
        raise ProtocolError("frame too short")
    if frame[0:2] != MAGIC:
        raise ProtocolError("invalid magic")
    if frame[2] != VERSION:
        raise ProtocolError(f"unsupported version: 0x{frame[2]:02X}")
    if frame[3] != expected_type:
        raise ProtocolError(f"unexpected type: 0x{frame[3]:02X}")


def parse_rssi_report(frame: bytes) -> RssiReport:
    _validate_header(frame, TYPE_RSSI_REPORT)
    if len(frame) < 6:
        raise ProtocolError("RSSI_REPORT too short")
    src_addr = frame[4]
    count = frame[5]
    expected_len = 6 + count * 2
    if len(frame) != expected_len:
        raise ProtocolError(f"RSSI_REPORT length mismatch: got {len(frame)}, expected {expected_len}")
    neighbors = [
        RssiNeighbor(addr=frame[offset], rssi=_to_int8(frame[offset + 1]))
        for offset in range(6, expected_len, 2)
    ]
    return RssiReport(src_addr=src_addr, neighbors=neighbors)


def parse_ack(frame: bytes) -> Ack:
    _validate_header(frame, TYPE_ACK)
    if len(frame) != 8:
        raise ProtocolError(f"ACK length mismatch: got {len(frame)}, expected 8")
    return Ack(src_addr=frame[4], dst_addr=frame[5], seq=int.from_bytes(frame[6:8], "big"))


def parse_frame(frame: bytes) -> RssiReport | Ack:
    if len(frame) < 4:
        raise ProtocolError("frame too short")
    if frame[3] == TYPE_RSSI_REPORT:
        return parse_rssi_report(frame)
    if frame[3] == TYPE_ACK:
        return parse_ack(frame)
    raise ProtocolError(f"unsupported frame type: 0x{frame[3]:02X}")


def parse_text_message(line: str) -> RssiReport | Ack | None:
    text = line.strip()
    ack_match = _TEXT_ACK_RE.match(text)
    if ack_match:
        return Ack(src_addr=parse_addr(ack_match.group(1)), dst_addr=0x00, seq=int(ack_match.group(2), 10))

    rssi_match = _TEXT_RSSI_RE.match(text)
    if not rssi_match:
        return None
    src_addr = parse_addr(rssi_match.group(1))
    expected_count = int(rssi_match.group(2), 10)
    neighbors = [
        RssiNeighbor(addr=parse_addr(addr), rssi=int(rssi, 10))
        for addr, rssi in _TEXT_NEIGHBOR_RE.findall(rssi_match.group(3))
    ]
    if len(neighbors) != expected_count:
        raise ProtocolError(f"RSSI_REPORT text count mismatch: got {len(neighbors)}, expected {expected_count}")
    return RssiReport(src_addr=src_addr, neighbors=neighbors)


def normalize_hex_payload(payload: str) -> str:
    normalized = "".join(payload.split()).lower()
    if not normalized:
        raise ProtocolError("payload is empty")
    if len(normalized) % 2 != 0:
        raise ProtocolError("payload hex length must be even")
    try:
        bytes.fromhex(normalized)
    except ValueError as exc:
        raise ProtocolError("payload must be hexadecimal") from exc
    return normalized


def format_addr(addr: int) -> str:
    if not 0 <= int(addr) <= 0xFF:
        raise ProtocolError(f"address out of range: {addr}")
    return f"{int(addr):02X}"


def parse_addr(value: str) -> int:
    text = value.strip().lower()
    if text.startswith("0x"):
        text = text[2:]
    if not text:
        raise ProtocolError("empty address")
    addr = int(text, 16)
    if not 0 <= addr <= 0xFF:
        raise ProtocolError(f"address out of range: {value}")
    return addr


def build_send_command(dst: int, path: Iterable[int], payload: str) -> str:
    path_list = list(path)
    if not path_list:
        raise ProtocolError("path is empty")
    if path_list[-1] != int(dst):
        raise ProtocolError("path must end with dst")
    payload_hex = normalize_hex_payload(payload)
    fields = ["SEND", format_addr(dst), str(len(path_list))]
    fields.extend(format_addr(addr) for addr in path_list)
    fields.append(payload_hex)
    return " ".join(fields) + "\r\n"

