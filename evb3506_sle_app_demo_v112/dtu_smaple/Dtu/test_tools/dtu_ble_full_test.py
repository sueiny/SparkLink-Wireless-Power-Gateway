#!/usr/bin/env python3
"""DTU BLE 全流程自动化测试脚本。

目标：
1. 按 docs/DTU完整测试流程.md 覆盖配置模式与运行模式测试。
2. 每条命令都做协议级校验（帧头/CRC/SEQ/RESP_CMD/status）。
3. 发现错误即时输出，最终给出统计与非 0 退出码。

依赖：
    python3 -m pip install bleak

示例：
    # 仅跑配置模式（请先把 GPIO13 拨到高电平并重启设备）
    python3 dtu_ble_full_test.py --suite config --name DTU

    # 跑运行模式（请先把 GPIO13 拨到低电平并重启设备）
    python3 dtu_ble_full_test.py --suite run --name DTU

    # 跑完整流程（脚本会提示你在中途手动切拨码）
    python3 dtu_ble_full_test.py --suite all --name DTU

        仅配置模式（先拨到 CONFIG）：
    python3 dtu_ble_full_test.py --suite config --name DTU

    仅运行模式（先拨到 RUN）：
    python3 dtu_ble_full_test.py --suite run --name DTU

    完整全测试（脚本中途会提示你切拨码）：
    python3 dtu_ble_full_test.py --suite all --name DTU

    失败即停：
    python3 dtu_ble_full_test.py --suite all --name DTU --fail-fast
"""

from __future__ import annotations

import argparse
import asyncio
import dataclasses
import sys
import time
from typing import Awaitable, Callable, Dict, List, Optional, Sequence, Tuple

try:
    from bleak import BleakClient, BleakScanner
except Exception as exc:  # pragma: no cover - import 错误直接给出友好提示
    print("[FATAL] 无法导入 bleak，请先安装依赖: python3 -m pip install bleak")
    print(f"[DETAIL] {exc}")
    raise SystemExit(2)


SERVICE_UUID = "0000FDF0-0000-1000-8000-00805F9B34FB"
CHAR_UUID = "0000FDF1-0000-1000-8000-00805F9B34FB"

SOF0 = 0xAA
SOF1 = 0x55

MODE_CONFIG = 0x00
MODE_RUN = 0x01
ROLE_NODE = 0x00
ROLE_ROOT = 0x01

STATUS: Dict[int, str] = {
    0x00: "SUCC",
    0x01: "CRC_ERR",
    0x02: "LEN_ERR",
    0x03: "CMD_ERR",
    0x04: "PARAM_ERR",
    0x05: "NOT_CONFIG",
    0x06: "ROLE_MISMATCH",
    0x07: "WL_FULL",
    0x08: "NOT_FOUND",
    0x09: "SAVE_FAIL",
    0x0A: "BUSY",
}

CMD = {
    "READ_DEV_INFO": 0x01,
    "READ_UART_CFG": 0x02,
    "READ_MODBUS_CFG": 0x03,
    "READ_ROOT_WL_ALL": 0x04,
    "READ_ROOT_POWER": 0x05,
    "GET_MODE_STATUS": 0x06,
    "READ_WL_NODE_CFG": 0x07,
    "SET_ROLE": 0x10,
    "SET_UART_CFG": 0x11,
    "SET_MODBUS_CFG": 0x12,
    "SET_ROOT_POWER": 0x13,
    "ADD_WL_ITEM": 0x14,
    "DEL_WL_ITEM": 0x15,
    "CLEAR_WL": 0x16,
    "SET_WL_NODE_CFG": 0x17,
    "COMMIT": 0x20,
    "REBOOT": 0x21,
    "FACTORY_RESET": 0x22,
}

WL_MAC = bytes([0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6])


class TestFailure(Exception):
    pass


@dataclasses.dataclass
class ParsedFrame:
    cmd: int
    seq: int
    body: bytes

    @property
    def status(self) -> Optional[int]:
        return self.body[0] if self.body else None


@dataclasses.dataclass
class Counters:
    passed: int = 0
    failed: int = 0


