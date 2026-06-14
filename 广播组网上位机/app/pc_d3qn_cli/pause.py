"""D3QN bench 键盘控制：s=暂停/恢复，y=停止→重采 RSSI→继续。"""
from __future__ import annotations

import select
import sys

try:
    import termios
    _HAS_TERMIOS = True
except ImportError:
    _HAS_TERMIOS = False


def _flush_stdin() -> None:
    if not sys.stdin.isatty() or not _HAS_TERMIOS:
        return
    try:
        termios.tcflush(sys.stdin, termios.TCIFLUSH)
    except termios.error:
        pass


def poll_key() -> str | None:
    """非阻塞：返回 's' / 'y' / None。"""
    if not sys.stdin.isatty():
        return None
    if select.select([sys.stdin], [], [], 0)[0]:
        line = sys.stdin.readline().strip().lower()
        if line in ("s", "y"):
            return line
    return None


def wait_for_key() -> str:
    """阻塞等待 s 或 y，返回按下的键。stdin 关闭时返回 's'（自动恢复）。"""
    while True:
        line = sys.stdin.readline()
        if not line:
            return "s"
        key = line.strip().lower()
        if key in ("s", "y"):
            _flush_stdin()
            return key


def wait_for_dynamic_continue(phase_done: int, total_phases: int) -> None:
    print(f"\n⏸  [动态模式] 第 {phase_done}/{total_phases} 阶段完成，按 s 继续下一阶段...", flush=True)
    if not sys.stdin.isatty():
        print("⚠️  非交互终端，自动继续", flush=True)
        return
    _flush_stdin()
    while True:
        line = sys.stdin.readline()
        if not line:
            print("\n⚠️  stdin 关闭，自动继续", flush=True)
            break
        if line.strip().lower() == 's':
            print("▶  继续...", flush=True)
            break
