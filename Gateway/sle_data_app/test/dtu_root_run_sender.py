#!/usr/bin/env python3
"""
Send RUN-mode SLE frames to one or more DTU root UARTs.

Windows lab usage uses the Python launcher and writes ASCII-hex ST frames by default:

    py -3 dtu_root_run_sender.py COM19 COM23 COM36 --duration 60 --interval 5

The current DTU root UART path reliably forwards printable ASCII. The default
serial write is:

    <dst_node_id> <ST_FRAME_HEX>\r\n

sle_data_app decodes this ASCII-hex line back to ST bytes before IPC to
gatewayd. For root firmware that can forward NUL-containing binary safely, use:

    --uart-encoding raw

which writes ST frame bytes directly.
"""

import argparse
import csv
import json
import struct
import sys
import threading
import time
from datetime import datetime, timezone
from typing import Dict, List, Optional

BAUDRATE = 115200
DEFAULT_DST_NODE = 0
DEFAULT_SRC_NODE = 1
DEFAULT_SRC_ROLE = 1
DEFAULT_DURATION_SEC = 60.0
DEFAULT_INTERVAL_SEC = 5.0
DEFAULT_WARMUP_SEC = 5.0
DEFAULT_WARMUP_INTERVAL_SEC = 0.2
DEFAULT_WARMUP_TEXT = "WARMUP"
DEFAULT_POST_WARMUP_DELAY_SEC = 8.0
DEFAULT_HOLD_OPEN_SEC = 10.0
DEFAULT_UART_ENCODING = "text-hex"
DEFAULT_RAW_TERMINATOR = "crlf"
DEFAULT_DEVICE_ID = "METER_001"
DEFAULT_DTU_ID = 1
DEFAULT_MODBUS_TYPE = 2
DEFAULT_MODBUS_ADDR = 1

SLE_FRAME_MAGIC = b"ST"
SLE_FRAME_VERSION = 0x01
SLE_FRAME_TYPE_HEARTBEAT = 0x01
SLE_FRAME_TYPE_DATA = 0x02
SLE_ROLE_ROOT = 0x01
SLE_ROLE_RELAY = 0x02
SLE_ROLE_LEAF = 0x03
SLE_ROLE_GATEWAY = 0x04
SLE_FRAME_HEADER_LEN = 13
SLE_FRAME_MAX_LEN = 256


def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for value in data:
        crc ^= value
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc & 0xFFFF


def u16be(value: int) -> bytes:
    return struct.pack(">H", value & 0xFFFF)


def u32be(value: int) -> bytes:
    return struct.pack(">I", value & 0xFFFFFFFF)


def u16le(value: int) -> bytes:
    return struct.pack("<H", value & 0xFFFF)


def build_meter_modbus_response(seq: int, slave_addr: int = DEFAULT_MODBUS_ADDR) -> bytes:
    voltage = 220.0 + (seq % 10) * 0.1
    current = 5.0 + (seq % 8) * 0.2
    power = int(voltage * current)
    power_factor = 960 + (seq % 5)
    frequency = 5000 + (seq % 3)
    energy = 100000 + seq * 25
    relay_status = 0x55

    data = (
        u16be(int(voltage * 10)) +
        u16be(int(current * 100)) +
        u16be(power) +
        u16be(power_factor) +
        u16be(frequency) +
        u32be(energy) +
        u16be(relay_status)
    )

    frame = bytes([slave_addr, 0x04, len(data)]) + data
    crc = crc16_modbus(frame)
    return frame + bytes([crc & 0xFF, (crc >> 8) & 0xFF])


def build_gateway_payload(seq: int) -> bytes:
    modbus_rtu = build_meter_modbus_response(seq)
    if len(modbus_rtu) > 241:
        raise ValueError(f"Modbus RTU frame too long: {len(modbus_rtu)}")
    return bytes([DEFAULT_MODBUS_TYPE, len(modbus_rtu)]) + modbus_rtu


