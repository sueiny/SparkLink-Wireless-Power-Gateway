#!/usr/bin/env python3
"""
V2 测试帧生成器。
使用数字 ID（36, 39, 42, ...）替代旧的 0x0010+ 编码。
"""

import json
import struct
import os

# ── SLE 帧常量 ──
SLE_MAGIC = bytes([0x53, 0x54])
SLE_VERSION = 0x01
SLE_FRAME_DATA = 2
SLE_ROLE_ROOT = 1

# ── 第一阶段设备拓扑（1 Root，11 DTU + 11 外接设备）──
DEVICES = [
    ("METER_001", 38, 2, 1),
    ("METER_002", 39, 2, 1),
    ("METER_003", 42, 2, 1),
    ("METER_004", 43, 2, 1),
    ("METER_005", 58, 2, 1),
    ("METER_006", 48, 2, 1),
    ("METER_007", 63, 2, 1),
    ("ENV_001",   32, 3, 1),
    ("ENV_002",   47, 3, 1),
    ("RELAY_001", 54, 4, 1),
    ("RELAY_002", 57, 4, 1),
]


def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc


def u16be(value: int) -> bytes:
    return struct.pack(">H", value & 0xFFFF)


def i16be(value: int) -> bytes:
    return struct.pack(">h", value)


def u32be(value: int) -> bytes:
    return struct.pack(">I", value & 0xFFFFFFFF)


def build_meter_response(slave_addr: int, voltage: float, current: float,
                          power: int, pf: int, freq: int, energy: int) -> bytes:
    func_code = 0x04
    byte_count = 16
    data = (
        u16be(int(voltage * 10)) +
        u16be(int(current * 100)) +
        u16be(power) +
        u16be(pf) +
        u16be(int(freq * 100)) +
        u32be(energy) +
        u16be(0x55)  # relay_status = 合闸
    )
    frame = bytes([slave_addr, func_code, byte_count]) + data
    crc = crc16_modbus(frame)
    return frame + bytes([crc & 0xFF, (crc >> 8) & 0xFF])


def build_env_response(slave_addr: int, humidity: float, temperature: float) -> bytes:
    func_code = 0x03
    byte_count = 4
    data = u16be(int(humidity * 10)) + i16be(int(temperature * 10))
    frame = bytes([slave_addr, func_code, byte_count]) + data
    crc = crc16_modbus(frame)
    return frame + bytes([crc & 0xFF, (crc >> 8) & 0xFF])


def build_relay_response(slave_addr: int, relay_state: int) -> bytes:
    func_code = 0x01
    byte_count = 1
    coil_byte = 0x01 if relay_state else 0x00
    frame = bytes([slave_addr, func_code, byte_count, coil_byte])
    crc = crc16_modbus(frame)
    return frame + bytes([crc & 0xFF, (crc >> 8) & 0xFF])


def build_sle_frame(src_node_id: int, dst_node_id: int, seq: int,
                    payload: bytes) -> bytes:
    header = bytearray(13)
    header[0] = 0x53
    header[1] = 0x54
    header[2] = SLE_VERSION
    header[3] = SLE_FRAME_DATA
    header[4] = SLE_ROLE_ROOT
    header[5] = src_node_id & 0xFF
    header[6] = (src_node_id >> 8) & 0xFF
    header[7] = dst_node_id & 0xFF
    header[8] = (dst_node_id >> 8) & 0xFF
    header[9] = seq & 0xFF
    header[10] = (seq >> 8) & 0xFF
    header[11] = len(payload) & 0xFF
    header[12] = (len(payload) >> 8) & 0xFF
    return bytes(header) + payload


def build_data_payload(modbus_type: int, modbus_rtu: bytes) -> bytes:
    return bytes([modbus_type, len(modbus_rtu)]) + modbus_rtu


def build_sle_ipc_frame(raw_data: bytes) -> dict:
    raw_list = list(raw_data)
    raw_list.extend([0] * (256 - len(raw_list)))
    return {
        "raw_len": len(raw_data),
        "raw_data": raw_list,
    }


def main():
    frames = []
    seq = 1

    for device_id, dtu_id, modbus_type, modbus_addr in DEVICES:
        if modbus_type == 2:
            idx = int(device_id.split("_")[1])
            modbus_rtu = build_meter_response(
                modbus_addr,
                voltage=218.0 + idx * 0.5,
                current=2.4 + idx * 0.8,
                power=500 + idx * 100,
                pf=940 + idx * 5,
                freq=50.00 + idx * 0.01,
                energy=300000 + idx * 12160)
        elif modbus_type == 3:
            idx = int(device_id.split("_")[1])
            modbus_rtu = build_env_response(
                modbus_addr,
                humidity=60.0 + idx * 2.5,
                temperature=28.0 + idx * 0.8)
        elif modbus_type == 4:
            idx = int(device_id.split("_")[1])
            modbus_rtu = build_relay_response(modbus_addr, relay_state=idx % 2)
        else:
            continue

        payload = build_data_payload(modbus_type, modbus_rtu)
        raw_data = build_sle_frame(dtu_id, 0x0000, seq, payload)
        seq += 1

        ipc_frame = build_sle_ipc_frame(raw_data)
        ipc_frame["device_id"] = device_id
        ipc_frame["dtu_id"] = dtu_id
        frames.append(ipc_frame)

    out_dir = os.path.dirname(os.path.abspath(__file__))
    out_path = os.path.join(out_dir, "test_frames.json")
    with open(out_path, "w") as f:
        json.dump(frames, f, indent=2)

    # 生成二进制 payload
    out_bin = bytearray()
    for fr in frames:
        raw = bytes(fr["raw_data"][:fr["raw_len"]])
        out_bin += struct.pack("<H", len(raw))
        out_bin += raw
    bin_path = os.path.join(out_dir, "test_payload.bin")
    with open(bin_path, "wb") as f:
        f.write(out_bin)

    print(f"Generated {len(frames)} test frames -> {out_path}")
    print(f"Binary payload: {len(out_bin)} bytes -> {bin_path}")
    for fr in frames:
        src = fr["raw_data"][5] | (fr["raw_data"][6] << 8)
        print(f"  {fr['device_id']}: dtu_id={fr['dtu_id']} src_node={src} modbus_type={fr['raw_data'][13]}")


if __name__ == "__main__":
    main()
