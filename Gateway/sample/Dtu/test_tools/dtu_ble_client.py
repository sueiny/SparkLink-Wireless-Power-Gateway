#!/usr/bin/env python3
"""DTU BLE 最小联调脚本。

功能：
1. 扫描名称包含关键字的 DTU BLE 设备
2. 自动连接并订阅 Notify
3. 发送一条 DTU 原始十六进制协议帧
4. 打印收到的 Notify 响应

依赖：
    python3 -m pip install bleak

示例：
    python3 dtu_ble_client.py --name DTU_TEST --hex "AA5502020000A05C"
"""

from __future__ import annotations

import argparse
import asyncio
from typing import Optional

from bleak import BleakClient, BleakScanner


SERVICE_UUID = "0000FDF0-0000-1000-8000-00805F9B34FB"
CHAR_UUID = "0000FDF1-0000-1000-8000-00805F9B34FB"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="DTU BLE test client")
    parser.add_argument(
        "--name",
        default="DTU",
        help="设备名关键字，默认按包含 DTU 过滤",
    )
    parser.add_argument(
        "--address",
        default="",
        help="指定 BLE 地址，填了就不按名称扫描",
    )
    parser.add_argument(
        "--hex",
        default="AA55010100005018",
        help="要发送的 DTU 十六进制报文，默认 READ_DEV_INFO",
    )
    parser.add_argument(
        "--scan-timeout",
        type=float,
        default=5.0,
        help="扫描超时时间，单位秒",
    )
    parser.add_argument(
        "--wait",
        type=float,
        default=3.0,
        help="发送后等待 Notify 的时间，单位秒",
    )
    return parser.parse_args()


def normalize_hex(hex_text: str) -> bytes:
    cleaned = hex_text.replace(" ", "").replace("\n", "").replace("\r", "")
    if len(cleaned) % 2 != 0:
        raise ValueError("十六进制字符串长度必须为偶数")
    return bytes.fromhex(cleaned)


async def find_device(name_keyword: str, timeout: float, address: str):
    if address:
        return address

    devices = await BleakScanner.discover(timeout=timeout)
    for dev in devices:
        dev_name = dev.name or ""
        if name_keyword in dev_name:
            return dev.address
    return None


def notify_handler(_: int, data: bytearray) -> None:
    print(f"notify: {bytes(data).hex(' ').upper()}")


async def main() -> int:
    args = parse_args()
    payload = normalize_hex(args.hex)

    target = await find_device(args.name, args.scan_timeout, args.address)
    if not target:
        print("未找到目标 BLE 设备")
        return 1

    print(f"connect: {target}")
    async with BleakClient(target) as client:
        if not client.is_connected:
            print("连接失败")
            return 1

        print(f"subscribe notify: {CHAR_UUID}")
        await client.start_notify(CHAR_UUID, notify_handler)

        print(f"write: {payload.hex(' ').upper()}")
        await client.write_gatt_char(CHAR_UUID, payload, response=False)

        await asyncio.sleep(args.wait)
        await client.stop_notify(CHAR_UUID)

    return 0


if __name__ == "__main__":
    raise SystemExit(asyncio.run(main()))