def build_sle_frame(frame_type: int, src_role: int, src_node: int,
                    dst_node: int, seq: int, payload: bytes) -> bytes:
    frame_len = SLE_FRAME_HEADER_LEN + len(payload)
    if frame_len > SLE_FRAME_MAX_LEN:
        raise ValueError(f"SLE frame too long: {frame_len}")

    return (
        SLE_FRAME_MAGIC +
        bytes([SLE_FRAME_VERSION, frame_type & 0xFF, src_role & 0xFF]) +
        u16le(src_node) +
        u16le(dst_node) +
        u16le(seq) +
        u16le(len(payload)) +
        payload
    )


def build_heartbeat_frame(seq: int, args: argparse.Namespace) -> bytes:
    return build_sle_frame(
        SLE_FRAME_TYPE_HEARTBEAT,
        args.src_role,
        args.src_node,
        args.dst_node,
        seq,
        bytes([args.src_role & 0xFF]),
    )


def build_data_frame(seq: int, args: argparse.Namespace) -> bytes:
    payload = build_gateway_payload(seq)
    return build_sle_frame(
        SLE_FRAME_TYPE_DATA,
        args.src_role,
        args.src_node,
        args.dst_node,
        seq,
        payload,
    )


def build_text_hex_line(dst_node: int, raw: bytes) -> bytes:
    return f"{dst_node} {raw.hex().upper()}\r\n".encode("ascii")


def raw_terminator_bytes(args: argparse.Namespace) -> bytes:
    if args.raw_terminator == "crlf":
        return b"\r\n"
    if args.raw_terminator == "lf":
        return b"\n"
    return b""


def encode_uart_write(raw: bytes, args: argparse.Namespace) -> bytes:
    if args.uart_encoding == "text-hex":
        return build_text_hex_line(args.dst_node, raw)
    return raw + raw_terminator_bytes(args)


def build_payload_uart_write(seq: int, args: argparse.Namespace) -> bytes:
    return encode_uart_write(build_gateway_payload(seq), args)


def build_frame_uart_write(seq: int, args: argparse.Namespace, frame_type: str) -> bytes:
    if frame_type == "heartbeat":
        raw = build_heartbeat_frame(seq, args)
    elif frame_type == "data":
        raw = build_data_frame(seq, args)
    else:
        raise ValueError(f"unsupported frame_type: {frame_type}")
    return encode_uart_write(raw, args)


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def warmup_line(args: argparse.Namespace) -> bytes:
    if args.warmup_text:
        return f"{args.warmup_text}\r\n".encode("ascii")
    return f"{DEFAULT_WARMUP_TEXT}\r\n".encode("ascii")


def warmup_serial(ser, args: argparse.Namespace, stats: Dict[str, object]) -> None:
    if args.warmup_sec <= 0:
        return

    line = warmup_line(args)
    deadline = time.time() + args.warmup_sec
    while time.time() < deadline:
        try:
            ser.write(line)
            ser.flush()
            stats["warmup_sent"] = int(stats["warmup_sent"]) + 1
        except Exception:
            stats["warmup_errors"] = int(stats["warmup_errors"]) + 1

        try:
            rx = ser.read(ser.in_waiting or 0)
            stats["rx_bytes"] = int(stats["rx_bytes"]) + len(rx)
        except Exception:
            pass

        time.sleep(args.warmup_interval)


