import select
import sys

_pending_pause = False

try:
    import termios
    _HAS_TERMIOS = True
except ImportError:
    _HAS_TERMIOS = False


def _flush_stdin():
    if not sys.stdin.isatty() or not _HAS_TERMIOS:
        return
    try:
        termios.tcflush(sys.stdin, termios.TCIFLUSH)
    except termios.error:
        pass


def check_pause_key() -> bool:
    global _pending_pause
    if not sys.stdin.isatty():
        return False
    if _pending_pause:
        return True
    if select.select([sys.stdin], [], [], 0)[0]:
        line = sys.stdin.readline().strip()
        if line.lower() == 's':
            _pending_pause = True
            print("\n🔔 检测到暂停请求(s)，当前 round 完成后暂停...", flush=True)
            return True
    return False


def consume_pause():
    global _pending_pause
    _pending_pause = False
    _flush_stdin()


def wait_for_resume(pause_count: int) -> None:
    print(f"\n⏸  已暂停（第{pause_count}次）。移动节点后按 y 重新采集并继续...", flush=True)
    while True:
        line = sys.stdin.readline()
        if not line:
            print("\n⚠️  stdin 关闭，自动恢复测试", flush=True)
            break
        if line.strip().lower() == 'y':
            print("▶  重新采集 RSSI 并继续...", flush=True)
            break


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