class StepRunner:
    def __init__(self, fail_fast: bool = False) -> None:
        self.counters = Counters()
        self.fail_fast = fail_fast

    def ok(self, name: str, detail: str = "") -> None:
        self.counters.passed += 1
        suffix = f" | {detail}" if detail else ""
        print(f"[PASS] {name}{suffix}")

    def fail(self, name: str, detail: str) -> None:
        self.counters.failed += 1
        print(f"[FAIL] {name} | {detail}")
        if self.fail_fast:
            raise TestFailure(f"{name}: {detail}")


def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 0x0001:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc & 0xFFFF


def build_frame(cmd: int, seq: int, body: bytes = b"") -> bytes:
    if len(body) > 0xFFFF:
        raise ValueError("body 太长")
    hdr = bytes([SOF0, SOF1, cmd, seq, len(body) & 0xFF, (len(body) >> 8) & 0xFF])
    crc_data = bytes([cmd, seq, len(body) & 0xFF, (len(body) >> 8) & 0xFF]) + body
    crc = crc16_modbus(crc_data)
    return hdr + body + bytes([crc & 0xFF, (crc >> 8) & 0xFF])


def parse_frame(raw: bytes) -> ParsedFrame:
    if len(raw) < 8:
        raise TestFailure(f"响应长度过短: {len(raw)}")
    if raw[0] != SOF0 or raw[1] != SOF1:
        raise TestFailure(f"SOF 错误: {raw[:2].hex(' ').upper()}")

    cmd = raw[2]
    seq = raw[3]
    body_len = raw[4] | (raw[5] << 8)
    expected_len = 2 + 1 + 1 + 2 + body_len + 2
    if len(raw) != expected_len:
        raise TestFailure(f"响应长度不匹配: got={len(raw)} expect={expected_len}")

    body = raw[6 : 6 + body_len]
    recv_crc = raw[-2] | (raw[-1] << 8)
    calc_crc = crc16_modbus(raw[2:-2])
    if recv_crc != calc_crc:
        raise TestFailure(f"CRC 错误: recv=0x{recv_crc:04X} calc=0x{calc_crc:04X}")

    return ParsedFrame(cmd=cmd, seq=seq, body=body)