def run_port(port: str, args: argparse.Namespace) -> Dict[str, object]:
    try:
        import serial
    except ImportError as exc:
        raise RuntimeError("pyserial is required: py -3 -m pip install pyserial") from exc

    stats: Dict[str, object] = {
        "port": port,
        "baudrate": BAUDRATE,
        "wire_format": args.wire_format,
        "uart_encoding": args.uart_encoding,
        "raw_terminator": args.raw_terminator,
        "dst_node": args.dst_node,
        "src_node": args.src_node,
        "src_role": args.src_role,
        "device_id": DEFAULT_DEVICE_ID,
        "dtu_id": DEFAULT_DTU_ID,
        "modbus_type": DEFAULT_MODBUS_TYPE,
        "modbus_addr": DEFAULT_MODBUS_ADDR,
        "warmup_sec": args.warmup_sec,
        "warmup_interval": args.warmup_interval,
        "post_warmup_delay": args.post_warmup_delay,
        "hold_open": args.hold_open,
        "dtr": args.dtr,
        "rts": args.rts,
        "warmup_sent": 0,
        "warmup_errors": 0,
        "sent": 0,
        "heartbeat_sent": 0,
        "data_sent": 0,
        "write_errors": 0,
        "rx_bytes": 0,
        "timeouts": 0,
        "crc_errors": 0,
        "first_ts": "",
        "last_ts": "",
        "duration_sec": 0.0,
        "total_duration_sec": 0.0,
        "rate_hz": 0.0,
    }

    total_start = time.time()
    seq = 1

    ser = serial.Serial()
    ser.port = port
    ser.baudrate = BAUDRATE
    ser.timeout = 0.1
    ser.write_timeout = 1.0
    ser.rtscts = False
    ser.dsrdtr = False

    with ser:
        try:
            ser.setDTR(args.dtr)
            ser.setRTS(args.rts)
        except Exception:
            pass

        warmup_serial(ser, args, stats)
        if args.post_warmup_delay > 0:
            time.sleep(args.post_warmup_delay)

        start = time.time()
        deadline = start + args.duration
        while True:
            if args.count is not None and seq > args.count:
                break
            if args.count is None and time.time() >= deadline:
                break

            writes = []
            if args.wire_format == "frame" and not args.no_heartbeat:
                writes.append(("heartbeat", build_frame_uart_write(seq * 2 - 1, args, "heartbeat")))
            if args.wire_format == "frame":
                writes.append(("data", build_frame_uart_write(seq * 2, args, "data")))
            else:
                writes.append(("data", build_payload_uart_write(seq, args)))

            for kind, write_data in writes:
                try:
                    ser.write(write_data)
                    ser.flush()
                    if stats["sent"] == 0:
                        stats["first_ts"] = utc_now()
                    stats["sent"] = int(stats["sent"]) + 1
                    if kind == "heartbeat":
                        stats["heartbeat_sent"] = int(stats["heartbeat_sent"]) + 1
                    else:
                        stats["data_sent"] = int(stats["data_sent"]) + 1
                    stats["last_ts"] = utc_now()
                except Exception:
                    stats["write_errors"] = int(stats["write_errors"]) + 1

            # Some firmware builds echo status over UART; consume it if present.
            try:
                rx = ser.read(ser.in_waiting or 0)
                stats["rx_bytes"] = int(stats["rx_bytes"]) + len(rx)
            except Exception:
                pass

            seq += 1
            if args.count is None or seq <= args.count:
                time.sleep(args.interval)

        if args.hold_open > 0:
            deadline = time.time() + args.hold_open
            while time.time() < deadline:
                try:
                    rx = ser.read(ser.in_waiting or 0)
                    stats["rx_bytes"] = int(stats["rx_bytes"]) + len(rx)
                except Exception:
                    pass
                time.sleep(0.1)

    elapsed = max(time.time() - start, 0.001)
    stats["duration_sec"] = round(elapsed, 3)
    stats["total_duration_sec"] = round(time.time() - total_start, 3)
    stats["rate_hz"] = round(float(stats["sent"]) / elapsed, 3)
    return stats


