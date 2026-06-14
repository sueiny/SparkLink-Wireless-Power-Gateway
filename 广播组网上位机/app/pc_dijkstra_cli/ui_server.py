from __future__ import annotations

import json
import mimetypes
import os
import threading
import time
import traceback
from collections import deque
from dataclasses import asdict
from datetime import datetime
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, unquote, urlparse

from .benchmark import (
    SAMPLE_ROUTE_MODE,
    parse_node_list,
    parse_optional_node_list,
    resolve_benchmark_log_dir,
    run_benchmark,
)
from .daemon import get_socket_path
from .protocol import Ack, RssiReport, build_send_command, format_addr, parse_addr
from .routing import ROUTE_MODES
from .serial_client import RawSerialEvent, SerialClient
from .topology import Topology, load_topology, routes_to_dict, save_topology
from .topology_svg import build_topology_svg, topology_to_dict, write_topology_svg
from pc_d3qn_cli.benchmark import resolve_log_dir as resolve_d3qn_log_dir
from pc_d3qn_cli.benchmark import run_benchmark as run_d3qn_benchmark
from pc_d3qn_cli.model import LATEST_CHECKPOINT


APP_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_LOG_ROOT = APP_ROOT / "logs"


# ---------------------------------------------------------------------------
# State
# ---------------------------------------------------------------------------