class DtuBleTester:
    def __init__(self, args: argparse.Namespace, runner: StepRunner) -> None:
        self.args = args
        self.runner = runner
        self.queue: "asyncio.Queue[bytes]" = asyncio.Queue()
        self.client: Optional[BleakClient] = None
        self.seq = args.seq_start & 0xFF
        self.target: Optional[str] = None

    async def discover(self) -> str:
        if self.args.address:
            return self.args.address

        devices = await BleakScanner.discover(timeout=self.args.scan_timeout)
        for dev in devices:
            name = dev.name or ""
            if self.args.name in name:
                return dev.address
        raise TestFailure(f"未找到设备，名称关键字={self.args.name!r}")

    def _notify_cb(self, _: int, data: bytearray) -> None:
        payload = bytes(data)
        print(f"[NOTIFY] {payload.hex(' ').upper()}")
        self.queue.put_nowait(payload)

    async def connect(self) -> None:
        self.target = await self.discover()
        print(f"[INFO] connect -> {self.target}")
        self.client = BleakClient(self.target)
        await self.client.connect(timeout=self.args.connect_timeout)
        if not self.client.is_connected:
            raise TestFailure("BLE 连接失败")

        await self.client.start_notify(CHAR_UUID, self._notify_cb)
        await self.drain_queue()
        self.runner.ok("BLE连接+Notify", f"addr={self.target}")

    async def disconnect(self) -> None:
        if self.client is None:
            return
        try:
            if self.client.is_connected:
                try:
                    await self.client.stop_notify(CHAR_UUID)
                except Exception:
                    pass
                await self.client.disconnect()
        finally:
            self.client = None

    async def reconnect(self, wait_sec: float = 1.0, retry: int = 8) -> None:
        await self.disconnect()
        for i in range(retry):
            try:
                await asyncio.sleep(wait_sec)
                await self.connect()
                self.runner.ok("重连设备", f"attempt={i + 1}")
                return
            except Exception as exc:
                print(f"[WARN] 重连失败 attempt={i + 1}: {exc}")
        raise TestFailure("设备重连失败")

    async def drain_queue(self) -> None:
        while not self.queue.empty():
            _ = self.queue.get_nowait()

    def next_seq(self) -> int:
        seq = self.seq
        self.seq = (self.seq + 1) & 0xFF
        return seq

    async def send_cmd(
        self,
        name: str,
        cmd: int,
        body: bytes = b"",
        expect_status: Optional[int] = 0x00,
        timeout: Optional[float] = None,
    ) -> ParsedFrame:
        if self.client is None or not self.client.is_connected:
            raise TestFailure("设备未连接")

        seq = self.next_seq()
        frame = build_frame(cmd, seq, body)
        await self.drain_queue()

        print(f"[SEND] {name}: {frame.hex(' ').upper()}")
        await self.client.write_gatt_char(CHAR_UUID, frame, response=False)

        parsed = await self.wait_resp(name, cmd, seq, timeout=timeout)
        if expect_status is not None:
            got = parsed.status
            if got != expect_status:
                got_name = STATUS.get(got if got is not None else -1, "UNKNOWN")
                exp_name = STATUS.get(expect_status, "UNKNOWN")
                raise TestFailure(
                    f"status不符: expect=0x{expect_status:02X}({exp_name}) "
                    f"got=0x{(got if got is not None else 0xFF):02X}({got_name})"
                )
        return parsed

    async def wait_resp(self, name: str, req_cmd: int, seq: int, timeout: Optional[float] = None) -> ParsedFrame:
        deadline = time.monotonic() + (timeout if timeout is not None else self.args.resp_timeout)
        exp_cmd = req_cmd | 0x80

        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TestFailure(f"{name} 等待响应超时")
            try:
                raw = await asyncio.wait_for(self.queue.get(), timeout=remaining)
                parsed = parse_frame(raw)
            except asyncio.TimeoutError:
                raise TestFailure(f"{name} 等待响应超时")

            if parsed.seq != seq or parsed.cmd != exp_cmd:
                print(
                    f"[WARN] 收到不匹配响应，忽略: "
                    f"cmd=0x{parsed.cmd:02X} seq=0x{parsed.seq:02X} "
                    f"expect_cmd=0x{exp_cmd:02X} expect_seq=0x{seq:02X}"
                )
                continue
            return parsed

    async def read_root_wl_all(self, expect_status: int) -> List[ParsedFrame]:
        first = await self.send_cmd(
            "READ_ROOT_WL_ALL",
            CMD["READ_ROOT_WL_ALL"],
            body=b"",
            expect_status=expect_status,
        )
        if expect_status != 0x00:
            return [first]

        if len(first.body) < 5:
            raise TestFailure("READ_ROOT_WL_ALL 成功响应长度过短")

        frag_idx = first.body[1]
        frag_total = first.body[2]
        frames = [first]
        if frag_idx != 1:
            raise TestFailure(f"READ_ROOT_WL_ALL 首片frag_idx异常: {frag_idx}")
        self._check_wl_fragment(first)

        # 多片时继续收同一 seq/cmd 的后续通知
        while len(frames) < frag_total:
            parsed = await self.wait_resp(
                "READ_ROOT_WL_ALL(fragment)",
                req_cmd=CMD["READ_ROOT_WL_ALL"],
                seq=first.seq,
                timeout=self.args.resp_timeout,
            )
            if not parsed.body:
                raise TestFailure("READ_ROOT_WL_ALL 分片响应空body")
            if parsed.body[0] != 0x00:
                raise TestFailure(f"READ_ROOT_WL_ALL 分片status异常: 0x{parsed.body[0]:02X}")
            self._check_wl_fragment(parsed)
            frames.append(parsed)

        return frames

    @staticmethod
    def _check_wl_fragment(frame: ParsedFrame) -> None:
        if len(frame.body) < 5:
            raise TestFailure("READ_ROOT_WL_ALL 分片长度过短")
        item_count = frame.body[4]
        payload_len = len(frame.body) - 5
        if payload_len != item_count * 6:
            raise TestFailure(
                f"READ_ROOT_WL_ALL V2 item长度异常: payload={payload_len} item_count={item_count}"
            )