def dry_run(args: argparse.Namespace) -> List[Dict[str, object]]:
    records = []
    for seq in range(1, args.preview_count + 1):
        if args.wire_format == "frame" and not args.no_heartbeat:
            heartbeat_frame = build_heartbeat_frame(seq * 2 - 1, args)
            write_data = encode_uart_write(heartbeat_frame, args)
            record = {
                "seq": seq,
                "kind": "heartbeat",
                "wire_format": args.wire_format,
                "uart_encoding": args.uart_encoding,
                "raw_terminator": args.raw_terminator,
                "uart_line": build_text_hex_line(args.dst_node, heartbeat_frame).decode("ascii").rstrip()
                if args.uart_encoding == "text-hex" else "",
                "write_hex": write_data.hex().upper(),
                "write_len": len(write_data),
                "dst_node": args.dst_node,
                "src_node": args.src_node,
                "src_role": args.src_role,
                "frame_hex": heartbeat_frame.hex().upper(),
                "frame_len": len(heartbeat_frame),
                "frame_type": SLE_FRAME_TYPE_HEARTBEAT,
            }
            records.append(record)
            print(json.dumps(record, ensure_ascii=True))

        payload = build_gateway_payload(seq)
        modbus_rtu = payload[2:]
        if args.wire_format == "frame":
            data_frame = build_data_frame(seq * 2, args)
            write_data = encode_uart_write(data_frame, args)
            uart_line = build_text_hex_line(args.dst_node, data_frame).decode("ascii").rstrip() \
                if args.uart_encoding == "text-hex" else ""
            frame_hex = data_frame.hex().upper()
            frame_len = len(data_frame)
            frame_type = SLE_FRAME_TYPE_DATA
        else:
            write_data = build_payload_uart_write(seq, args)
            uart_line = build_text_hex_line(args.dst_node, payload).decode("ascii").rstrip() \
                if args.uart_encoding == "text-hex" else ""
            frame_hex = ""
            frame_len = 0
            frame_type = ""

        record = {
            "seq": seq,
            "kind": "data",
            "wire_format": args.wire_format,
            "uart_encoding": args.uart_encoding,
            "raw_terminator": args.raw_terminator,
            "uart_line": uart_line,
            "write_hex": write_data.hex().upper(),
            "write_len": len(write_data),
            "dst_node": args.dst_node,
            "src_node": args.src_node if args.wire_format == "frame" else "",
            "src_role": args.src_role if args.wire_format == "frame" else "",
            "frame_hex": frame_hex,
            "frame_len": frame_len,
            "frame_type": frame_type,
            "payload_hex": payload.hex().upper(),
            "modbus_rtu_hex": modbus_rtu.hex().upper(),
            "modbus_crc_ok": crc16_modbus(modbus_rtu[:-2]) ==
            (modbus_rtu[-2] | (modbus_rtu[-1] << 8)),
        }
        records.append(record)
        print(json.dumps(record, ensure_ascii=True))
    return records


def write_json(path: Optional[str], records: List[Dict[str, object]]) -> None:
    if not path:
        return
    with open(path, "w", encoding="utf-8") as fp:
        json.dump(records, fp, indent=2, ensure_ascii=True)
        fp.write("\n")


