from __future__ import annotations

import json
import mimetypes
import threading
import time
import traceback
from collections import deque
from dataclasses import asdict
from datetime import datetime
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import unquote, urlparse

from .benchmark import parse_node_list, parse_optional_node_list, resolve_benchmark_log_dir, run_benchmark
from .protocol import Ack, RssiReport, build_send_command, format_addr, parse_addr
from .serial_client import RawSerialEvent, SerialClient
from .topology import Topology, load_topology, routes_to_dict, save_topology
from .topology_svg import build_topology_svg, topology_to_dict, write_topology_svg
from pc_d3qn_cli.benchmark import resolve_log_dir as resolve_d3qn_log_dir
from pc_d3qn_cli.benchmark import run_benchmark as run_d3qn_benchmark
from pc_d3qn_cli.model import LATEST_CHECKPOINT


APP_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_LOG_ROOT = APP_ROOT / "logs"


class UiState:
    def __init__(self, log_root: str | Path = DEFAULT_LOG_ROOT):
        self.log_root = Path(log_root)
        self.lock = threading.Lock()
        self.events: deque[dict] = deque(maxlen=1000)
        self.live_topology = Topology(stale_seconds=None, edge_direction="src_to_neighbor")
        self.bench_thread: threading.Thread | None = None
        self.bench_status = {
            "running": False,
            "status": "idle",
            "message": "",
            "log_dir": None,
            "started_at": None,
            "finished_at": None,
            "summary": None,
            "error": None,
        }
        self.listen_thread: threading.Thread | None = None
        self.listen_stop = threading.Event()
        self.listen_client: SerialClient | None = None
        self.listen_status = {
            "running": False,
            "port": None,
            "baud": None,
            "error": None,
        }

    def add_event(self, event_type: str, **payload) -> None:
        with self.lock:
            self.events.append(
                {
                    "ts": datetime.now().isoformat(timespec="milliseconds"),
                    "type": event_type,
                    **payload,
                }
            )

    def raw_callback(self, event: RawSerialEvent) -> None:
        text = "".join(chr(b) if b in (9, 10, 13) or 32 <= b <= 126 else "." for b in event.data)
        self.add_event(
            "raw_serial",
            direction=event.direction,
            length=len(event.data),
            hex=event.data.hex(" "),
            text=text.strip(),
        )

    def start_bench(self, config: dict) -> dict:
        with self.lock:
            if self.bench_status["running"]:
                raise RuntimeError("benchmark is already running")

            port = str(config.get("port") or "/dev/ttyUSB0")
            algorithm = str(config.get("algorithm") or "dijkstra").strip().lower()
            if algorithm not in {"dijkstra", "d3qn"}:
                raise RuntimeError(f"unsupported algorithm: {algorithm}")
            baud = int(config.get("baud") or 115200)
            nodes_text = str(config.get("nodes") or "1,2,3,4,5,6,7,8,9,10")
            sources_text = str(config.get("sources") or "").strip()
            rounds = int(config.get("rounds") or 20)
            payload = str(config.get("payload") or "AABBCC")
            interval = float(config.get("interval") or 1.0)
            rssi_seconds = 0.0
            rssi_requests = int(config.get("rssi_requests") or 5)
            ack_timeout = float(config.get("ack_timeout") or 2.0)
            route_mode = str(config.get("route_mode") or "baseline_dijkstra")
            boot_wait = float(config.get("boot_wait") or 5.0)
            gateway = parse_addr(str(config.get("gateway") or "00"))
            recollect_consecutive_failures = int(config.get("recollect_consecutive_failures") or 3)
            checkpoint = str(config.get("checkpoint") or LATEST_CHECKPOINT)
            path_loss_degrade_threshold = float(config.get("path_loss_degrade_threshold") or 0.10)
            path_p95_degrade_ms = float(config.get("path_p95_degrade_ms") or 700.0)
            path_avg_degrade_ms = float(config.get("path_avg_degrade_ms") or 220.0)
            path_health_window = int(config.get("path_health_window") or 5)
            log_name = str(config.get("log_name") or "").strip()
            log_root = self.log_root / ("d3qn_hw" if algorithm == "d3qn" else "dijkstra_hw")
            log_dir = log_root
            if log_name:
                log_dir = log_dir / _safe_name(log_name)
            actual_log_dir = resolve_d3qn_log_dir(log_dir) if algorithm == "d3qn" else resolve_benchmark_log_dir(log_dir)

            self.bench_status = {
                "running": True,
                "status": "running",
                "message": "benchmark running",
                "log_dir": str(actual_log_dir),
                "started_at": datetime.now().isoformat(timespec="seconds"),
                "finished_at": None,
                "summary": None,
                "error": None,
            }

        def worker() -> None:
            try:
                run_log_dir = resolve_d3qn_log_dir(log_dir) if algorithm == "d3qn" else resolve_benchmark_log_dir(log_dir)
                with self.lock:
                    self.bench_status["log_dir"] = str(run_log_dir)
                self.add_event("bench_start", algorithm=algorithm, log_dir=str(run_log_dir), nodes=nodes_text, sources=sources_text, interval=interval)
                common_kwargs = {
                    "port": port,
                    "baud": baud,
                    "nodes": parse_node_list(nodes_text),
                    "rounds": rounds,
                    "payload": payload,
                    "log_dir": run_log_dir,
                    "boot_wait": boot_wait,
                    "rssi_requests": rssi_requests,
                    "ack_timeout": ack_timeout,
                    "interval": interval,
                    "gateway": gateway,
                    "sources": parse_optional_node_list(sources_text),
                    "recollect_consecutive_failures": recollect_consecutive_failures,
                }
                if algorithm == "d3qn":
                    summary = run_d3qn_benchmark(
                        **common_kwargs,
                        checkpoint=checkpoint,
                        path_loss_degrade_threshold=path_loss_degrade_threshold,
                        path_p95_degrade_ms=path_p95_degrade_ms,
                        path_avg_degrade_ms=path_avg_degrade_ms,
                        path_health_window=path_health_window,
                    )
                else:
                    summary = run_benchmark(
                        **common_kwargs,
                        rssi_seconds=rssi_seconds,
                        route_mode=route_mode,
                        path_loss_degrade_threshold=path_loss_degrade_threshold,
                        path_avg_degrade_ms=path_avg_degrade_ms,
                        path_health_window=path_health_window,
                    )
                with self.lock:
                    actual_summary_dir = summary.get("log_dir") or summary.get("config", {}).get("log_dir") or str(run_log_dir)
                    self.bench_status.update(
                        {
                            "running": False,
                            "status": "complete",
                            "message": "benchmark complete",
                            "finished_at": datetime.now().isoformat(timespec="seconds"),
                            "log_dir": actual_summary_dir,
                            "summary": summary.get("total"),
                            "error": None,
                        }
                    )
                self.add_event("bench_complete", algorithm=algorithm, log_dir=str(run_log_dir), total=summary.get("total"))
            except Exception as exc:  # pragma: no cover - exercised through API status
                error_record = {
                    "ts": datetime.now().isoformat(timespec="milliseconds"),
                    "algorithm": algorithm,
                    "error": str(exc),
                    "traceback": traceback.format_exc(),
                    "log_dir": str(run_log_dir) if "run_log_dir" in locals() else str(actual_log_dir),
                }
                error_dir = Path(error_record["log_dir"])
                error_dir.mkdir(parents=True, exist_ok=True)
                (error_dir / "bench_error.json").write_text(
                    json.dumps(error_record, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
                    encoding="utf-8",
                )
                with self.lock:
                    self.bench_status.update(
                        {
                            "running": False,
                            "status": "error",
                            "message": str(exc),
                            "finished_at": datetime.now().isoformat(timespec="seconds"),
                            "error": str(exc),
                        }
                    )
                self.add_event("bench_error", error=str(exc))

        thread = threading.Thread(target=worker, name="pc-dijkstra-bench", daemon=True)
        self.bench_thread = thread
        thread.start()
        return self.get_bench_status()

    def get_bench_status(self) -> dict:
        with self.lock:
            status = dict(self.bench_status)
        log_dir = status.get("log_dir")
        if log_dir:
            events_path = _run_file_path(Path(log_dir), "events.jsonl")
            rounds_path = _run_file_path(Path(log_dir), "rounds.jsonl")
            status["event_count"] = _count_lines(events_path)
            status["round_count"] = _count_lines(rounds_path)
        return status

    def start_listen(self, port: str, baud: int) -> dict:
        with self.lock:
            if self.listen_status["running"]:
                raise RuntimeError("listener is already running")
            self.listen_stop.clear()
            self.listen_status = {"running": True, "port": port, "baud": baud, "error": None}

        def worker() -> None:
            client = None
            try:
                client = SerialClient(port, baud, raw_callback=self.raw_callback)
                with self.lock:
                    self.listen_client = client
                self.add_event("listen_start", port=port, baud=baud)
                while not self.listen_stop.is_set():
                    for message in client.read_available():
                        if isinstance(message, RssiReport):
                            updated = self.live_topology.update_from_rssi_report(message)
                            self.add_event(
                                "rssi_report",
                                src=format_addr(message.src_addr),
                                neighbors=[asdict(neighbor) for neighbor in message.neighbors],
                                edges=[asdict(edge) for edge in updated],
                            )
                        elif isinstance(message, Ack):
                            self.add_event("ack", src=format_addr(message.src_addr), seq=message.seq)
                    time.sleep(0.02)
            except Exception as exc:  # pragma: no cover - hardware failure path
                self.add_event("listen_error", error=str(exc))
                with self.lock:
                    self.listen_status["error"] = str(exc)
            finally:
                if client is not None:
                    client.close()
                with self.lock:
                    self.listen_client = None
                    self.listen_status["running"] = False
                self.add_event("listen_stop")

        thread = threading.Thread(target=worker, name="pc-dijkstra-listen", daemon=True)
        self.listen_thread = thread
        thread.start()
        return self.get_listen_status()

    def stop_listen(self) -> dict:
        self.listen_stop.set()
        thread = self.listen_thread
        if thread and thread.is_alive():
            thread.join(timeout=2.0)
        return self.get_listen_status()

    def get_listen_status(self) -> dict:
        with self.lock:
            return dict(self.listen_status)

    def recent_events(self, limit: int = 200) -> dict:
        with self.lock:
            events = list(self.events)[-limit:]
            topology = topology_to_dict(self.live_topology)
            listen_status = dict(self.listen_status)
            bench_status = dict(self.bench_status)
        log_events = _read_recent_jsonl(_run_file_path(Path(bench_status["log_dir"]), "events.jsonl"), limit) if bench_status.get("log_dir") else []
        if log_events:
            events = log_events[-limit:]
        return {"events": events, "topology": topology, "listen": listen_status}

    def send_payload(self, config: dict) -> dict:
        port = str(config.get("port") or "/dev/ttyUSB0")
        baud = int(config.get("baud") or 115200)
        src = parse_addr(str(config.get("src") or "00"))
        dst = parse_addr(str(config["dst"]))
        payload = str(config.get("payload") or "AABBCC")
        state = config.get("state")
        topology = load_topology(state) if state else self.live_topology
        route = topology.route(src, dst)
        if route.status != "valid":
            raise RuntimeError(f"unreachable: {format_addr(src)} -> {format_addr(dst)}")
        command = build_send_command(dst, route.path, payload)
        client = SerialClient(port, baud)
        try:
            client.write_command(command)
        finally:
            client.close()
        self.add_event("manual_send", command=command.rstrip(), route=route.path, cost=route.cost)
        return {"command": command.rstrip(), "route": route.path, "cost": route.cost}


def _safe_name(value: str) -> str:
    safe = "".join(ch if ch.isalnum() or ch in {"-", "_", "."} else "_" for ch in value)
    return safe.strip("._") or f"ui_{datetime.now().strftime('%Y%m%d_%H%M%S')}"


def _count_lines(path: Path) -> int:
    if not path.exists():
        return 0
    with path.open("r", encoding="utf-8", errors="ignore") as file_obj:
        return sum(1 for _ in file_obj)


def _read_json(path: Path) -> dict | list | None:
    if not path.exists():
        return None
    with path.open("r", encoding="utf-8") as file_obj:
        return json.load(file_obj)


def _run_file_path(run_dir: Path, filename: str) -> Path:
    candidates = [
        run_dir / filename,
        run_dir / "原始JSON数据" / filename,
    ]
    aliases = {
        "summary.json": ["summary.json"],
        "routes.json": ["routes.json"],
        "state.json": ["state.json"],
        "simulation_aligned_metrics.json": ["simulation_aligned_metrics.json"],
        "hardware_test_record.json": ["hardware_test_record.json"],
        "events.jsonl": ["events.jsonl"],
        "rounds.jsonl": ["rounds.jsonl"],
        "readable_report.md": ["测试结果汇报.md", "readable_report.md"],
        "metrics.xlsx": ["测试指标汇总.xlsx", "metrics.xlsx"],
        "topology.svg": ["拓扑图.svg", "topology.svg"],
        "topology.txt": ["拓扑图.txt", "topology.txt"],
        "raw_serial.log": ["原始串口日志.log", "raw_serial.log"],
    }
    for alias in aliases.get(filename, [filename]):
        candidates.append(run_dir / alias)
        candidates.append(run_dir / "原始JSON数据" / alias)
    for candidate in candidates:
        if candidate.exists():
            return candidate
    return candidates[0]


def _read_recent_jsonl(path: Path, limit: int) -> list[dict]:
    if not path.exists():
        return []
    lines = path.read_text(encoding="utf-8", errors="ignore").splitlines()[-limit:]
    events = []
    for line in lines:
        try:
            events.append(json.loads(line))
        except json.JSONDecodeError:
            continue
    return events


def _run_dirs(log_root: Path) -> list[Path]:
    roots = [log_root / "dijkstra_hw", log_root / "dijkstra_hw_sweep", log_root / "d3qn_hw"]
    runs: list[Path] = []
    for root in roots:
        if not root.exists():
            continue
        for marker_path in list(root.rglob("summary.json")) + list(root.rglob("bench_error.json")):
            run_dir = marker_path.parent.parent if marker_path.parent.name == "原始JSON数据" else marker_path.parent
            if run_dir not in runs:
                runs.append(run_dir)
    return sorted(runs, key=lambda item: item.stat().st_mtime, reverse=True)


def list_runs(log_root: Path) -> list[dict]:
    runs = []
    for run_dir in _run_dirs(log_root):
        summary = _read_json(_run_file_path(run_dir, "summary.json")) or {}
        error = _read_json(run_dir / "bench_error.json") or {}
        config = summary.get("config", {})
        rel = run_dir.relative_to(log_root).as_posix()
        runs.append(
            {
                "id": rel,
                "name": run_dir.name,
                "path": str(run_dir),
                "mtime": datetime.fromtimestamp(run_dir.stat().st_mtime).isoformat(timespec="seconds"),
                "status": "error" if error else "complete",
                "error": error.get("error"),
                "nodes": [format_addr(node) for node in config.get("nodes", [])],
                "algorithm": error.get("algorithm") or config.get("algorithm") or summary.get("algorithm") or ("D3QN_MPNN" if rel.startswith("d3qn_hw/") else "dijkstra"),
                "interval": config.get("interval"),
                "sent": summary.get("total", {}).get("sent"),
                "success": summary.get("total", {}).get("success"),
                "loss_rate": summary.get("total", {}).get("loss_rate"),
                "avg_ms": summary.get("total", {}).get("latency", {}).get("avg_ms"),
                "p95_ms": summary.get("total", {}).get("latency", {}).get("p95_ms"),
            }
        )
    return runs


def _resolve_run(log_root: Path, run_id: str) -> Path:
    parts = [part for part in unquote(run_id).split("/") if part]
    if any(part in {"..", "."} for part in parts):
        raise FileNotFoundError(run_id)
    run_dir = log_root.joinpath(*parts)
    if not run_dir.exists() or (not _run_file_path(run_dir, "summary.json").exists() and not (run_dir / "bench_error.json").exists()):
        raise FileNotFoundError(run_id)
    return run_dir


def load_run(log_root: Path, run_id: str) -> dict:
    run_dir = _resolve_run(log_root, run_id)
    state_path = _run_file_path(run_dir, "state.json")
    topology = load_topology(state_path) if state_path.exists() else Topology(stale_seconds=None, edge_direction="src_to_neighbor")
    error = _read_json(run_dir / "bench_error.json")
    return {
        "id": run_dir.relative_to(log_root).as_posix(),
        "error": error,
        "summary": _read_json(_run_file_path(run_dir, "summary.json")),
        "routes": _read_json(_run_file_path(run_dir, "routes.json")),
        "topology": topology_to_dict(topology),
        "aligned": _read_json(_run_file_path(run_dir, "simulation_aligned_metrics.json")),
        "hardware": _read_json(_run_file_path(run_dir, "hardware_test_record.json")),
        "readable_report": _run_file_path(run_dir, "readable_report.md").read_text(encoding="utf-8") if _run_file_path(run_dir, "readable_report.md").exists() else "",
    }


def run_topology_svg(log_root: Path, run_id: str) -> str:
    run_dir = _resolve_run(log_root, run_id)
    svg_path = _run_file_path(run_dir, "topology.svg")
    if svg_path.exists():
        return svg_path.read_text(encoding="utf-8")
    topology = load_topology(_run_file_path(run_dir, "state.json"))
    summary = _read_json(_run_file_path(run_dir, "summary.json"))
    svg = build_topology_svg(topology, summary)
    (run_dir / "拓扑图.svg").write_text(svg, encoding="utf-8")
    return svg


def resolve_run_file(log_root: Path, run_id: str, filename: str) -> Path:
    run_dir = _resolve_run(log_root, run_id)
    safe_name = Path(unquote(filename)).name
    file_path = _run_file_path(run_dir, safe_name)
    if not file_path.exists() or not file_path.is_file():
        raise FileNotFoundError(filename)
    return file_path


class UiRequestHandler(BaseHTTPRequestHandler):
    state: UiState

    def do_GET(self) -> None:
        parsed = urlparse(self.path)
        path = parsed.path
        try:
            if path == "/":
                self._send_text(INDEX_HTML, "text/html; charset=utf-8")
            elif path == "/api/runs":
                self._send_json({"runs": list_runs(self.state.log_root)})
            elif path.startswith("/api/runs/") and path.endswith("/topology.svg"):
                run_id = path[len("/api/runs/") : -len("/topology.svg")]
                self._send_text(run_topology_svg(self.state.log_root, run_id), "image/svg+xml")
            elif path.startswith("/api/runs/") and "/file/" in path:
                run_id, filename = path[len("/api/runs/") :].split("/file/", 1)
                self._send_file(resolve_run_file(self.state.log_root, run_id, filename))
            elif path.startswith("/api/runs/"):
                run_id = path[len("/api/runs/") :]
                self._send_json(load_run(self.state.log_root, run_id))
            elif path == "/api/bench/status":
                self._send_json(self.state.get_bench_status())
            elif path == "/api/events":
                self._send_json(self.state.recent_events())
            else:
                self.send_error(HTTPStatus.NOT_FOUND, "not found")
        except FileNotFoundError:
            self.send_error(HTTPStatus.NOT_FOUND, "run not found")
        except Exception as exc:
            self._send_json({"error": str(exc)}, status=500)

    def do_POST(self) -> None:
        parsed = urlparse(self.path)
        try:
            payload = self._read_json_body()
            if parsed.path == "/api/bench/start":
                self._send_json(self.state.start_bench(payload))
            elif parsed.path == "/api/listen/start":
                self._send_json(self.state.start_listen(str(payload.get("port") or "/dev/ttyUSB0"), int(payload.get("baud") or 115200)))
            elif parsed.path == "/api/listen/stop":
                self._send_json(self.state.stop_listen())
            elif parsed.path == "/api/send":
                self._send_json(self.state.send_payload(payload))
            else:
                self.send_error(HTTPStatus.NOT_FOUND, "not found")
        except Exception as exc:
            self._send_json({"error": str(exc)}, status=400)

    def log_message(self, fmt: str, *args) -> None:
        return

    def _read_json_body(self) -> dict:
        length = int(self.headers.get("Content-Length") or "0")
        if length == 0:
            return {}
        return json.loads(self.rfile.read(length).decode("utf-8"))

    def _send_json(self, data, status: int = 200) -> None:
        body = json.dumps(data, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _send_text(self, text: str, content_type: str | None = None, status: int = 200) -> None:
        body = text.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", content_type or mimetypes.guess_type(self.path)[0] or "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _send_file(self, path: Path) -> None:
        body = path.read_bytes()
        content_type = mimetypes.guess_type(path.name)[0] or "application/octet-stream"
        self.send_response(200)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Content-Disposition", "inline")
        self.end_headers()
        self.wfile.write(body)


def create_server(host: str = "127.0.0.1", port: int = 8080, log_root: str | Path = DEFAULT_LOG_ROOT) -> ThreadingHTTPServer:
    state = UiState(log_root)

    class Handler(UiRequestHandler):
        pass

    Handler.state = state
    server = ThreadingHTTPServer((host, port), Handler)
    server.state = state  # type: ignore[attr-defined]
    return server


def serve_ui(host: str = "127.0.0.1", port: int = 8080, log_root: str | Path = DEFAULT_LOG_ROOT) -> None:
    server = create_server(host, port, log_root)
    print(f"UI server listening on http://{host}:{server.server_port}")
    try:
        server.serve_forever()
    finally:
        server.server_close()


INDEX_HTML = r"""<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Dijkstra 上位机</title>
  <style>
    :root { color-scheme: light; --bg:#f5f7fb; --panel:#fff; --ink:#172033; --muted:#64748b; --line:#d8dee9; --accent:#2563eb; --ok:#15803d; --warn:#b45309; --bad:#dc2626; }
    * { box-sizing: border-box; }
    body { margin: 0; background: var(--bg); color: var(--ink); font: 14px/1.45 system-ui, -apple-system, Segoe UI, sans-serif; }
    header { height: 56px; display:flex; align-items:center; justify-content:space-between; padding:0 24px; background:#111827; color:#fff; }
    header h1 { font-size:18px; margin:0; font-weight:700; }
    main { display:grid; grid-template-columns: 360px 1fr; min-height: calc(100vh - 56px); }
    aside { border-right:1px solid var(--line); background:#fff; padding:16px; overflow:auto; }
    section { padding:18px 22px; }
    h2 { font-size:15px; margin:18px 0 10px; }
    label { display:block; color:var(--muted); font-size:12px; margin:10px 0 4px; }
    input, select, button { width:100%; height:34px; border:1px solid var(--line); border-radius:6px; padding:0 10px; background:#fff; color:var(--ink); }
    button { background:var(--accent); color:#fff; border-color:var(--accent); font-weight:650; cursor:pointer; }
    button.secondary { background:#fff; color:var(--ink); border-color:var(--line); }
    button.danger { background:#b91c1c; border-color:#b91c1c; }
    .row { display:grid; grid-template-columns:1fr 1fr; gap:10px; }
    .toolbar { display:flex; gap:10px; align-items:center; margin:0 0 14px; }
    .toolbar button { width:auto; padding:0 14px; }
    .panel { background:var(--panel); border:1px solid var(--line); border-radius:8px; padding:14px; margin-bottom:14px; }
    .runs { display:flex; flex-direction:column; gap:8px; }
    .run { border:1px solid var(--line); border-radius:7px; padding:10px; cursor:pointer; background:#fff; }
    .run:hover { border-color:#93c5fd; }
    .run.active { border-color:var(--accent); box-shadow:0 0 0 2px #dbeafe; }
    .run-title { font-weight:700; margin-bottom:4px; }
    .muted { color:var(--muted); }
    .metrics { display:grid; grid-template-columns:repeat(4, minmax(120px,1fr)); gap:12px; margin-bottom:14px; }
    .metric { background:#fff; border:1px solid var(--line); border-radius:8px; padding:12px; }
    .metric b { display:block; font-size:20px; margin-top:4px; }
    .report { background:#fff; border:1px solid var(--line); border-radius:8px; padding:14px; margin-bottom:14px; }
    .report h2, .report h3 { margin:10px 0 8px; }
    .report h2 { font-size:16px; }
    .report h3 { font-size:14px; color:#334155; }
    .report p { margin:6px 0; color:#334155; }
    .report .file-links { display:flex; flex-wrap:wrap; gap:8px; margin-top:10px; }
    .report .file-links a { color:var(--accent); text-decoration:none; border:1px solid #bfdbfe; border-radius:6px; padding:5px 8px; background:#eff6ff; }
    table { width:100%; border-collapse:collapse; background:#fff; border:1px solid var(--line); border-radius:8px; overflow:hidden; }
    th, td { padding:9px 10px; border-bottom:1px solid var(--line); text-align:left; white-space:nowrap; }
    th { background:#f8fafc; color:#475569; font-size:12px; }
    .topology { width:100%; min-height:420px; border:1px solid var(--line); border-radius:8px; background:#fff; }
    .topology img { display:block; width:100%; height:auto; }
    pre { margin:0; max-height:260px; overflow:auto; background:#0f172a; color:#dbeafe; padding:12px; border-radius:8px; font-size:12px; }
    .status { color:var(--muted); margin:8px 0 0; min-height:20px; }
    @media (max-width: 980px) { main { grid-template-columns:1fr; } aside { border-right:0; border-bottom:1px solid var(--line); } .metrics { grid-template-columns:1fr 1fr; } }
  </style>
</head>
<body>
  <header><h1>广播组网上位机</h1><div id="serverState">Dijkstra / D3QN Web UI</div></header>
  <main>
    <aside>
      <div class="panel">
        <h2>启动测试</h2>
        <label>算法</label><select id="benchAlgorithm" onchange="syncAlgorithmFields()"><option value="dijkstra">Dijkstra</option><option value="d3qn">D3QN_MPNN</option></select>
        <label>串口</label><input id="benchPort" value="/dev/ttyUSB0">
        <div class="row"><div><label>波特率</label><input id="benchBaud" value="115200"></div><div><label>发包间隔 s</label><input id="benchInterval" value="0.5"></div></div>
        <label>节点列表（十六进制）</label><input id="benchNodes" value="1,2,3,4,5,6,7,8,9,10">
        <label>源节点限制（留空表示全部）</label><input id="benchSources" placeholder="例如 1 或 1,2,3">
        <div class="row"><div><label>每节点轮数</label><input id="benchRounds" value="20"></div><div><label>ACK timeout</label><input id="benchAck" value="2.0"></div></div>
        <div class="row"><div><label>RSSI_REQ 次数</label><input id="benchRssiRequests" value="5"></div><div><label>路由模式</label><select id="benchRouteMode"><option value="baseline_dijkstra">baseline_dijkstra</option><option value="reliable_dijkstra_v1">reliable_dijkstra_v1</option></select></div></div>
        <div id="d3qnFields">
          <label>D3QN checkpoint</label><input id="benchCheckpoint" placeholder="留空使用默认 checkpoints/d3qn_mpnn/latest.pt">
          <div class="row"><div><label>路径丢包降级阈值</label><input id="benchPathLoss" value="0.10"></div><div><label>路径 P95 降级 ms</label><input id="benchPathP95" value="700"></div></div>
          <label>路径平均延时降级 ms</label><input id="benchPathAvg" value="220">
        </div>
        <label>连续失败重采阈值</label><input id="benchRecollect" value="3">
        <label>Payload</label><input id="benchPayload" value="AABBCC">
        <label>日志目录名</label><input id="benchLog" placeholder="留空自动生成">
        <button onclick="startBench()">开始 Benchmark</button>
        <div class="status" id="benchStatus"></div>
      </div>
      <div class="panel">
        <h2>手动发送</h2>
        <div class="row"><div><label>目标</label><input id="sendDst" value="01"></div><div><label>Payload</label><input id="sendPayload" value="AABBCC"></div></div>
        <button class="secondary" onclick="manualSend()">计算路径并发送</button>
        <div class="status" id="sendStatus"></div>
      </div>
      <h2>历史测试</h2>
      <div class="runs" id="runs"></div>
    </aside>
    <section>
      <div class="toolbar"><button class="secondary" onclick="loadRuns()">刷新</button><span class="muted" id="selectedRun">未选择测试记录</span></div>
      <div class="metrics" id="metrics"></div>
      <div class="report" id="reportPanel"></div>
      <div class="topology" id="topology"></div>
      <h2>目标结果</h2>
      <div id="targets"></div>
      <h2>仿真指标对齐</h2>
      <div id="aligned"></div>
      <h2>测试监听事件流</h2>
      <pre id="events"></pre>
    </section>
  </main>
<script>
const $ = (id) => document.getElementById(id);
let currentRun = null;

function pct(v){ return v == null ? 'n/a' : (v*100).toFixed(2)+'%'; }
function ms(v){ return v == null ? 'n/a' : v.toFixed(1)+'ms'; }
function pathText(route){ return (route || []).map(n => Number(n).toString(16).toUpperCase().padStart(2,'0')).join(' -> '); }
function escapeHtml(v){ return String(v ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c])); }

async function api(path, options){ const r = await fetch(path, options); const t = await r.text(); const d = t ? JSON.parse(t) : {}; if(!r.ok) throw new Error(d.error || r.statusText); return d; }

async function loadRuns(){
  const data = await api('/api/runs');
  $('runs').innerHTML = data.runs.map(run => `<div class="run ${run.id===currentRun?'active':''}" data-run-id="${escapeHtml(run.id)}">
    <div class="run-title">${escapeHtml(run.name)}</div>
    <div class="muted">${escapeHtml(run.algorithm || '')} · ${escapeHtml(run.status || 'complete')} · sent ${run.sent ?? '-'} · loss ${pct(run.loss_rate)} · avg ${ms(run.avg_ms)}</div>
    ${run.error ? `<div class="muted">${escapeHtml(run.error)}</div>` : ''}
  </div>`).join('');
  document.querySelectorAll('.run[data-run-id]').forEach(item => {
    item.addEventListener('click', () => loadRun(item.dataset.runId));
  });
}

async function loadRun(id){
  currentRun = id; await loadRuns();
  const data = await api('/api/runs/' + encodeURIComponent(id).replaceAll('%2F','/'));
  if(!data.summary || !data.summary.total){
    $('selectedRun').textContent = id + (data.error ? ' · error' : ' · summary missing');
    $('metrics').innerHTML = [
      metricCard('状态', data.error ? 'error' : 'missing'),
      metricCard('发送', 'n/a'),
      metricCard('丢包率', 'n/a'),
      metricCard('轮次', '0')
    ].join('');
    $('reportPanel').innerHTML = `<h2>测试未生成正式结果</h2><p>${escapeHtml(data.error?.error || 'summary.json missing')}</p><p>日志目录：${escapeHtml(id)}</p>`;
    $('topology').innerHTML = '';
    $('targets').innerHTML = '';
    $('aligned').innerHTML = '';
    return;
  }
  $('selectedRun').textContent = id;
  const total = data.summary.total;
  $('metrics').innerHTML = [
    ['发送', total.sent], ['成功', total.success], ['丢包率', pct(total.loss_rate)], ['平均延时', ms(total.latency.avg_ms)]
  ].map(x => `<div class="metric"><span class="muted">${x[0]}</span><b>${x[1]}</b></div>`).join('');
  $('topology').innerHTML = `<img src="/api/runs/${encodeURIComponent(id).replaceAll('%2F','/')}/topology.svg?ts=${Date.now()}" alt="topology">`;
  $('reportPanel').innerHTML = renderReport(data, id);
  $('targets').innerHTML = renderTargets(data.summary.pairs || data.summary.targets || {});
  $('aligned').innerHTML = renderAligned(data.aligned?.metrics || {});
}

function renderReport(data, id){
  const s = data.summary || {};
  const total = s.total || {};
  const cfg = s.config || {};
  const metrics = s.metrics || {};
  const rssi = metrics.rssi_fluctuation || {};
  const jitter = metrics.latency_jitter || {};
  const targets = s.pairs || s.targets || {};
  const fileBase = '/api/runs/' + encodeURIComponent(id).replaceAll('%2F','/') + '/file/';
  return `<h2>测试结果报告</h2>
    <p><b>日志：</b>${escapeHtml(id)} · <b>算法：</b>${escapeHtml(cfg.algorithm || (id.startsWith('d3qn_hw/') ? 'D3QN_MPNN' : 'dijkstra'))} · <b>节点：</b>${escapeHtml((cfg.nodes || []).map(n => Number(n).toString(16).toUpperCase().padStart(2,'0')).join(', '))} · <b>发包间隔：</b>${cfg.interval ?? 'n/a'}s · <b>每源-目标轮数：</b>${cfg.rounds ?? 'n/a'}</p>
    <div class="metrics">
      ${metricCard('总发送', total.sent ?? 'n/a')}
      ${metricCard('成功', total.success ?? 'n/a')}
      ${metricCard('全局丢包率', pct(total.loss_rate))}
      ${metricCard('点到点平均延时', ms(total.latency?.avg_ms))}
    </div>
    <h3>测试结果</h3>
    ${renderTargets(targets)}
    <h3>指标总结对比</h3>
    <table><thead><tr><th>指标</th><th>值</th><th>说明</th></tr></thead><tbody>
      <tr><td>算法/推理计算延时</td><td>${ms(metrics.algorithm_compute_latency_ms)}</td><td>Dijkstra 为路由计算耗时，D3QN 为平均推理耗时</td></tr>
      <tr><td>点到点平均延时</td><td>${ms(metrics.command_downlink_latency_ms)}</td><td>第二次 SEND ACK 时延减第一次 gateway->source ACK 时延</td></tr>
      <tr><td>D3QN 总耗时</td><td>${ms(total.d3qn_total_latency?.avg_ms)}</td><td>D3QN 为 inference_ms + point_to_point_ms；Dijkstra 无此项</td></tr>
      <tr><td>全局平均丢包率</td><td>${pct(metrics.global_avg_loss_rate)}</td><td>总 timeout / 总发送</td></tr>
      <tr><td>平均跳数</td><td>${num(metrics.avg_route_hops)}</td><td>各目标最终路径跳数平均</td></tr>
      <tr><td>平均单跳传输耗时</td><td>${ms(metrics.avg_single_hop_latency_ms)}</td><td>端到端平均延时 / 跳数</td></tr>
      <tr><td>RSSI 波动</td><td>${num(rssi.range)} dB</td><td>min ${num(rssi.min)} dBm / max ${num(rssi.max)} dBm / std ${num(rssi.stddev)}</td></tr>
      <tr><td>时延抖动</td><td>${ms(jitter.jitter_ms)}</td><td>相邻成功 ACK 延时差值均值，std ${ms(jitter.stddev_ms)}</td></tr>
    </tbody></table>
    <h3>完整指标汇总</h3>
    ${renderFullMetrics(targets, metrics)}
    <h3>路径对比</h3>
    ${renderPathCompare(targets)}
    <div class="file-links">
      <a href="${fileBase}${encodeURIComponent('测试结果汇报.md')}" target="_blank">测试结果汇报.md</a>
      <a href="${fileBase}${encodeURIComponent('测试指标汇总.xlsx')}" target="_blank">测试指标汇总.xlsx</a>
      <a href="${fileBase}${encodeURIComponent('拓扑图.txt')}" target="_blank">拓扑图.txt</a>
      <a href="${fileBase}${encodeURIComponent('原始串口日志.log')}" target="_blank">原始串口日志.log</a>
    </div>`;
}

function metricCard(label, value){ return `<div class="metric"><span class="muted">${label}</span><b>${value}</b></div>`; }
function num(v){ return v == null ? 'n/a' : Number(v).toFixed(3).replace(/\.?0+$/,''); }

function renderTargets(targets){
  const rows = Object.entries(targets).map(([k,v]) => `<tr><td>${v.source ?? '00'}</td><td>${v.destination ?? k}</td><td>${pathText(v.last_route)}</td><td>${v.success}/${v.sent}</td><td>${v.planned_rounds ?? 'n/a'}</td><td>${v.ack_timeout_loss ?? 0}</td><td>${v.route_failed ?? v.d3qn_route_failures ?? 0}</td><td>${pct(v.loss_rate)}</td><td>${ms(v.latency?.avg_ms)}</td><td>${ms(v.latency?.p95_ms)}</td><td>${ms(v.inference_latency?.avg_ms)}</td><td>${ms(v.d3qn_total_latency?.avg_ms)}</td><td>${v.recollect_count ?? 0}</td><td>${v.path_switch_count ?? 0}</td><td>${v.path_rssi?.min_rssi ?? 'n/a'}</td></tr>`).join('');
  return `<table><thead><tr><th>出发点</th><th>目标点</th><th>路径</th><th>成功/实际SEND</th><th>计划轮次</th><th>ACK timeout</th><th>路由失败</th><th>丢包率</th><th>点到点平均</th><th>P95</th><th>推理平均</th><th>D3QN总耗时</th><th>重采</th><th>切换</th><th>最弱 RSSI</th></tr></thead><tbody>${rows}</tbody></table>`;
}

function renderFullMetrics(targets, metrics){
  const rssi = metrics.rssi_fluctuation || {};
  const jitter = metrics.latency_jitter || {};
  const rows = Object.entries(targets).map(([k,v]) => `<tr>
    <td>${v.source ?? '00'}</td>
    <td>${v.destination ?? k}</td>
    <td>${pathText(v.last_route)}</td>
    <td>${ms(v.algorithm_compute_latency_ms ?? metrics.algorithm_compute_latency_ms)}</td>
    <td>${ms(v.command_downlink_latency_ms)}</td>
    <td>${ms(v.end_to_end_avg_latency_ms)}</td>
    <td>${pct(v.loss_rate)}</td>
    <td>${pct(metrics.global_avg_loss_rate)}</td>
    <td>${v.route_hops ?? 'n/a'}</td>
    <td>${num(metrics.avg_route_hops)}</td>
    <td>${ms(v.avg_single_hop_latency_ms)}</td>
    <td>min ${num(rssi.min)} / max ${num(rssi.max)} / range ${num(rssi.range)}dB / std ${num(rssi.stddev)}</td>
    <td>节点 ${ms(v.latency_jitter?.jitter_ms)} / 全局 ${ms(jitter.jitter_ms)}</td>
  </tr>`).join('');
  return `<table><thead><tr><th>出发点</th><th>目标点</th><th>路径</th><th>算法计算延时</th><th>指令下发延时</th><th>端到端平均延时</th><th>节点丢包率</th><th>全局平均丢包率</th><th>路由跳数</th><th>单路径平均跳数</th><th>平均单跳传输耗时</th><th>RSSI 实时波动</th><th>时延抖动变化</th></tr></thead><tbody>${rows}</tbody></table>`;
}

function renderPathCompare(targets){
  const rows = Object.entries(targets).map(([k,v]) => `<tr><td>${v.destination ?? k}</td><td>${v.route_hops ?? 'n/a'}</td><td>${pct(v.loss_rate)}</td><td>${ms(v.avg_single_hop_latency_ms)}</td><td>${ms(v.latency_jitter?.jitter_ms)}</td><td>${num(v.path_rssi?.mean_rssi)}</td><td>${v.path_rssi?.min_rssi ?? 'n/a'}</td></tr>`).join('');
  return `<table><thead><tr><th>目标点</th><th>路由跳数</th><th>单路径丢包率</th><th>平均单跳传输耗时</th><th>时延抖动</th><th>RSSI均值</th><th>最弱RSSI</th></tr></thead><tbody>${rows}</tbody></table>`;
}

function renderAligned(metrics){
  const keys = ['packet_loss_rate','total_delay','route_length_hops','mean_delay_per_hop','train_score','link_utilization','queueing_delay'];
  const rows = keys.map(k => {
    const m = metrics[k] || {value:'n/a', unit:'', source:'missing'};
    let val = m.unit === 'ratio' ? pct(m.value) : (m.unit || '').includes('seconds') && m.value !== 'n/a' ? Number(m.value).toFixed(4)+'s' : m.value;
    return `<tr><td>${k}</td><td>${val}</td><td>${m.unit}</td><td>${m.source}</td></tr>`;
  }).join('');
  return `<table><thead><tr><th>字段</th><th>值</th><th>单位</th><th>来源</th></tr></thead><tbody>${rows}</tbody></table>`;
}

async function startBench(){
  const payload = {
    algorithm:$('benchAlgorithm').value,
    port:$('benchPort').value, baud:+$('benchBaud').value, nodes:$('benchNodes').value, sources:$('benchSources').value, rounds:+$('benchRounds').value,
    payload:$('benchPayload').value, interval:+$('benchInterval').value,
    rssi_requests:+$('benchRssiRequests').value, ack_timeout:+$('benchAck').value, route_mode:$('benchRouteMode').value, log_name:$('benchLog').value,
    recollect_consecutive_failures:+$('benchRecollect').value,
    checkpoint:$('benchCheckpoint').value,
    path_loss_degrade_threshold:+$('benchPathLoss').value,
    path_p95_degrade_ms:+$('benchPathP95').value,
    path_avg_degrade_ms:+$('benchPathAvg').value
  };
  try {
    const s = await api('/api/bench/start', {method:'POST', body:JSON.stringify(payload)});
    $('benchStatus').textContent = s.message + ' · ' + s.log_dir;
    $('selectedRun').textContent = '正在测试：' + s.log_dir;
    $('reportPanel').innerHTML = `<h2>测试运行中</h2><p>日志目录：${escapeHtml(s.log_dir)}</p><p>事件流会显示本次 benchmark 监听到的 RSSI、路由、SEND、ACK 和 timeout。</p>`;
    $('metrics').innerHTML = [metricCard('状态', s.status), metricCard('事件', s.event_count ?? 0), metricCard('轮次', s.round_count ?? 0), metricCard('日志', '生成中')].join('');
    $('topology').innerHTML = '';
    $('targets').innerHTML = '';
    $('aligned').innerHTML = '';
  } catch(e){ $('benchStatus').textContent = e.message; }
}

async function pollBench(){
  try {
    const s = await api('/api/bench/status');
    const detail = s.status === 'error' ? ` · ${s.message || s.error || 'unknown error'}` : '';
    $('benchStatus').textContent = `${s.status} · events ${s.event_count ?? 0} · rounds ${s.round_count ?? 0}${detail}`;
    if(s.status === 'error') {
      $('reportPanel').innerHTML = `<h2>测试启动失败</h2><p>${escapeHtml(s.message || s.error || 'unknown error')}</p><p>日志目录：${escapeHtml(s.log_dir || '')}</p>`;
    }
    if(s.status==='complete') { await loadRuns(); const runId = runIdFromPath(s.log_dir); if(runId) loadRun(runId); }
  } catch(e){}
}
function runIdFromPath(path){ const marker = '/logs/'; if(!path || !path.includes(marker)) return null; return path.split(marker).pop(); }
function syncAlgorithmFields(){
  const isD3qn = $('benchAlgorithm').value === 'd3qn';
  $('d3qnFields').style.display = isD3qn ? 'block' : 'none';
  $('benchRouteMode').disabled = isD3qn;
}
async function startListen(){ try { const s = await api('/api/listen/start', {method:'POST', body:JSON.stringify({port:$('benchPort').value, baud:+$('benchBaud').value})}); $('listenStatus').textContent = s.running ? 'listening' : 'stopped'; } catch(e){ $('listenStatus').textContent = e.message; } }
async function stopListen(){ const s = await api('/api/listen/stop', {method:'POST', body:'{}'}); $('listenStatus').textContent = s.running ? 'listening' : 'stopped'; }
async function manualSend(){ try { const r = await api('/api/send', {method:'POST', body:JSON.stringify({port:$('benchPort').value, baud:+$('benchBaud').value, dst:$('sendDst').value, payload:$('sendPayload').value})}); $('sendStatus').textContent = r.command; } catch(e){ $('sendStatus').textContent = e.message; } }
async function pollEvents(){ try { const d = await api('/api/events'); $('events').textContent = d.events.slice(-60).map(e => `${e.ts} ${e.type} ${JSON.stringify(e)}`).join('\\n'); } catch(e){} }

syncAlgorithmFields(); loadRuns(); setInterval(pollBench, 2000); setInterval(pollEvents, 1000);
</script>
</body>
</html>"""