async def run_config_suite(t: DtuBleTester, runner: StepRunner) -> None:
    print("\n===== 配置模式测试开始 =====")

    # 1) 基础读取
    try:
        rsp = await t.send_cmd("READ_DEV_INFO", CMD["READ_DEV_INFO"])
        if len(rsp.body) < 1 + 1 + 6 + 1:
            raise TestFailure("READ_DEV_INFO 响应体长度异常")
        runner.ok("READ_DEV_INFO", f"status={STATUS.get(rsp.status or 0, 'UNKNOWN')}")
    except Exception as exc:
        runner.fail("READ_DEV_INFO", str(exc))

    try:
        rsp = await t.send_cmd("READ_UART_CFG", CMD["READ_UART_CFG"])
        if len(rsp.body) != 5:
            raise TestFailure(f"READ_UART_CFG body长度应为5，got={len(rsp.body)}")
        runner.ok("READ_UART_CFG", f"baud={rsp.body[1]} parity={rsp.body[2]} stop={rsp.body[3]} data={rsp.body[4]}")
    except Exception as exc:
        runner.fail("READ_UART_CFG", str(exc))

    try:
        rsp = await t.send_cmd("READ_MODBUS_CFG", CMD["READ_MODBUS_CFG"])
        if len(rsp.body) < 2:
            raise TestFailure("READ_MODBUS_CFG body长度过短")
        cnt = rsp.body[1]
        if len(rsp.body) != 2 + cnt * 2:
            raise TestFailure(f"READ_MODBUS_CFG 长度不匹配: count={cnt}, len={len(rsp.body)}")
        if cnt != 8:
            raise TestFailure(f"READ_MODBUS_CFG 默认项数量异常: expect=8 got={cnt}")
        for i in range(8):
            addr = rsp.body[2 + i * 2]
            dev_type = rsp.body[3 + i * 2]
            if addr != i or dev_type != 0x05:
                raise TestFailure(
                    f"READ_MODBUS_CFG 默认项异常: idx={i} expect=({i},0x05) got=({addr},0x{dev_type:02X})"
                )
        runner.ok("READ_MODBUS_CFG", f"item_count={cnt}")
    except Exception as exc:
        runner.fail("READ_MODBUS_CFG", str(exc))

    try:
        rsp = await t.send_cmd("GET_MODE_STATUS", CMD["GET_MODE_STATUS"])
        if len(rsp.body) != 10:
            raise TestFailure(f"GET_MODE_STATUS body长度应为10，got={len(rsp.body)}")
        if rsp.body[1] != MODE_CONFIG:
            raise TestFailure(f"当前模式不是CONFIG: mode=0x{rsp.body[1]:02X}")
        runner.ok("GET_MODE_STATUS(CONFIG)", f"mode={rsp.body[1]} role={rsp.body[2]}")
    except Exception as exc:
        runner.fail("GET_MODE_STATUS(CONFIG)", str(exc))

    # 2) NODE 配置保存
    try:
        await t.send_cmd("SET_ROLE(NODE)", CMD["SET_ROLE"], body=bytes([ROLE_NODE]))
        runner.ok("SET_ROLE(NODE)")
    except Exception as exc:
        runner.fail("SET_ROLE(NODE)", str(exc))

    try:
        await t.send_cmd("SET_UART_CFG", CMD["SET_UART_CFG"], body=bytes([0x07, 0x00, 0x01, 0x08]))
        runner.ok("SET_UART_CFG(115200,none,1,8)")
    except Exception as exc:
        runner.fail("SET_UART_CFG", str(exc))

    try:
        await t.send_cmd("SET_MODBUS_CFG(2 items)", CMD["SET_MODBUS_CFG"], body=bytes([0x02, 0x01, 0x02, 0x05, 0x03]))
        runner.ok("SET_MODBUS_CFG(2 items)")
    except Exception as exc:
        runner.fail("SET_MODBUS_CFG(2 items)", str(exc))

    # 3) ROOT 专属
    try:
        await t.send_cmd("SET_ROLE(ROOT)", CMD["SET_ROLE"], body=bytes([ROLE_ROOT]))
        runner.ok("SET_ROLE(ROOT)")
    except Exception as exc:
        runner.fail("SET_ROLE(ROOT)", str(exc))

    try:
        await t.send_cmd("SET_ROOT_POWER(5)", CMD["SET_ROOT_POWER"], body=bytes([0x05]))
        runner.ok("SET_ROOT_POWER(5)")
    except Exception as exc:
        runner.fail("SET_ROOT_POWER(5)", str(exc))

    wl_body = WL_MAC
    try:
        await t.send_cmd("ADD_WL_ITEM", CMD["ADD_WL_ITEM"], body=wl_body)
        runner.ok("ADD_WL_ITEM")
    except Exception as exc:
        runner.fail("ADD_WL_ITEM", str(exc))

    try:
        frames = await t.read_root_wl_all(expect_status=0x00)
        runner.ok("READ_ROOT_WL_ALL", f"fragments={len(frames)}")
    except Exception as exc:
        runner.fail("READ_ROOT_WL_ALL", str(exc))

    try:
        rsp = await t.send_cmd("READ_WL_NODE_CFG(default)", CMD["READ_WL_NODE_CFG"], body=WL_MAC)
        if len(rsp.body) < 6:
            raise TestFailure(f"READ_WL_NODE_CFG body长度过短: {len(rsp.body)}")
        if rsp.body[1:5] != bytes([0x07, 0x00, 0x01, 0x08]):
            raise TestFailure(f"READ_WL_NODE_CFG 默认uart异常: {rsp.body[1:5].hex(' ').upper()}")
        node_cnt = rsp.body[5]
        if node_cnt != 2:
            # 当前脚本前面已把 ROOT runtime 改成 2 项，新增白名单项应继承这 2 项
            raise TestFailure(f"READ_WL_NODE_CFG 默认modbus数量异常: expect=2 got={node_cnt}")
        if len(rsp.body) != 6 + node_cnt * 2:
            raise TestFailure(f"READ_WL_NODE_CFG 长度不匹配: count={node_cnt}, len={len(rsp.body)}")
        expected_pairs = [(0x01, 0x02), (0x05, 0x03)]
        for i, (exp_addr, exp_type) in enumerate(expected_pairs):
            addr = rsp.body[6 + i * 2]
            dev_type = rsp.body[7 + i * 2]
            if addr != exp_addr or dev_type != exp_type:
                raise TestFailure(
                    f"READ_WL_NODE_CFG 默认继承异常: idx={i} "
                    f"expect=({exp_addr},0x{exp_type:02X}) got=({addr},0x{dev_type:02X})"
                )
        runner.ok("READ_WL_NODE_CFG(default)", f"item_count={node_cnt}")
    except Exception as exc:
        runner.fail("READ_WL_NODE_CFG(default)", str(exc))

    try:
        node_body = WL_MAC + bytes([0x07, 0x00, 0x01, 0x08, 0x02, 0x01, 0x05, 0x02, 0x05])
        await t.send_cmd("SET_WL_NODE_CFG", CMD["SET_WL_NODE_CFG"], body=node_body)
        runner.ok("SET_WL_NODE_CFG")
    except Exception as exc:
        runner.fail("SET_WL_NODE_CFG", str(exc))

    try:
        rsp = await t.send_cmd("READ_WL_NODE_CFG(after set)", CMD["READ_WL_NODE_CFG"], body=WL_MAC)
        if len(rsp.body) != 10:
            raise TestFailure(f"READ_WL_NODE_CFG(after set) 长度异常: {len(rsp.body)}")
        if rsp.body[1:5] != bytes([0x07, 0x00, 0x01, 0x08]):
            raise TestFailure(f"READ_WL_NODE_CFG(after set) uart异常: {rsp.body[1:5].hex(' ').upper()}")
        if rsp.body[5] != 2 or rsp.body[6:10] != bytes([0x01, 0x05, 0x02, 0x05]):
            raise TestFailure(f"READ_WL_NODE_CFG(after set) modbus异常: {rsp.body[5:].hex(' ').upper()}")
        runner.ok("READ_WL_NODE_CFG(after set)", "node cfg updated")
    except Exception as exc:
        runner.fail("READ_WL_NODE_CFG(after set)", str(exc))

    try:
        rsp = await t.send_cmd("READ_ROOT_POWER", CMD["READ_ROOT_POWER"], body=b"")
        if len(rsp.body) != 2:
            raise TestFailure(f"READ_ROOT_POWER body长度应为2，got={len(rsp.body)}")
        runner.ok("READ_ROOT_POWER", f"power={rsp.body[1]}")
    except Exception as exc:
        runner.fail("READ_ROOT_POWER", str(exc))

    # 4) 删除与清空
    try:
        await t.send_cmd("DEL_WL_ITEM", CMD["DEL_WL_ITEM"], body=WL_MAC)
        runner.ok("DEL_WL_ITEM")
    except Exception as exc:
        runner.fail("DEL_WL_ITEM", str(exc))

    try:
        await t.send_cmd("ADD_WL_ITEM(re-add)", CMD["ADD_WL_ITEM"], body=wl_body)
        await t.send_cmd("CLEAR_WL", CMD["CLEAR_WL"], body=b"")
        runner.ok("CLEAR_WL")
    except Exception as exc:
        runner.fail("CLEAR_WL", str(exc))

    # 5) COMMIT + REBOOT + 重连验证
    try:
        await t.send_cmd("COMMIT", CMD["COMMIT"], body=b"")
        runner.ok("COMMIT")
    except Exception as exc:
        runner.fail("COMMIT", str(exc))

    try:
        await t.send_cmd("REBOOT", CMD["REBOOT"], body=b"")
        runner.ok("REBOOT命令应答")
    except Exception as exc:
        runner.fail("REBOOT命令应答", str(exc))

    try:
        await t.reconnect(wait_sec=t.args.reboot_wait, retry=t.args.reconnect_retry)
        # 重启后快速抽检
        rsp = await t.send_cmd("GET_MODE_STATUS(after reboot)", CMD["GET_MODE_STATUS"], body=b"")
        if len(rsp.body) != 10:
            raise TestFailure("重启后 GET_MODE_STATUS 格式异常")
        runner.ok("重启后状态读取", f"mode={rsp.body[1]} role={rsp.body[2]}")
    except Exception as exc:
        runner.fail("重启后状态读取", str(exc))

    # 6) 恢复出厂
    try:
        await t.send_cmd("FACTORY_RESET", CMD["FACTORY_RESET"], body=b"")
        runner.ok("FACTORY_RESET")
    except Exception as exc:
        runner.fail("FACTORY_RESET", str(exc))


