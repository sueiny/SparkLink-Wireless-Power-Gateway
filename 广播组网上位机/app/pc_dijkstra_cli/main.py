from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from .benchmark import parse_float_list, parse_int_list, parse_node_list, parse_optional_node_list, parse_route_mode_list, run_benchmark, run_interval_sweep, run_optimization_sweep
from .launcher import launch_ui
from .protocol import Ack, ProtocolError, RssiReport, build_send_command, parse_addr
from .serial_client import SerialClient
from .topology import Topology, load_topology, routes_to_dict, save_topology
from .ui_server import serve_ui


DEFAULT_STATE = Path(__file__).resolve().parent / "state.json"
DEFAULT_LOG_ROOT = Path(__file__).resolve().parents[1] / "logs"


def _format_path(path: list[int]) -> str:
    return " -> ".join(f"{addr:02X}" for addr in path)


def _format_rate(value: float | None) -> str:
    return "n/a" if value is None else f"{value:.2%}"


def _print_routes(topology: Topology) -> None:
    routes = topology.routes()
    if not routes:
        print("No reachable routes.")
        return
    for key, route in sorted(routes.items()):
        print(f"{key} cost={route.cost:g} path={_format_path(route.path)}")


def cmd_listen(args: argparse.Namespace) -> int:
    topology = load_topology(args.state)
    client = SerialClient(args.port, args.baud)
    try:
        for message in client.iter_messages():
            if isinstance(message, RssiReport):
                updated = topology.update_from_rssi_report(message)
                save_topology(args.state, topology)
                edge_text = ", ".join(f"{edge.src:02X}->{edge.dst:02X} rssi={edge.rssi} w={edge.weight}" for edge in updated)
                print(f"RSSI src={message.src_addr:02X} count={len(message.neighbors)} {edge_text}")
            elif isinstance(message, Ack):
                print(f"ACK src={message.src_addr:02X} dst={message.dst_addr:02X} seq=0x{message.seq:04X}")
    finally:
        client.close()


def cmd_routes(args: argparse.Namespace) -> int:
    _print_routes(load_topology(args.state))
    return 0


def cmd_path(args: argparse.Namespace) -> int:
    topology = load_topology(args.state)
    route = topology.route(parse_addr(args.src), parse_addr(args.dst))
    if route.status != "valid":
        print(f"unreachable: {args.src} -> {args.dst}", file=sys.stderr)
        return 2
    print(f"cost={route.cost:g} path={_format_path(route.path)}")
    return 0


def cmd_send(args: argparse.Namespace) -> int:
    topology = load_topology(args.state)
    src = parse_addr(args.src)
    dst = parse_addr(args.dst)
    route = topology.route(src, dst)
    if route.status != "valid":
        print(f"unreachable: {args.src} -> {args.dst}", file=sys.stderr)
        return 2

    command = build_send_command(dst, route.path, args.payload)
    client = SerialClient(args.port, args.baud)
    try:
        client.serial.write(command.encode("ascii"))
    finally:
        client.close()
    print(command.rstrip())
    return 0


def cmd_bench(args: argparse.Namespace) -> int:
    summary = run_benchmark(
        port=args.port,
        baud=args.baud,
        nodes=parse_node_list(args.nodes),
        rounds=args.rounds,
        payload=args.payload,
        log_dir=args.log_dir,
        boot_wait=args.boot_wait,
        rssi_seconds=args.rssi_seconds,
        rssi_requests=args.rssi_requests,
        ack_timeout=args.ack_timeout,
        interval=args.interval,
        gateway=parse_addr(args.gateway),
        dongle_addr=parse_addr(args.dongle_addr) if args.dongle_addr else None,
        min_rssi=args.min_rssi,
        route_mode=args.route_mode,
        sources=parse_optional_node_list(args.sources),
        recollect_consecutive_failures=args.recollect_consecutive_failures,
    )
    print(f"bench complete: sent={summary['total']['sent']} success={summary['total']['success']} loss={_format_rate(summary['total']['loss_rate'])}")
    report_dir = Path(summary.get("log_dir") or summary.get("config", {}).get("log_dir") or args.log_dir)
    print(f"report: {report_dir / '测试结果汇报.md'}")
    return 0


