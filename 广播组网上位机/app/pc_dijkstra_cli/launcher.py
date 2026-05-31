from __future__ import annotations

import argparse
import errno
import sys
import webbrowser
from pathlib import Path

from .ui_server import DEFAULT_LOG_ROOT, create_server


def create_server_auto_port(host: str, port: int, log_root: str | Path, attempts: int = 20):
    """Create the UI server, moving upward from port when the port is busy."""
    if port == 0:
        return create_server(host, 0, log_root)

    last_error: OSError | None = None
    for candidate in range(port, port + max(1, attempts)):
        try:
            return create_server(host, candidate, log_root)
        except OSError as exc:
            last_error = exc
            if exc.errno not in {errno.EADDRINUSE, errno.EACCES}:
                raise
    raise RuntimeError(f"no available UI port from {port} to {port + attempts - 1}") from last_error


def launch_ui(
    host: str = "127.0.0.1",
    port: int = 8080,
    log_root: str | Path = DEFAULT_LOG_ROOT,
    open_browser: bool = True,
    attempts: int = 20,
) -> None:
    server = create_server_auto_port(host, port, log_root, attempts=attempts)
    actual_host, actual_port = server.server_address[:2]
    browser_host = "127.0.0.1" if actual_host in {"", "0.0.0.0"} else actual_host
    url = f"http://{browser_host}:{actual_port}/"
    print(f"Dijkstra 上位机 UI 已启动: {url}")
    print(f"日志目录: {Path(log_root).resolve()}")
    print("关闭窗口或按 Ctrl+C 可停止服务。")
    if open_browser:
        webbrowser.open(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nUI server stopped.")
    finally:
        server.server_close()


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Launch the Dijkstra upper-computer Web UI")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    parser.add_argument("--log-root", default=str(DEFAULT_LOG_ROOT))
    parser.add_argument("--no-browser", action="store_true", help="start server without opening a browser")
    parser.add_argument("--port-attempts", type=int, default=20, help="number of ports to try from --port upward")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        launch_ui(
            host=args.host,
            port=args.port,
            log_root=args.log_root,
            open_browser=not args.no_browser,
            attempts=args.port_attempts,
        )
        return 0
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