async def run_run_suite(t: DtuBleTester, runner: StepRunner) -> None:
    print("\n===== 运行模式测试开始 =====")

    try:
        rsp = await t.send_cmd("GET_MODE_STATUS", CMD["GET_MODE_STATUS"])
        if len(rsp.body) != 10:
            raise TestFailure(f"GET_MODE_STATUS body长度应为10，got={len(rsp.body)}")
        if rsp.body[1] != MODE_RUN:
            raise TestFailure(f"当前模式不是RUN: mode=0x{rsp.body[1]:02X}")
        runner.ok("GET_MODE_STATUS(RUN)", f"mode={rsp.body[1]}")
    except Exception as exc:
        runner.fail("GET_MODE_STATUS(RUN)", str(exc))

    reject_cmds: Sequence[Tuple[str, int, bytes]] = [
        ("READ_DEV_INFO", CMD["READ_DEV_INFO"], b""),
        ("READ_UART_CFG", CMD["READ_UART_CFG"], b""),
        ("READ_MODBUS_CFG", CMD["READ_MODBUS_CFG"], b""),
        ("READ_ROOT_WL_ALL", CMD["READ_ROOT_WL_ALL"], b""),
        ("READ_ROOT_POWER", CMD["READ_ROOT_POWER"], b""),
        ("READ_WL_NODE_CFG", CMD["READ_WL_NODE_CFG"], WL_MAC),
        ("SET_ROLE", CMD["SET_ROLE"], bytes([ROLE_NODE])),
        ("SET_UART_CFG", CMD["SET_UART_CFG"], bytes([0x07, 0x00, 0x01, 0x08])),
        ("SET_MODBUS_CFG", CMD["SET_MODBUS_CFG"], bytes([0x01, 0x01, 0x02])),
        ("SET_ROOT_POWER", CMD["SET_ROOT_POWER"], bytes([0x05])),
        ("ADD_WL_ITEM", CMD["ADD_WL_ITEM"], WL_MAC),
        ("DEL_WL_ITEM", CMD["DEL_WL_ITEM"], WL_MAC),
        ("CLEAR_WL", CMD["CLEAR_WL"], b""),
        ("SET_WL_NODE_CFG", CMD["SET_WL_NODE_CFG"], WL_MAC + bytes([0x07, 0x00, 0x01, 0x08, 0x01, 0x01, 0x05])),
        ("COMMIT", CMD["COMMIT"], b""),
        ("FACTORY_RESET", CMD["FACTORY_RESET"], b""),
    ]

    for name, cmd, body in reject_cmds:
        case_name = f"RUN拒配-{name}"
        try:
            await t.send_cmd(case_name, cmd, body=body, expect_status=0x05)
            runner.ok(case_name, "status=NOT_CONFIG")
        except Exception as exc:
            runner.fail(case_name, str(exc))

    try:
        await t.send_cmd("RUN下REBOOT", CMD["REBOOT"], body=b"")
        runner.ok("RUN下REBOOT")
    except Exception as exc:
        runner.fail("RUN下REBOOT", str(exc))


