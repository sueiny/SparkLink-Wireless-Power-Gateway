#!/usr/bin/env python3
"""独立节点存活检查 — 广播 RSSI_REQ，统计哪些节点“自证存活”。

强存活信号 = 节点作为 RSSI_REPORT 的 src_addr 出现（它自己发了完整报告并回到 dongle）。
弱信号     = 节点只在别人的报告里作为邻居出现（被听到，但自己没上报，可能已掉线）。

设计要点（针对交接代码已知问题）：
  * 只用 src_addr 强信号判存活，不依赖 topology.nodes（后者混入了邻居地址，有假阳性）。
  * 对每次 read 的 ProtocolError 做容错：一帧脏数据不再毒死同批其它节点的报告。
  * 区分 ALIVE / (弱) / MISSING 三态。

约束：独占串口，禁止与 bench/sweep/optimize/listen 同时运行。
不修改 pc_dijkstra_cli 包，纯新增诊断工具。
"""
from __future__ import annotations

import argparse
import sys
import time
from collections import defaultdict

from pc_dijkstra_cli.serial_client import SerialClient
from pc_dijkstra_cli.protocol import RssiReport, Ack, ProtocolError


def main() -> int:
    ap = argparse.ArgumentParser(description="广播 RSSI_REQ，汇总每个节点是否自证存活")
    ap.add_argument("--port", default="/dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--nodes", default="0,1,2,3,4,5,6,7,8,9,10",
                    help="期望节点地址(十进制,逗号分隔),默认 0..10")
    ap.add_argument("--dongle-addr", type=int, default=16,
                    help="dongle 地址(十进制),从期望集合中排除,默认 16(0x10)")
    ap.add_argument("--requests", type=int, default=10, help="最多发送 RSSI_REQ 次数")
    ap.add_argument("--window", type=float, default=20.0, help="最长窗口秒数")
    ap.add_argument("--req-interval", type=float, default=1.5, help="两次 RSSI_REQ 间隔秒")
    args = ap.parse_args()

    expected = [int(x) for x in args.nodes.split(",") if x.strip() != ""]
    expected = [a for a in expected if a != args.dongle_addr]

    print(f"打开 {args.port} @ {args.baud} (含 2s dongle 初始化)...")
    client = SerialClient(args.port, args.baud, timeout=0.1)

    report_count: dict[int, int] = defaultdict(int)       # src -> RSSI_REPORT 次数(强)
    last_neighbors: dict[int, int] = {}                    # src -> 最近一次邻居数
    heard_as_neighbor: dict[int, int] = defaultdict(int)   # addr -> 被当邻居听到次数(弱)
    ack_src: dict[int, int] = defaultdict(int)
    poisoned = 0
    reqs_sent = 0

    t0 = time.monotonic()
    last_req = -1e9
    try:
        while True:
            now = time.monotonic()
            if now - t0 >= args.window:
                break
            # 全部期望节点已自证存活 → 提前结束(至少发过 2 次)
            if reqs_sent >= 2 and all(report_count[a] > 0 for a in expected):
                break
            if reqs_sent < args.requests and now - last_req >= args.req_interval:
                client.send_rssi_req()
                reqs_sent += 1
                last_req = now
            try:
                msgs = client.read_available(4096)
            except ProtocolError:
                poisoned += 1
                msgs = []
            for m in msgs:
                if isinstance(m, RssiReport):
                    report_count[m.src_addr] += 1
                    last_neighbors[m.src_addr] = len(m.neighbors)
                    for nb in m.neighbors:
                        heard_as_neighbor[nb.addr] += 1
                elif isinstance(m, Ack):
                    ack_src[m.src_addr] += 1
            time.sleep(0.03)
    finally:
        client.close()

    elapsed = time.monotonic() - t0
    print(f"\n发送 RSSI_REQ {reqs_sent} 次, 窗口 {elapsed:.1f}s, 跳过脏帧(ProtocolError) {poisoned} 次\n")
    print(f"  {'节点':<6}{'判定':<9}{'报告数':>6}{'邻居数':>7}{'被听到':>7}{'ACK':>6}")
    print("  " + "-" * 41)
    missing: list[int] = []
    for a in expected:
        strong = report_count[a] > 0
        if strong:
            verdict = "ALIVE"
        elif heard_as_neighbor[a] > 0:
            verdict = "(弱)仅被听到"
        else:
            verdict = "MISSING"
        if not strong:
            missing.append(a)
        nb = last_neighbors.get(a, "-")
        print(f"  0x{a:02X}  {verdict:<9}{report_count[a]:>6}{str(nb):>7}{heard_as_neighbor[a]:>7}{ack_src[a]:>6}")

    print()
    if not missing:
        print(f"[PASS] 全部 {len(expected)} 个期望节点都以 src 自证存活 ✅")
        return 0
    weak = [a for a in missing if heard_as_neighbor[a] > 0]
    dead = [a for a in missing if heard_as_neighbor[a] == 0]
    print(f"[FAIL] 未自证存活: {[f'0x{a:02X}' for a in missing]}")
    if weak:
        print(f"  仅被邻居听到(弱信号,未自证): {[f'0x{a:02X}' for a in weak]}")
    if dead:
        print(f"  完全未出现(疑似掉线): {[f'0x{a:02X}' for a in dead]}")
    return 1


if __name__ == "__main__":
    sys.exit(main())