def cmd_sweep(args: argparse.Namespace) -> int:
    sweep = run_interval_sweep(
        port=args.port,
        baud=args.baud,
        nodes=parse_node_list(args.nodes),
        rounds=args.rounds,
        payload=args.payload,
        log_dir=args.log_dir,
        intervals=parse_float_list(args.intervals),
        boot_wait=args.boot_wait,
        rssi_seconds=args.rssi_seconds,
        rssi_requests=args.rssi_requests,
        ack_timeout=args.ack_timeout,
        gateway=parse_addr(args.gateway),
        route_mode=args.route_mode,
    )
    print("sweep complete:")
    for item in sweep["results"]:
        print(f"interval={item['interval']:g}s sent={item['sent']} success={item['success']} loss={_format_rate(item['loss_rate'])}")
    print(f"report: {Path(args.log_dir) / 'sweep_report.md'}")
    return 0


def cmd_optimize(args: argparse.Namespace) -> int:
    sweep = run_optimization_sweep(
        port=args.port,
        baud=args.baud,
        nodes=parse_node_list(args.nodes),
        rounds=args.rounds,
        payload=args.payload,
        log_dir=args.log_dir,
        intervals=parse_float_list(args.intervals),
        route_modes=parse_route_mode_list(args.route_modes),
        rssi_requests_values=parse_int_list(args.rssi_requests_values),
        boot_wait=args.boot_wait,
        rssi_seconds=args.rssi_seconds,
        ack_timeout=args.ack_timeout,
        gateway=parse_addr(args.gateway),
        target_min=args.target_min,
        target_max=args.target_max,
    )
    best = sweep.get("best")
    print("optimization complete:")
    if best:
        print(
            f"best route_mode={best['route_mode']} interval={best['interval']:g}s "
            f"rssi_requests={best['rssi_requests']} sent={best['sent']} success={best['success']} "
            f"loss={_format_rate(best['loss_rate'])} in_target={best['in_target']}"
        )
    print(f"report: {Path(args.log_dir) / 'optimization_report.md'}")
    return 0


def cmd_export(args: argparse.Namespace) -> int:
    topology = load_topology(args.state)
    output = {
        "topology": topology.to_dict(),
        "routes": routes_to_dict(topology.routes()),
    }
    routes_path = Path(args.routes)
    routes_path.parent.mkdir(parents=True, exist_ok=True)
    with routes_path.open("w", encoding="utf-8") as file_obj:
        json.dump(output, file_obj, ensure_ascii=False, indent=2, sort_keys=True)
        file_obj.write("\n")
    print(f"exported {routes_path}")
    return 0


def cmd_ui(args: argparse.Namespace) -> int:
    serve_ui(host=args.host, port=args.port, log_root=args.log_root)
    return 0


