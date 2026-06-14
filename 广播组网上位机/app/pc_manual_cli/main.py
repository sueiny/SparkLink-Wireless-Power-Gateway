"""手动路径测试 CLI

用法示例：
  python -m pc_manual_cli --port COM3 --path "00 01 06 04"
  python -m pc_manual_cli --port /dev/ttyUSB0 --interactive
"""
from __future__ import annotations

import argparse
import statistics
import time

from pc_dijkstra_cli.protocol import Ack, ProtocolError, build_send_command, parse_addr
from pc_dijkstra_cli.serial_client import SerialClient


def _send_once(
    client: SerialClient,
    dst: int,
    path: list[int],
    payload: str,
    ack_timeout: float,
) -> tuple[bool, float | None, int | None]:
    command = build_send_command(dst, path, payload)
    client.write_command(command)
    started = time.monotonic()
    deadline = started + ack_timeout
    while time.monotonic() < deadline:
        for msg in client.read_available():
            if isinstance(msg, Ack) and msg.src_addr == dst:
                latency_ms = (time.monotonic() - started) * 1000.0
                return True, latency_ms, msg.seq
    return False, None, None


def _run_path(client: SerialClient, path_str: str, payload: str, rounds: int, interval: float, ack_timeout: float) -> None:
    try:
        path = [parse_addr(x) for x in path_str.split()]
    except (ProtocolError, ValueError) as exc:
        print(f"路径解析失败: {exc}")
        return
    if len(path) < 2:
        print("路径至少需要 2 个节点")
        return

    dst = path[-1]
    path_display = " → ".join(f"{x:02X}" for x in path)
    try:
        cmd_preview = build_send_command(dst, path, payload)
    except ProtocolError as exc:
        print(f"构建指令失败: {exc}")
        return

    print(f"路径: {path_display}   指令: {cmd_preview.rstrip()}")

    success_count = 0
    latencies: list[float] = []
    for i in range(1, rounds + 1):
        print(f"  [{i}/{rounds}]  发送... ", end="", flush=True)
        ok, latency_ms, seq = _send_once(client, dst, path, payload, ack_timeout)
        if ok:
            success_count += 1
            latencies.append(latency_ms)
            print(f"✓  ACK seq=0x{seq:04X}  {latency_ms:.1f}ms")
        else:
            print(f"✗  超时 ({ack_timeout}s)")
        if i < rounds:
            time.sleep(interval)

    loss_rate = 1.0 - success_count / rounds
    summary = f"结果: {success_count}/{rounds} 成功  丢包率: {loss_rate:.1%}"
    if latencies:
        summary += f"  均值: {statistics.fmean(latencies):.1f}ms  最小: {min(latencies):.1f}ms  最大: {max(latencies):.1f}ms"
    print(summary)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="手动指定路径发包，测试特定路径的连通性和延时",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例：
  python -m pc_manual_cli --port /dev/ttyUSB0 --path "00 01 06 04"
  python -m pc_manual_cli --port /dev/ttyUSB0 --path "00 01 06 04" --rounds 10 --interval 0.5
  python -m pc_manual_cli --port /dev/ttyUSB0 --interactive
        """,
    )
    parser.add_argument("--port", required=True, help="串口号，如 COM3 或 /dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--path", help='路径，空格分隔的十六进制节点，如 "00 01 06 04"')
    parser.add_argument("--payload", default="AABBCC", help="十六进制 payload，默认 AABBCC")
    parser.add_argument("--rounds", type=int, default=1, help="发包轮次，默认 1")
    parser.add_argument("--interval", type=float, default=1.0, help="每轮间隔秒数，默认 1.0s")
    parser.add_argument("--ack-timeout", type=float, default=2.0, help="单次 ACK 等待超时，默认 2.0s")
    parser.add_argument("--interactive", action="store_true", help="交互模式：串口只开一次，循环输入路径")

    args = parser.parse_args(argv)

    if not args.interactive and not args.path:
        parser.error("非交互模式下必须指定 --path")

    print(f"正在连接串口 {args.port}，请稍候...")
    client = SerialClient(args.port, args.baud)
    print("串口已连接。")

    try:
        if args.interactive:
            print("交互模式：输入路径发包，输入 quit 退出。")
            print(f"默认参数：rounds={args.rounds}  interval={args.interval}s  ack-timeout={args.ack_timeout}s  payload={args.payload}")
            print()
            while True:
                try:
                    line = input("路径> ").strip()
                except EOFError:
                    break
                if not line:
                    continue
                if line.lower() in ("quit", "exit", "q"):
                    break
                _run_path(client, line, args.payload, args.rounds, args.interval, args.ack_timeout)
                print()
        else:
            _run_path(client, args.path, args.payload, args.rounds, args.interval, args.ack_timeout)
    except KeyboardInterrupt:
        print("\n中断。")
    finally:
        client.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