def ask_enter(prompt: str) -> None:
    print(f"\n[ACTION] {prompt}")
    input("按 Enter 继续...")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="DTU BLE 全流程自动化测试")
    parser.add_argument("--name", default="DTU", help="BLE 设备名关键字")
    parser.add_argument("--address", default="", help="指定 BLE 地址，填了就不扫描")
    parser.add_argument("--scan-timeout", type=float, default=6.0, help="扫描超时秒数")
    parser.add_argument("--connect-timeout", type=float, default=10.0, help="连接超时秒数")
    parser.add_argument("--resp-timeout", type=float, default=3.0, help="等待响应超时秒数")
    parser.add_argument("--reboot-wait", type=float, default=1.2, help="重启后每次重连前等待秒数")
    parser.add_argument("--reconnect-retry", type=int, default=8, help="重连重试次数")
    parser.add_argument("--seq-start", type=lambda s: int(s, 0), default=1, help="起始序列号(支持0x前缀)")
    parser.add_argument(
        "--suite",
        choices=["config", "run", "all"],
        default="all",
        help="测试套件：config/run/all",
    )
    parser.add_argument("--fail-fast", action="store_true", help="遇到首个失败即退出")
    parser.add_argument("--yes", action="store_true", help="跳过人工确认提示")
    return parser.parse_args()