def cmd_launch(args: argparse.Namespace) -> int:
    launch_ui(
        host=args.host,
        port=args.port,
        log_root=args.log_root,
        open_browser=not args.no_browser,
        attempts=args.port_attempts,
    )
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Dijkstra PC CLI for the mesh serial protocol")
    subparsers = parser.add_subparsers(dest="command", required=True)

    listen = subparsers.add_parser("listen", help="read RSSI/ACK frames from dongle")
    listen.add_argument("--port", required=True)
    listen.add_argument("--baud", type=int, default=115200)
    listen.add_argument("--state", default=str(DEFAULT_STATE))
    listen.set_defaults(func=cmd_listen)

    routes = subparsers.add_parser("routes", help="print reachable routes from saved state")
    routes.add_argument("--state", default=str(DEFAULT_STATE))
    routes.set_defaults(func=cmd_routes)

    path = subparsers.add_parser("path", help="print one shortest path")
    path.add_argument("--state", default=str(DEFAULT_STATE))
    path.add_argument("--src", required=True)
    path.add_argument("--dst", required=True)
    path.set_defaults(func=cmd_path)

    send = subparsers.add_parser("send", help="send payload through a computed path")
    send.add_argument("--port", required=True)
    send.add_argument("--baud", type=int, default=115200)
    send.add_argument("--state", default=str(DEFAULT_STATE))
    send.add_argument("--src", default="00")
    send.add_argument("--dst", required=True)
    send.add_argument("--payload", required=True)
    send.set_defaults(func=cmd_send)

    bench = subparsers.add_parser("bench", help="run hardware Dijkstra benchmark")
    bench.add_argument("--port", required=True)
    bench.add_argument("--baud", type=int, default=115200)
    bench.add_argument("--nodes", default="1,2,3,4,5,6,7,8,9,10")
    bench.add_argument("--rounds", type=int, default=20)
    bench.add_argument("--payload", default="AABBCC")
    bench.add_argument("--log-dir", default=str(DEFAULT_LOG_ROOT / "dijkstra_hw"))
    bench.add_argument("--gateway", default="00")
    bench.add_argument("--dongle-addr", default=None, help="dongle address to exclude from routing (e.g. 10)")
    bench.add_argument("--min-rssi", type=int, default=-85, help="minimum RSSI threshold for routing edges")
    bench.add_argument("--boot-wait", type=float, default=5.0)
    bench.add_argument("--rssi-seconds", type=float, default=8.0)
    bench.add_argument("--rssi-requests", type=int, default=5)
    bench.add_argument("--ack-timeout", type=float, default=2.0)
    bench.add_argument("--interval", type=float, default=1.0)
    bench.add_argument("--route-mode", choices=["baseline_dijkstra", "reliable_dijkstra_v1"], default="baseline_dijkstra")
    bench.add_argument("--sources", default=None, help="comma-separated source nodes; default uses all --nodes")
    bench.add_argument("--recollect-consecutive-failures", type=int, default=3)
    bench.set_defaults(func=cmd_bench)

    sweep = subparsers.add_parser("sweep", help="run benchmark across command intervals")
    sweep.add_argument("--port", required=True)
    sweep.add_argument("--baud", type=int, default=115200)
    sweep.add_argument("--nodes", default="1,2,3,4,5,6,7,8,9,10")
    sweep.add_argument("--rounds", type=int, default=10)
    sweep.add_argument("--payload", default="AABBCC")
    sweep.add_argument("--log-dir", default=str(DEFAULT_LOG_ROOT / "dijkstra_hw_sweep"))
    sweep.add_argument("--intervals", default="0.3,0.5,0.8,1.0,1.5")
    sweep.add_argument("--gateway", default="00")
    sweep.add_argument("--boot-wait", type=float, default=5.0)
    sweep.add_argument("--rssi-seconds", type=float, default=15.0)
    sweep.add_argument("--rssi-requests", type=int, default=5)
    sweep.add_argument("--ack-timeout", type=float, default=2.0)
    sweep.add_argument("--route-mode", choices=["baseline_dijkstra", "reliable_dijkstra_v1"], default="baseline_dijkstra")
    sweep.set_defaults(func=cmd_sweep)

    optimize = subparsers.add_parser("optimize", help="scan route modes and safe command intervals for target loss")
    optimize.add_argument("--port", required=True)
    optimize.add_argument("--baud", type=int, default=115200)
    optimize.add_argument("--nodes", default="1,2,3,4,5,6,7,8,9,10")
    optimize.add_argument("--rounds", type=int, default=20)
    optimize.add_argument("--payload", default="AABBCC")
    optimize.add_argument("--log-dir", default=str(DEFAULT_LOG_ROOT / "dijkstra_hw_optimize"))
    optimize.add_argument("--intervals", default="0.2,0.3,0.5,0.8,1.0,1.5")
    optimize.add_argument("--route-modes", default="baseline_dijkstra,reliable_dijkstra_v1")
    optimize.add_argument("--rssi-requests-values", default="5,8")
    optimize.add_argument("--gateway", default="00")
    optimize.add_argument("--boot-wait", type=float, default=5.0)
    optimize.add_argument("--rssi-seconds", type=float, default=8.0)
    optimize.add_argument("--ack-timeout", type=float, default=2.0)
    optimize.add_argument("--target-min", type=float, default=0.04)
    optimize.add_argument("--target-max", type=float, default=0.06)
    optimize.set_defaults(func=cmd_optimize)

    export = subparsers.add_parser("export", help="export topology and routes as JSON")
    export.add_argument("--state", default=str(DEFAULT_STATE))
    export.add_argument("--routes", required=True)
    export.set_defaults(func=cmd_export)

    ui = subparsers.add_parser("ui", help="start local Web UI")
    ui.add_argument("--host", default="127.0.0.1")
    ui.add_argument("--port", type=int, default=8080)
    ui.add_argument("--log-root", default="app/广播组网上位机/app/logs")
    ui.set_defaults(func=cmd_ui)

    launch = subparsers.add_parser("launch", help="start local Web UI and open browser")
    launch.add_argument("--host", default="127.0.0.1")
    launch.add_argument("--port", type=int, default=8080)
    launch.add_argument("--log-root", default="app/广播组网上位机/app/logs")
    launch.add_argument("--no-browser", action="store_true")
    launch.add_argument("--port-attempts", type=int, default=20)
    launch.set_defaults(func=cmd_launch)
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return int(args.func(args) or 0)
    except (ProtocolError, ValueError, RuntimeError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
