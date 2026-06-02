#!/usr/bin/env python3
"""
IPC 测试客户端：连接 gatewayd 的 Unix Socket，发送仿真 SleIpcFrame。

用法:
    python3 ipc_test_client.py test_frames.json [--loop] [--interval 5]

参数:
    test_frames.json  由 gen_test_frames.py 生成的测试数据
    --loop            循环发送（模拟持续上报）
    --interval N      每轮间隔秒数（默认 5）
"""

import json
import socket
import struct
import sys
import time
import os

# SleIpcFrame 固定大小:
# root_conn_id(1) + sle_mac(6) + raw_len(2) + raw_data(256) + timestamp_ms(8) = 273 字节
IPC_FRAME_SIZE = 273
SOCKET_PATH = "/var/run/gateway/sle_data.sock"


def pack_ipc_frame(frame: dict) -> bytes:
    """将 JSON 帧序列化为 SleIpcFrame 二进制"""
    buf = bytearray(IPC_FRAME_SIZE)

    # root_conn_id (1 byte)
    buf[0] = frame["root_conn_id"] & 0xFF

    # sle_mac (6 bytes)
    mac = frame["sle_mac"]
    for i in range(6):
        buf[1 + i] = mac[i] & 0xFF

    # raw_len (2 bytes, 小端)
    raw_len = frame["raw_len"]
    buf[7] = raw_len & 0xFF
    buf[8] = (raw_len >> 8) & 0xFF

    # raw_data (256 bytes)
    raw_data = frame["raw_data"]
    for i in range(min(len(raw_data), 256)):
        buf[9 + i] = raw_data[i]

    # timestamp_ms (8 bytes, 小端)
    ts = frame["timestamp_ms"]
    struct.pack_into("<q", buf, 265, ts)

    return bytes(buf)


def connect_socket(path: str, retries: int = 10, delay: float = 2.0) -> socket.socket:
    """连接 Unix Socket，带重试"""
    for attempt in range(retries):
        try:
            sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            sock.connect(path)
            print(f"[CLIENT] connected to {path}")
            return sock
        except (ConnectionRefusedError, FileNotFoundError) as e:
            print(f"[CLIENT] connect attempt {attempt + 1}/{retries} failed: {e}")
            sock.close()
            if attempt < retries - 1:
                time.sleep(delay)
    print("[CLIENT] failed to connect after all retries")
    sys.exit(1)


def send_frame(sock: socket.socket, frame: bytes) -> bool:
    """发送一帧，返回是否成功"""
    try:
        sock.sendall(frame)
        return True
    except BrokenPipeError:
        print("[CLIENT] connection lost")
        return False


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <test_frames.json> [--loop] [--interval N]")
        sys.exit(1)

    json_path = sys.argv[1]
    loop_mode = "--loop" in sys.argv
    interval = 5.0

    if "--interval" in sys.argv:
        idx = sys.argv.index("--interval")
        if idx + 1 < len(sys.argv):
            interval = float(sys.argv[idx + 1])

    # 加载测试帧
    with open(json_path) as f:
        frames = json.load(f)
    print(f"[CLIENT] loaded {len(frames)} test frames from {json_path}")

    # 打印帧摘要
    for fr in frames:
        dev_id = fr.get("device_id", "?")
        raw_len = fr["raw_len"]
        # 解析 SLE 帧头
        rd = fr["raw_data"]
        src_node = rd[5] | (rd[6] << 8)
        modbus_type = rd[13] if raw_len > 13 else -1
        print(f"  {dev_id}: src_node=0x{src_node:04x} modbus_type={modbus_type} raw_len={raw_len}")

    # 连接 socket
    sock = connect_socket(SOCKET_PATH)

    round_num = 0
    try:
        while True:
            round_num += 1
            print(f"\n[CLIENT] === round {round_num} ===")

            # 更新时间戳
            now_ms = int(time.time() * 1000)
            for i, fr in enumerate(frames):
                fr["timestamp_ms"] = now_ms + i  # 每帧递增 1ms

            # 逐帧发送
            success = 0
            for fr in frames:
                data = pack_ipc_frame(fr)
                if send_frame(sock, data):
                    dev_id = fr.get("device_id", "?")
                    print(f"  [TX] {dev_id} ({len(data)} bytes)")
                    success += 1
                else:
                    break
                time.sleep(0.05)  # 帧间间隔 50ms

            print(f"[CLIENT] sent {success}/{len(frames)} frames")

            if not loop_mode:
                break

            time.sleep(interval)

    except KeyboardInterrupt:
        print("\n[CLIENT] interrupted")
    finally:
        sock.close()
        print("[CLIENT] disconnected")


if __name__ == "__main__":
    main()