def write_csv(path: Optional[str], records: List[Dict[str, object]]) -> None:
    if not path:
        return
    if not records:
        return
    fieldnames = sorted({key for record in records for key in record.keys()})
    with open(path, "w", newline="", encoding="utf-8") as fp:
        writer = csv.DictWriter(fp, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(records)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Send METER_001 RUN-mode SLE frames to DTU root UARTs at 115200 8N1.")
    parser.add_argument("ports", nargs="*", help="Serial ports, for example COM19 COM23 COM36")
    parser.add_argument("--wire-format", choices=("frame", "payload"), default="frame",
                        help="frame sends a full ST frame; payload sends the legacy modbus payload only")
    parser.add_argument("--uart-encoding", choices=("raw", "text-hex"), default=DEFAULT_UART_ENCODING,
                        help="raw writes ST bytes directly; text-hex writes '<dst> <HEX>' for old firmware")
    parser.add_argument("--raw-terminator", choices=("crlf", "lf", "none"), default=DEFAULT_RAW_TERMINATOR,
                        help="Terminator appended only in --uart-encoding raw mode")
    parser.add_argument("--duration", type=float, default=DEFAULT_DURATION_SEC,
                        help="Send duration in seconds when --count is not set")
    parser.add_argument("--interval", type=float, default=DEFAULT_INTERVAL_SEC,
                        help="Interval between UART lines in seconds")
    parser.add_argument("--warmup-sec", type=float, default=DEFAULT_WARMUP_SEC,
                        help="Seconds to keep writing harmless UART content after opening the port")
    parser.add_argument("--warmup-interval", type=float, default=DEFAULT_WARMUP_INTERVAL_SEC,
                        help="Interval between warmup UART writes in seconds")
    parser.add_argument("--warmup-text", default="",
                        help="Warmup text before CRLF; defaults to WARMUP")
    parser.add_argument("--post-warmup-delay", type=float, default=DEFAULT_POST_WARMUP_DELAY_SEC,
                        help="Seconds to wait after warmup writes before sending real frames")
    parser.add_argument("--hold-open", type=float, default=DEFAULT_HOLD_OPEN_SEC,
                        help="Seconds to keep the port open after sending real frames")
    parser.add_argument("--dtr", action="store_true",
                        help="Assert DTR after opening the port; default keeps DTR deasserted")
    parser.add_argument("--rts", action="store_true",
                        help="Assert RTS after opening the port; default keeps RTS deasserted")
    parser.add_argument("--count", type=int, default=None,
                        help="Send an exact number of lines per port")
    parser.add_argument("--dst-node", type=int, default=DEFAULT_DST_NODE,
                        help="SLE tree destination node id; 0 means gateway")
    parser.add_argument("--src-node", type=int, default=DEFAULT_SRC_NODE,
                        help="SLE frame source node id; default 1 means DTU_001/root")
    parser.add_argument("--src-role", type=int, default=DEFAULT_SRC_ROLE,
                        help="SLE frame source role; 1=root, 2=relay, 3=leaf")
    parser.add_argument("--no-heartbeat", action="store_true",
                        help="Do not send heartbeat frames in --wire-format frame mode")
    parser.add_argument("--dry-run", action="store_true",
                        help="Print generated UART lines without opening serial ports")
    parser.add_argument("--preview-count", type=int, default=3,
                        help="Number of dry-run lines to print")
    parser.add_argument("--json-out", default=None, help="Write result records to JSON")
    parser.add_argument("--csv-out", default=None, help="Write result records to CSV")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.interval <= 0:
        print("--interval must be positive", file=sys.stderr)
        return 2
    if args.warmup_sec < 0:
        print("--warmup-sec must be non-negative", file=sys.stderr)
        return 2
    if args.warmup_interval <= 0:
        print("--warmup-interval must be positive", file=sys.stderr)
        return 2
    if args.post_warmup_delay < 0:
        print("--post-warmup-delay must be non-negative", file=sys.stderr)
        return 2
    if args.hold_open < 0:
        print("--hold-open must be non-negative", file=sys.stderr)
        return 2
    if args.duration <= 0 and args.count is None:
        print("--duration must be positive when --count is not set", file=sys.stderr)
        return 2
    if args.count is not None and args.count <= 0:
        print("--count must be positive", file=sys.stderr)
        return 2

    if args.dry_run:
        records = dry_run(args)
        write_json(args.json_out, records)
        write_csv(args.csv_out, records)
        return 0

    if not args.ports:
        print("at least one serial port is required unless --dry-run is used; "
              "on Windows use: py -3 dtu_root_run_sender.py COM19", file=sys.stderr)
        return 2

    records: List[Dict[str, object]] = []
    errors: List[str] = []
    lock = threading.Lock()

    def worker(port: str) -> None:
        try:
            result = run_port(port, args)
            with lock:
                records.append(result)
                print(json.dumps(result, ensure_ascii=True))
        except Exception as exc:
            with lock:
                errors.append(f"{port}: {exc}")
                print(f"[ERROR] {port}: {exc}", file=sys.stderr)

    threads = [threading.Thread(target=worker, args=(port,), daemon=True) for port in args.ports]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()

    write_json(args.json_out, records)
    write_csv(args.csv_out, records)
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