async def main_async() -> int:
    args = parse_args()
    runner = StepRunner(fail_fast=args.fail_fast)
    tester = DtuBleTester(args, runner)

    try:
        if args.suite in ("config", "all") and not args.yes:
            ask_enter("请将 GPIO13 拨到高电平(CONFIG)并重启设备")

        await tester.connect()

        if args.suite in ("config", "all"):
            await run_config_suite(tester, runner)

        if args.suite == "all":
            await tester.disconnect()
            if not args.yes:
                ask_enter("请将 GPIO13 拨到低电平(RUN)并重启设备")
            await tester.connect()

        if args.suite in ("run", "all"):
            await run_run_suite(tester, runner)

    except TestFailure as exc:
        runner.fail("测试中止", str(exc))
    except KeyboardInterrupt:
        runner.fail("测试中止", "用户中断")
    except Exception as exc:
        runner.fail("测试中止", f"未捕获异常: {exc}")
    finally:
        await tester.disconnect()

    print("\n===== 测试总结 =====")
    print(f"PASS: {runner.counters.passed}")
    print(f"FAIL: {runner.counters.failed}")

    return 0 if runner.counters.failed == 0 else 1


def main() -> int:
    return asyncio.run(main_async())


if __name__ == "__main__":
    raise SystemExit(main())