class UiState:
    def __init__(self, log_root: str | Path = DEFAULT_LOG_ROOT):
        self.log_root = Path(log_root)
        self.lock = threading.Lock()
        self.events: deque[dict] = deque(maxlen=500)
        self.live_topology = Topology(stale_seconds=None, edge_direction="src_to_neighbor")
        self.bench_thread: threading.Thread | None = None
        self.bench_status: dict = {"running": False, "status": "idle", "message": "", "log_dir": None, "started_at": None, "finished_at": None, "summary": None, "error": None}
        self.listen_thread: threading.Thread | None = None
        self.listen_stop = threading.Event()
        self.listen_client: SerialClient | None = None
        self.listen_status: dict = {"running": False, "port": None, "baud": None, "error": None}

    def add_event(self, event_type: str, **payload) -> None:
        with self.lock:
            self.events.append({"ts": datetime.now().isoformat(timespec="milliseconds"), "type": event_type, **payload})

    def raw_callback(self, event: RawSerialEvent) -> None:
        text = "".join(chr(b) if b in (9, 10, 13) or 32 <= b <= 126 else "." for b in event.data)
        self.add_event("raw_serial", direction=event.direction, length=len(event.data), hex=event.data.hex(" "), text=text.strip())

    # ---- bench ----

    def start_bench(self, config: dict) -> dict:
        with self.lock:
            if self.bench_status["running"]:
                raise RuntimeError("测试正在运行中")

        port = str(config.get("port") or "/dev/ttyUSB0")
        algorithm = str(config.get("algorithm") or "dijkstra").strip().lower()
        if algorithm not in {"dijkstra", "d3qn"}:
            raise RuntimeError(f"不支持的算法: {algorithm}")

        baud        = int(config.get("baud") or 115200)
        nodes       = parse_node_list(str(config.get("nodes") or "1,2,3,4,5,6,7,8,9,10"))
        sources     = parse_optional_node_list(str(config.get("sources") or "").strip())
        rounds      = int(config.get("rounds") or 2)
        payload     = str(config.get("payload") or "AABBCC")
        interval    = float(config.get("interval") or 0)
        rssi_req    = int(config.get("rssi_requests") or 2)
        ack_timeout = float(config.get("ack_timeout") or 5.0)
        gateway     = parse_addr(str(config.get("gateway") or "00"))
        dongle_addr = int(config.get("dongle_addr") or 16)
        no_retry       = bool(config.get("no_retry") or True)
        route_mode     = str(config.get("route_mode") or SAMPLE_ROUTE_MODE)
        hop_penalty    = float(config.get("hop_penalty") or 0.0)
        enable_congestion = bool(config.get("enable_congestion") or False)
        dynamic_pause  = bool(config.get("dynamic_pause") or False)
        log_name       = str(config.get("log_name") or "").strip()
        checkpoint     = str(config.get("checkpoint") or LATEST_CHECKPOINT)
        online_learn   = bool(config.get("online_learn") or False)
        online_interval = int(config.get("online_interval") or 100)
        online_lr      = float(config.get("online_lr") or 3e-4)
        online_epochs  = int(config.get("online_epochs") or 1)
        online_nudge   = float(config.get("online_nudge") or 0.8)

        log_root = self.log_root / ("d3qn_hw" if algorithm == "d3qn" else "dijkstra_hw")
        log_dir = log_root / _safe_name(log_name) if log_name else log_root
        actual_log_dir = resolve_d3qn_log_dir(log_dir) if algorithm == "d3qn" else resolve_benchmark_log_dir(log_dir)

        with self.lock:
            self.bench_status = {"running": True, "status": "running", "message": "测试运行中", "log_dir": str(actual_log_dir), "started_at": datetime.now().isoformat(timespec="seconds"), "finished_at": None, "summary": None, "error": None}

        common = dict(port=port, baud=baud, nodes=nodes, rounds=rounds, payload=payload, log_dir=actual_log_dir, rssi_requests=rssi_req, ack_timeout=ack_timeout, interval=interval, gateway=gateway, dongle_addr=dongle_addr, sources=sources)

        def worker() -> None:
            try:
                run_log_dir = resolve_d3qn_log_dir(log_dir) if algorithm == "d3qn" else resolve_benchmark_log_dir(log_dir)
                with self.lock:
                    self.bench_status["log_dir"] = str(run_log_dir)
                self.add_event("bench_start", algorithm=algorithm, log_dir=str(run_log_dir))
                kw = dict(common, log_dir=run_log_dir)
                if algorithm == "d3qn":
                    summary = run_d3qn_benchmark(**kw, checkpoint=checkpoint, enable_online_learn=online_learn, online_interval=online_interval, online_lr=online_lr, online_epochs=online_epochs, online_nudge=online_nudge)
                else:
                    summary = run_benchmark(**kw, rssi_seconds=0.0, route_mode=route_mode, no_retry=no_retry, hop_penalty=hop_penalty, enable_congestion=enable_congestion, dynamic_pause=dynamic_pause)
                with self.lock:
                    log_path = summary.get("log_dir") or summary.get("config", {}).get("log_dir") or str(run_log_dir)
                    self.bench_status.update({"running": False, "status": "complete", "message": "测试完成", "finished_at": datetime.now().isoformat(timespec="seconds"), "log_dir": log_path, "summary": summary.get("total"), "error": None})
                self.add_event("bench_complete", algorithm=algorithm, log_dir=str(run_log_dir))
            except Exception as exc:
                tb = traceback.format_exc()
                err_dir = Path(self.bench_status.get("log_dir") or str(actual_log_dir))
                err_dir.mkdir(parents=True, exist_ok=True)
                (err_dir / "bench_error.json").write_text(json.dumps({"ts": datetime.now().isoformat(), "algorithm": algorithm, "error": str(exc), "traceback": tb}, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
                with self.lock:
                    self.bench_status.update({"running": False, "status": "error", "message": str(exc), "finished_at": datetime.now().isoformat(timespec="seconds"), "error": str(exc)})
                self.add_event("bench_error", error=str(exc))

        t = threading.Thread(target=worker, name="pc-bench", daemon=True)
        self.bench_thread = t
        t.start()
        return self.get_bench_status()

    def get_bench_status(self) -> dict:
        with self.lock:
            status = dict(self.bench_status)
        if status.get("log_dir"):
            p = Path(status["log_dir"])
            status["event_count"] = _count_lines(_run_file_path(p, "events.jsonl"))
            status["round_count"] = _count_lines(_run_file_path(p, "rounds.jsonl"))
        return status

    # ---- listen ----

    def start_listen(self, port: str, baud: int) -> dict:
        with self.lock:
            if self.listen_status["running"]:
                raise RuntimeError("监听已在运行")
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
                    for msg in client.read_available():
                        if isinstance(msg, RssiReport):
                            updated = self.live_topology.update_from_rssi_report(msg)
                            self.add_event("rssi_report", src=format_addr(msg.src_addr), neighbors=[asdict(n) for n in msg.neighbors], edges=[asdict(e) for e in updated])
                        elif isinstance(msg, Ack):
                            self.add_event("ack", src=format_addr(msg.src_addr), seq=msg.seq)
                    time.sleep(0.02)
            except Exception as exc:
                self.add_event("listen_error", error=str(exc))
                with self.lock:
                    self.listen_status["error"] = str(exc)
            finally:
                if client:
                    client.close()
                with self.lock:
                    self.listen_client = None
                    self.listen_status["running"] = False
                self.add_event("listen_stop")

        t = threading.Thread(target=worker, name="pc-listen", daemon=True)
        self.listen_thread = t
        t.start()
        return self.get_listen_status()

    def stop_listen(self) -> dict:
        self.listen_stop.set()
        if self.listen_thread and self.listen_thread.is_alive():
            self.listen_thread.join(timeout=2.0)
        return self.get_listen_status()

    def get_listen_status(self) -> dict:
        with self.lock:
            return dict(self.listen_status)

    # ---- manual path send ----

    def manual_send_path(self, config: dict) -> dict:
        port        = str(config.get("port") or "/dev/ttyUSB0")
        baud        = int(config.get("baud") or 115200)
        path_str    = str(config["path"]).strip()
        payload     = str(config.get("payload") or "AABBCC")
        ack_timeout = float(config.get("ack_timeout") or 3.0)
        rounds      = int(config.get("rounds") or 1)
        interval    = float(config.get("interval") or 1.0)

        path = [parse_addr(x) for x in path_str.split()]
        if len(path) < 2:
            raise RuntimeError("路径至少需要 2 个节点")
        dst = path[-1]
        command = build_send_command(dst, path, payload)

        results = []
        client = SerialClient(port, baud)
        try:
            for i in range(rounds):
                client.write_command(command)
                started = time.monotonic()
                deadline = started + ack_timeout
                got = False
                while time.monotonic() < deadline:
                    for msg in client.read_available():
                        if isinstance(msg, Ack) and msg.src_addr == dst:
                            latency_ms = (time.monotonic() - started) * 1000.0
                            results.append({"success": True, "latency_ms": round(latency_ms, 1), "seq": msg.seq})
                            got = True
                            break
                    if got:
                        break
                if not got:
                    results.append({"success": False, "latency_ms": None, "seq": None})
                if i < rounds - 1:
                    time.sleep(interval)
        finally:
            client.close()

        success_count = sum(1 for r in results if r["success"])
        latencies = [r["latency_ms"] for r in results if r["latency_ms"] is not None]
        summary = {
            "command": command.rstrip(),
            "path": [format_addr(n) for n in path],
            "rounds": rounds,
            "success": success_count,
            "loss_rate": round(1 - success_count / rounds, 4) if rounds else None,
            "avg_ms": round(sum(latencies) / len(latencies), 1) if latencies else None,
            "min_ms": round(min(latencies), 1) if latencies else None,
            "max_ms": round(max(latencies), 1) if latencies else None,
            "results": results,
        }
        self.add_event("manual_path_send", **{k: v for k, v in summary.items() if k != "results"})
        return summary

    # ---- events ----

    def recent_events(self, limit: int = 100) -> dict:
        with self.lock:
            events = list(self.events)[-limit:]
            topology = topology_to_dict(self.live_topology)
            listen_status = dict(self.listen_status)
            bench_status = dict(self.bench_status)
        log_events = _read_recent_jsonl(_run_file_path(Path(bench_status["log_dir"]), "events.jsonl"), limit) if bench_status.get("log_dir") else []
        if log_events:
            events = log_events[-limit:]
        return {"events": events, "topology": topology, "listen": listen_status}


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _safe_name(value: str) -> str:
    safe = "".join(ch if ch.isalnum() or ch in {"-", "_", "."} else "_" for ch in value)
    return safe.strip("._") or f"ui_{datetime.now().strftime('%Y%m%d_%H%M%S')}"

def _count_lines(path: Path) -> int:
    if not path.exists():
        return 0
    with path.open("r", encoding="utf-8", errors="ignore") as f:
        return sum(1 for _ in f)

def _read_json(path: Path) -> dict | list | None:
    if not path.exists():
        return None
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)

def _run_file_path(run_dir: Path, filename: str) -> Path:
    aliases = {
        "summary.json": ["summary.json"],
        "routes.json": ["routes.json"],
        "state.json": ["state.json"],
        "events.jsonl": ["events.jsonl"],
        "rounds.jsonl": ["rounds.jsonl"],
        "readable_report.md": ["测试结果汇报.md", "readable_report.md"],
        "topology.svg": ["拓扑图.svg", "topology.svg"],
        "topology.txt": ["拓扑图.txt", "topology.txt"],
        "raw_serial.log": ["原始串口日志.log", "raw_serial.log"],
    }
    for alias in aliases.get(filename, [filename]):
        for candidate in [run_dir / alias, run_dir / "原始JSON数据" / alias]:
            if candidate.exists():
                return candidate
    return run_dir / filename

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
    runs: list[Path] = []
    for root in [log_root / "dijkstra_hw", log_root / "d3qn_hw"]:
        if not root.exists():
            continue
        for marker in list(root.rglob("summary.json")) + list(root.rglob("bench_error.json")):
            run_dir = marker.parent.parent if marker.parent.name == "原始JSON数据" else marker.parent
            if run_dir not in runs:
                runs.append(run_dir)
    return sorted(runs, key=lambda p: p.stat().st_mtime, reverse=True)

def list_runs(log_root: Path) -> list[dict]:
    runs = []
    for run_dir in _run_dirs(log_root):
        summary = _read_json(_run_file_path(run_dir, "summary.json")) or {}
        error   = _read_json(run_dir / "bench_error.json") or {}
        cfg     = summary.get("config", {})
        rel     = run_dir.relative_to(log_root).as_posix()
        total   = summary.get("total", {})
        runs.append({
            "id": rel,
            "name": run_dir.name,
            "mtime": datetime.fromtimestamp(run_dir.stat().st_mtime).isoformat(timespec="seconds"),
            "status": "error" if error else "complete",
            "error": error.get("error"),
            "algorithm": error.get("algorithm") or cfg.get("algorithm") or ("d3qn" if rel.startswith("d3qn_hw/") else "dijkstra"),
            "nodes": [format_addr(n) for n in cfg.get("nodes", [])],
            "rounds": cfg.get("rounds"),
            "sent": total.get("sent"),
            "success": total.get("success"),
            "loss_rate": total.get("loss_rate"),
            "avg_ms": total.get("latency", {}).get("avg_ms"),
        })
    return runs

def _resolve_run(log_root: Path, run_id: str) -> Path:
    parts = [p for p in unquote(run_id).split("/") if p]
    if any(p in {"..", "."} for p in parts):
        raise FileNotFoundError(run_id)
    run_dir = log_root.joinpath(*parts)
    if not run_dir.exists():
        raise FileNotFoundError(run_id)
    return run_dir

def load_run(log_root: Path, run_id: str) -> dict:
    run_dir = _resolve_run(log_root, run_id)
    state_path = _run_file_path(run_dir, "state.json")
    topology = load_topology(state_path) if state_path.exists() else Topology(stale_seconds=None, edge_direction="src_to_neighbor")
    return {
        "id": run_dir.relative_to(log_root).as_posix(),
        "error": _read_json(run_dir / "bench_error.json"),
        "summary": _read_json(_run_file_path(run_dir, "summary.json")),
        "routes": _read_json(_run_file_path(run_dir, "routes.json")),
        "topology": topology_to_dict(topology),
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
    file_path = _run_file_path(run_dir, Path(unquote(filename)).name)
    if not file_path.exists() or not file_path.is_file():
        raise FileNotFoundError(filename)
    return file_path


# ---------------------------------------------------------------------------
# HTTP Handler
# ---------------------------------------------------------------------------

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
            elif path == "/api/bench/status":
                self._send_json(self.state.get_bench_status())
            elif path == "/api/events":
                self._send_json(self.state.recent_events())
            elif path == "/api/serial/status":
                qs = parse_qs(parsed.query)
                port = (qs.get("port") or ["/dev/ttyUSB0"])[0]
                socket_path = get_socket_path(port)
                self._send_json({"daemon_running": os.path.exists(socket_path), "socket_path": socket_path, "port": port})
            elif path.startswith("/api/runs/") and path.endswith("/topology.svg"):
                run_id = path[len("/api/runs/"):-len("/topology.svg")]
                self._send_text(run_topology_svg(self.state.log_root, run_id), "image/svg+xml")
            elif path.startswith("/api/runs/") and "/file/" in path:
                run_id, filename = path[len("/api/runs/"):].split("/file/", 1)
                self._send_file(resolve_run_file(self.state.log_root, run_id, filename))
            elif path.startswith("/api/runs/"):
                self._send_json(load_run(self.state.log_root, path[len("/api/runs/"):]))
            else:
                self.send_error(HTTPStatus.NOT_FOUND)
        except FileNotFoundError:
            self.send_error(HTTPStatus.NOT_FOUND, "not found")
        except Exception as exc:
            self._send_json({"error": str(exc)}, status=500)

    def do_POST(self) -> None:
        parsed = urlparse(self.path)
        try:
            body = self._read_json_body()
            if parsed.path == "/api/bench/start":
                self._send_json(self.state.start_bench(body))
            elif parsed.path == "/api/listen/start":
                self._send_json(self.state.start_listen(str(body.get("port") or "/dev/ttyUSB0"), int(body.get("baud") or 115200)))
            elif parsed.path == "/api/listen/stop":
                self._send_json(self.state.stop_listen())
            elif parsed.path == "/api/manual/send":
                self._send_json(self.state.manual_send_path(body))
            else:
                self.send_error(HTTPStatus.NOT_FOUND)
        except Exception as exc:
            self._send_json({"error": str(exc)}, status=400)

    def log_message(self, fmt: str, *args) -> None:
        return

    def _read_json_body(self) -> dict:
        length = int(self.headers.get("Content-Length") or "0")
        return json.loads(self.rfile.read(length).decode("utf-8")) if length else {}

    def _send_json(self, data, status: int = 200) -> None:
        body = json.dumps(data, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _send_text(self, text: str, content_type: str = "text/plain; charset=utf-8", status: int = 200) -> None:
        body = text.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _send_file(self, path: Path) -> None:
        body = path.read_bytes()
        self.send_response(200)
        self.send_header("Content-Type", mimetypes.guess_type(path.name)[0] or "application/octet-stream")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Content-Disposition", "inline")
        self.end_headers()
        self.wfile.write(body)


# ---------------------------------------------------------------------------
# Server
# ---------------------------------------------------------------------------

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
    print(f"Web UI: http://{host}:{server.server_port}")
    try:
        server.serve_forever()
    finally:
        server.server_close()


# ---------------------------------------------------------------------------
# Frontend
# ---------------------------------------------------------------------------

INDEX_HTML = r"""<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>广播组网上位机</title>
<style>
:root{--bg:#f1f5f9;--panel:#fff;--ink:#1e293b;--muted:#64748b;--line:#e2e8f0;--accent:#2563eb;--ok:#16a34a;--warn:#d97706;--bad:#dc2626}
*{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:var(--ink);font:13px/1.5 system-ui,sans-serif}
header{height:52px;display:flex;align-items:center;justify-content:space-between;padding:0 20px;background:#0f172a;color:#f8fafc}
header h1{font-size:16px;font-weight:700}
#daemonBadge{font-size:12px;padding:3px 10px;border-radius:999px;background:#334155;color:#94a3b8}
#daemonBadge.ok{background:#14532d;color:#86efac}
main{display:grid;grid-template-columns:340px 1fr;min-height:calc(100vh - 52px)}
aside{background:#fff;border-right:1px solid var(--line);padding:12px;overflow-y:auto;display:flex;flex-direction:column;gap:10px}
section{padding:16px;overflow-y:auto}
.card{border:1px solid var(--line);border-radius:8px;padding:12px}
.card h2{font-size:13px;font-weight:700;margin-bottom:10px;color:#334155}
label{display:block;font-size:11px;color:var(--muted);margin:8px 0 3px}
input,select{width:100%;height:32px;border:1px solid var(--line);border-radius:5px;padding:0 8px;font-size:13px;background:#fff;color:var(--ink)}
.row2{display:grid;grid-template-columns:1fr 1fr;gap:8px}
.row3{display:grid;grid-template-columns:1fr 1fr 1fr;gap:8px}
button{display:block;width:100%;height:34px;border:1px solid var(--accent);border-radius:5px;background:var(--accent);color:#fff;font-size:13px;font-weight:600;cursor:pointer;margin-top:8px}
button.sec{background:#fff;color:var(--ink);border-color:var(--line)}
button.ok{background:var(--ok);border-color:var(--ok)}
button.bad{background:var(--bad);border-color:var(--bad)}
.status{font-size:11px;color:var(--muted);margin-top:5px;min-height:16px;word-break:break-all}
.status.ok{color:var(--ok)}
.status.bad{color:var(--bad)}
.runs{display:flex;flex-direction:column;gap:6px}
.run{border:1px solid var(--line);border-radius:6px;padding:8px 10px;cursor:pointer}
.run:hover{border-color:#93c5fd}
.run.active{border-color:var(--accent);box-shadow:0 0 0 2px #dbeafe}
.run-name{font-weight:700;font-size:12px}
.run-meta{font-size:11px;color:var(--muted);margin-top:2px}
.metrics{display:grid;grid-template-columns:repeat(4,1fr);gap:10px;margin-bottom:12px}
.metric{border:1px solid var(--line);border-radius:7px;padding:10px;background:#fff}
.metric span{font-size:11px;color:var(--muted)}
.metric b{display:block;font-size:18px;margin-top:2px}
table{width:100%;border-collapse:collapse;font-size:12px;background:#fff;border:1px solid var(--line);border-radius:7px;overflow:hidden;margin-bottom:12px}
th,td{padding:7px 9px;border-bottom:1px solid var(--line);text-align:left;white-space:nowrap}
th{background:#f8fafc;color:#475569;font-size:11px}
pre{background:#0f172a;color:#bfdbfe;padding:10px;border-radius:7px;font-size:11px;max-height:240px;overflow:auto;margin-bottom:12px;white-space:pre-wrap}
.topo img{width:100%;border:1px solid var(--line);border-radius:7px}
.report{background:#fff;border:1px solid var(--line);border-radius:7px;padding:12px;margin-bottom:12px;white-space:pre-wrap;font-size:12px;font-family:monospace;max-height:500px;overflow:auto}
#d3qnExtra,#onlineExtra{display:none}
.tabs{display:flex;gap:6px;margin-bottom:10px}
.tab{flex:1;height:30px;border:1px solid var(--line);border-radius:5px;background:#fff;cursor:pointer;font-size:12px;font-weight:600;color:var(--muted)}
.tab.active{background:var(--accent);color:#fff;border-color:var(--accent)}
</style>
</head>
<body>
<header>
  <h1>广播组网上位机</h1>
  <div id="daemonBadge">守护进程: 检测中...</div>
</header>
<main>
<aside>
  <!-- 串口连接 -->
  <div class="card">
    <h2>串口连接</h2>
    <label>串口</label><input id="port" value="/dev/ttyUSB0">
    <div class="row2">
      <div><label>波特率</label><input id="baud" value="115200"></div>
      <div><label>Dongle 地址(十进制)</label><input id="dongleAddr" value="16"></div>
    </div>
    <div class="row2">
      <button class="ok" onclick="startListen()">开始监听</button>
      <button class="bad sec" onclick="stopListen()">停止监听</button>
    </div>
    <div class="status" id="listenStatus">未连接</div>
  </div>

  <!-- 启动测试 -->
  <div class="card">
    <h2>启动测试</h2>
    <div class="tabs">
      <button class="tab active" onclick="setAlgo('dijkstra',this)">Dijkstra</button>
      <button class="tab" onclick="setAlgo('d3qn',this)">D3QN</button>
    </div>
    <label>节点列表（十六进制，逗号分隔）</label><input id="nodes" value="1,2,3,4,5,6,7,8,9,10">
    <label>源节点限制（留空=全部）</label><input id="sources" placeholder="如 1 或 1,2,3">
    <div class="row2">
      <div><label>每对轮数</label><input id="rounds" value="2"></div>
      <div><label>ACK 超时 s</label><input id="ackTimeout" value="5.0"></div>
    </div>
    <div class="row2">
      <div><label>发包间隔 s</label><input id="interval" value="0"></div>
      <div><label>RSSI_REQ 次数</label><input id="rssiReq" value="2"></div>
    </div>
    <div id="djExtra">
      <label>路由模式</label>
      <select id="routeMode">
        <option value="sample_dijkstra" selected>sample_dijkstra（推荐）</option>
        <option value="baseline_dijkstra">baseline_dijkstra</option>
        <option value="reliable_dijkstra_v1">reliable_dijkstra_v1</option>
      </select>
      <div class="row2" style="margin-top:8px">
        <div><label>跳数惩罚</label><input id="hopPenalty" value="0.0" type="number" step="0.5" min="0"></div>
        <div style="display:flex;align-items:flex-end;padding-bottom:4px">
          <label style="margin:0;display:flex;align-items:center;gap:4px;cursor:pointer">
            <input type="checkbox" id="enableCongestion"> 拥塞惩罚
          </label>
        </div>
      </div>
      <div style="margin-top:6px;display:flex;align-items:center;gap:4px">
        <input type="checkbox" id="dynamicPause">
        <label style="margin:0;cursor:pointer" for="dynamicPause">动态模式（自动暂停轮次，轮数×2）</label>
      </div>
    </div>
    <div id="d3qnExtra">
      <label>Checkpoint</label><input id="checkpoint" placeholder="留空=latest.pt">
      <label><input type="checkbox" id="onlineLearn" onchange="$('onlineExtra').style.display=this.checked?'block':'none'"> 开启在线学习</label>
      <div id="onlineExtra">
        <div class="row3">
          <div><label>间隔轮数</label><input id="onlineInterval" value="100"></div>
          <div><label>学习率</label><input id="onlineLr" value="3e-4"></div>
          <div><label>nudge</label><input id="onlineNudge" value="0.8"></div>
        </div>
      </div>
    </div>
    <div class="row2">
      <div><label>Payload</label><input id="payload" value="AABBCC"></div>
      <div><label>日志目录名（可选）</label><input id="logName" placeholder="自动生成"></div>
    </div>
    <button onclick="startBench()">开始测试</button>
    <div class="status" id="benchStatus">空闲</div>
  </div>

  <!-- 单路径测试 -->
  <div class="card">
    <h2>单路径测试</h2>
    <label>路径（空格分隔十六进制，如 00 01 06 04）</label>
    <input id="manualPath" value="00 01">
    <div class="row2">
      <div><label>Payload</label><input id="manualPayload" value="AABBCC"></div>
      <div><label>ACK 超时 s</label><input id="manualAck" value="3.0"></div>
    </div>
    <div class="row2">
      <div><label>轮次</label><input id="manualRounds" value="3"></div>
      <div><label>间隔 s</label><input id="manualInterval" value="0.5"></div>
    </div>
    <button onclick="manualSend()">发送</button>
    <div class="status" id="manualStatus"></div>
    <div id="manualResult"></div>
  </div>

  <!-- 历史测试 -->
  <div class="card">
    <h2>历史测试 <button class="sec" style="width:auto;padding:0 10px;height:26px;margin:0 0 0 8px;font-size:11px" onclick="loadRuns()">刷新</button></h2>
    <div class="runs" id="runs"></div>
  </div>
</aside>

<section>
  <div class="metrics" id="metrics"></div>
  <div id="topology" class="topo" style="margin-bottom:12px"></div>
  <div class="report" id="report" style="display:none"></div>
  <div id="targets"></div>
  <h3 style="margin:10px 0 6px;font-size:13px">事件流</h3>
  <pre id="events"></pre>
</section>
</main>

<script>
const $=id=>document.getElementById(id);
let currentRun=null, currentAlgo='dijkstra';

function pct(v){return v==null?'n/a':(v*100).toFixed(1)+'%'}
function ms(v){return v==null?'n/a':v.toFixed(1)+'ms'}
function esc(v){return String(v??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}
function pathStr(p){return(p||[]).map(n=>n.toString(16).toUpperCase().padStart(2,'0')).join(' → ')}

async function api(path,opts){
  if(opts&&opts.body&&!opts.headers)opts={...opts,headers:{'Content-Type':'application/json'}};
  const r=await fetch(path,opts);
  const t=await r.text();
  const d=t?JSON.parse(t):{};
  if(!r.ok)throw new Error(d.error||r.statusText);
  return d;
}

function setAlgo(algo,btn){
  currentAlgo=algo;
  document.querySelectorAll('.tab').forEach(t=>t.classList.remove('active'));
  btn.classList.add('active');
  $('djExtra').style.display=algo==='dijkstra'?'block':'none';
  $('d3qnExtra').style.display=algo==='d3qn'?'block':'none';
}

// Serial daemon status
async function checkDaemon(){
  try{
    const d=await api('/api/serial/status?port='+encodeURIComponent($('port').value));
    const badge=$('daemonBadge');
    if(d.daemon_running){badge.textContent='守护进程: 运行中';badge.className='ok';}
    else{badge.textContent='守护进程: 未启动';badge.className='';}
  }catch(e){}
}

// Listen
async function startListen(){
  try{
    const s=await api('/api/listen/start',{method:'POST',body:JSON.stringify({port:$('port').value,baud:+$('baud').value})});
    updateListenStatus(s);
  }catch(e){$('listenStatus').textContent=e.message;$('listenStatus').className='status bad';}
}
async function stopListen(){
  try{
    const s=await api('/api/listen/stop',{method:'POST',body:'{}'});
    updateListenStatus(s);
  }catch(e){}
}
function updateListenStatus(s){
  const el=$('listenStatus');
  el.textContent=s.running?`监听中: ${s.port}`:(s.error?`错误: ${s.error}`:'已停止');
  el.className='status'+(s.running?' ok':s.error?' bad':'');
}

// Bench
async function startBench(){
  const payload={
    algorithm:currentAlgo,
    port:$('port').value, baud:+$('baud').value,
    nodes:$('nodes').value, sources:$('sources').value,
    rounds:+$('rounds').value, ack_timeout:+$('ackTimeout').value,
    interval:+$('interval').value, rssi_requests:+$('rssiReq').value,
    payload:$('payload').value, log_name:$('logName').value,
    dongle_addr:+$('dongleAddr').value, no_retry:true,
    route_mode:currentAlgo==='dijkstra'?$('routeMode').value:'sample_dijkstra',
    hop_penalty:+($('hopPenalty').value||0),
    enable_congestion:$('enableCongestion').checked,
    dynamic_pause:$('dynamicPause').checked,
    checkpoint:$('checkpoint').value,
    online_learn:$('onlineLearn').checked,
    online_interval:+$('onlineInterval').value,
    online_lr:+$('onlineLr').value,
    online_nudge:+$('onlineNudge').value,
  };
  try{
    const s=await api('/api/bench/start',{method:'POST',body:JSON.stringify(payload)});
    $('benchStatus').textContent=s.message+' · '+s.log_dir;
    $('benchStatus').className='status';
  }catch(e){$('benchStatus').textContent=e.message;$('benchStatus').className='status bad';}
}

async function pollBench(){
  try{
    const s=await api('/api/bench/status');
    const st=$('benchStatus');
    if(s.status==='complete'){
      st.textContent=`完成 · rounds ${s.round_count??0} · ${s.log_dir||''}`;
      st.className='status ok';
      await loadRuns();
      if(s.log_dir){const rid=runIdFromPath(s.log_dir);if(rid)loadRun(rid);}
    }else if(s.status==='error'){
      st.textContent=`错误: ${s.error||s.message}`;
      st.className='status bad';
      await loadRuns();
    }else if(s.status==='running'){
      st.textContent=`运行中 · events ${s.event_count??0} · rounds ${s.round_count??0}`;
      st.className='status';
    }
  }catch(e){console.error('pollBench',e);}
}
function runIdFromPath(p){const m='/logs/';return p&&p.includes(m)?p.split(m).pop():null;}

// Manual send
async function manualSend(){
  const st=$('manualStatus');
  const res=$('manualResult');
  st.textContent='发送中...'; res.innerHTML='';
  try{
    const d=await api('/api/manual/send',{method:'POST',body:JSON.stringify({
      port:$('port').value, baud:+$('baud').value,
      path:$('manualPath').value, payload:$('manualPayload').value,
      ack_timeout:+$('manualAck').value, rounds:+$('manualRounds').value,
      interval:+$('manualInterval').value,
    })});
    const ok=d.success===d.rounds;
    st.textContent=`${d.success}/${d.rounds} 成功 · 丢包率 ${pct(d.loss_rate)} · 均值 ${ms(d.avg_ms)}`;
    st.className='status'+(ok?' ok':' bad');
    const rows=d.results.map((r,i)=>`<tr><td>${i+1}</td><td>${r.success?'✓':'✗'}</td><td>${r.latency_ms!=null?r.latency_ms+'ms':'超时'}</td><td>${r.seq!=null?'0x'+r.seq.toString(16).padStart(4,'0'):'-'}</td></tr>`).join('');
    res.innerHTML=`<table style="margin-top:6px"><thead><tr><th>#</th><th>结果</th><th>延时</th><th>seq</th></tr></thead><tbody>${rows}</tbody></table>`;
  }catch(e){st.textContent=e.message;st.className='status bad';}
}

// Runs list
async function loadRuns(){
  try{
    const d=await api('/api/runs');
    $('runs').innerHTML=d.runs.map(r=>`<div class="run${r.id===currentRun?' active':''}" data-run-id="${esc(r.id)}" onclick="loadRun(this.dataset.runId)">
      <div class="run-name">${esc(r.name)}</div>
      <div class="run-meta">${esc(r.algorithm||'?')} · ${r.status==='error'?'<span style="color:var(--bad)">error</span>':esc(r.status||'')} · sent ${r.sent??'-'} · loss ${pct(r.loss_rate)} · avg ${ms(r.avg_ms)}</div>
    </div>`).join('');
  }catch(e){$('runs').innerHTML=`<div class="status bad">加载失败: ${esc(String(e.message))}</div>`;}
}

async function loadRun(id){
  currentRun=id;
  document.querySelectorAll('.run').forEach(el=>el.classList.toggle('active',el.dataset.runId===id));
  try{
    const encId=encodeURIComponent(id).replaceAll('%2F','/');
    const d=await api('/api/runs/'+encId);
    if(d.error){
      $('metrics').innerHTML=`<div class="metric" style="grid-column:span 4"><span>错误</span><b style="font-size:13px;color:var(--bad)">${esc(String(d.error.error||d.error))}</b></div>`;
      $('report').style.display='none';$('targets').innerHTML='';$('topology').innerHTML='';return;
    }
    const total=d.summary?.total||{};
    $('metrics').innerHTML=[
      ['发送',total.sent??'n/a'],['成功',total.success??'n/a'],
      ['丢包率',pct(total.loss_rate)],['平均延时',ms(total.latency?.avg_ms)]
    ].map(([l,v])=>`<div class="metric"><span>${l}</span><b>${v}</b></div>`).join('');
    $('topology').innerHTML=`<img src="/api/runs/${encId}/topology.svg?ts=${Date.now()}" onerror="this.style.display='none'">`;
    if(d.readable_report){$('report').textContent=d.readable_report;$('report').style.display='block';}else{$('report').style.display='none';}
    const pairs=d.summary?.pairs||d.summary?.targets||{};
    $('targets').innerHTML=Object.keys(pairs).length?renderPairs(pairs):'<p style="color:var(--muted);font-size:12px;padding:8px">无配对数据</p>';
  }catch(e){$('metrics').innerHTML=`<div class="metric" style="grid-column:span 4"><span>加载失败</span><b style="font-size:13px;color:var(--bad)">${esc(e.message)}</b></div>`;}
}

function renderPairs(pairs){
  const entries=Object.entries(pairs).sort(([,a],[,b])=>{
    const as=String(a.source||'').padStart(4,'0'),bs=String(b.source||'').padStart(4,'0');
    const ad=String(a.destination||'').padStart(4,'0'),bd=String(b.destination||'').padStart(4,'0');
    return as!==bs?as.localeCompare(bs):ad.localeCompare(bd);
  });
  const rows=entries.map(([,v])=>`<tr>
    <td>${esc(String(v.source??'?').toUpperCase())}</td>
    <td>${esc(String(v.destination??'?').toUpperCase())}</td>
    <td style="font-family:monospace">${esc(pathStr(v.last_route))}</td>
    <td>${v.success??'-'}/${v.sent??'-'}</td>
    <td style="color:${(v.loss_rate||0)>0.1?'var(--bad)':(v.loss_rate||0)>0?'var(--warn)':'var(--ok)'}">${pct(v.loss_rate)}</td>
    <td>${ms(v.latency?.avg_ms)}</td>
    <td>${ms(v.latency?.p95_ms)}</td>
    <td>${ms(v.inference_latency?.avg_ms)}</td>
  </tr>`).join('');
  return `<h3 style="margin:10px 0 6px;font-size:13px">逐对结果（${entries.length} 对）</h3><table><thead><tr><th>源</th><th>目标</th><th>路径</th><th>成功/发送</th><th>丢包率</th><th>均值延时</th><th>P95</th><th>推理</th></tr></thead><tbody>${rows}</tbody></table>`;
}

// Event stream
async function pollEvents(){
  try{
    const d=await api('/api/events');
    $('events').textContent=d.events.slice(-80).map(e=>`${e.ts} [${e.type}] ${JSON.stringify(e)}`).join('\n');
  }catch(e){}
}

// Init
loadRuns().catch(e=>console.error('loadRuns init',e));
checkDaemon();
setInterval(pollBench,2000);
setInterval(pollEvents,1000);
setInterval(checkDaemon,5000);
</script>
</body>
</html>"""
