from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from .batch_benchmark import run_batch_benchmark
from .benchmark import parse_node_list, parse_optional_node_list, run_benchmark
from .model import LATEST_CHECKPOINT, D3QNPredictor, checkpoint_metadata, demand_for_payload, run_sample_training, setup_env
from .protocol import parse_addr
from .rssi_profile import write_profile_from_run
from .state import write_d3qn_state
from .topology import load_topology


DEFAULT_STATE = Path(__file__).resolve().parent / "state.json"
DEFAULT_LOG_ROOT = Path(__file__).resolve().parents[1] / "logs" / "d3qn_hw"


def _fmt_path(path: list[int]) -> str:
    return " -> ".join(f"{addr:02X}" for addr in path)


def _fmt_rate(value: float | None) -> str:
    return "n/a" if value is None else f"{value:.2%}"


def _fmt_ms(value: float | None) -> str:
    return "n/a" if value is None else f"{value:.1f}ms"


def cmd_setup_env(args: argparse.Namespace) -> int:
    return setup_env(args.venv)


def cmd_train(args: argparse.Namespace) -> int:
    reward_profile = {
        "D3QN_REWARD_LOSS_WEIGHT": args.reward_loss_weight,
        "D3QN_REWARD_DELAY_WEIGHT": args.reward_delay_weight,
        "D3QN_REWARD_UTILIZATION_WEIGHT": args.reward_utilization_weight,
        "D3QN_REWARD_HOP_WEIGHT": args.reward_hop_weight,
        "D3QN_REWARD_WEAK_RSSI_WEIGHT": args.reward_weak_rssi_weight,
        "D3QN_REWARD_OVERLOAD_WEIGHT": args.reward_overload_weight,
    }
    checkpoint = run_sample_training(
        iterations=args.iterations,
        training_episodes=args.training_episodes,
        evaluation_episodes=args.evaluation_episodes,
        first_work_train_episode=args.first_work_train_episode,
        output_dir=args.output_dir,
        seed=args.seed,
        evaluation_interval=args.evaluation_interval,
        save_interval=args.save_interval,
        graph_lazy_paths=args.graph_lazy_paths,
        graph_require_full_k_paths=args.graph_require_full_k_paths,
        graph_num_nodes=args.graph_num_nodes,
        graph_target_edges=args.graph_target_edges,
        hardware_rssi_profile=args.hardware_rssi_profile,
        reward_profile=reward_profile,
        resume=getattr(args, "resume", None),
    )
    print(f"checkpoint: {checkpoint}")
    return 0


def cmd_train_fast(args: argparse.Namespace) -> int:
    reward_profile = {
        "D3QN_REWARD_LOSS_WEIGHT": args.reward_loss_weight,
        "D3QN_REWARD_DELAY_WEIGHT": args.reward_delay_weight,
        "D3QN_REWARD_UTILIZATION_WEIGHT": args.reward_utilization_weight,
        "D3QN_REWARD_HOP_WEIGHT": args.reward_hop_weight,
        "D3QN_REWARD_WEAK_RSSI_WEIGHT": args.reward_weak_rssi_weight,
        "D3QN_REWARD_OVERLOAD_WEIGHT": args.reward_overload_weight,
    }
    checkpoint = run_sample_training(
        iterations=args.iterations,
        training_episodes=args.training_episodes,
        evaluation_episodes=args.evaluation_episodes,
        first_work_train_episode=args.first_work_train_episode,
        output_dir=args.output_dir,
        seed=args.seed,
        evaluation_interval=args.evaluation_interval,
        save_interval=args.save_interval,
        graph_lazy_paths=args.graph_lazy_paths,
        graph_require_full_k_paths=args.graph_require_full_k_paths,
        graph_num_nodes=args.graph_num_nodes,
        graph_target_edges=args.graph_target_edges,
        hardware_rssi_profile=args.hardware_rssi_profile,
        reward_profile=reward_profile,
        resume=getattr(args, "resume", None),
    )
    print(f"fast checkpoint: {checkpoint}")
    return 0


def cmd_bench(args: argparse.Namespace) -> int:
    summary = run_benchmark(
        port=args.port,
        baud=args.baud,
        nodes=parse_node_list(args.nodes),
        rounds=args.rounds,
        payload=args.payload,
        log_dir=args.log_dir,
        checkpoint=args.checkpoint,
        boot_wait=args.boot_wait,
        rssi_requests=args.rssi_requests,
        ack_timeout=args.ack_timeout,
        interval=args.interval,
        gateway=parse_addr(args.gateway),
        dongle_addr=parse_addr(args.dongle_addr) if args.dongle_addr else None,
        sources=parse_optional_node_list(args.sources),
        recollect_consecutive_failures=args.recollect_consecutive_failures,
        path_loss_degrade_threshold=args.path_loss_degrade_threshold,
        path_p95_degrade_ms=args.path_p95_degrade_ms,
        path_avg_degrade_ms=args.path_avg_degrade_ms,
        path_health_window=args.path_health_window,
        send_mode=args.send_mode,
    )
    print(
        f"D3QN bench complete: sent={summary['total']['sent']} "
        f"success={summary['total']['success']} loss={_fmt_rate(summary['total']['loss_rate'])} "
        f"route_failures={summary.get('d3qn_route_failures')}"
    )
    print(f"report: {Path(summary['log_dir']) / '测试结果汇报.md'}")
    return 0


def cmd_bench10(args: argparse.Namespace) -> int:
    summary = run_batch_benchmark(
        repeats=args.repeats,
        output_dir=args.output_dir,
        latency_target_ms=args.latency_target_ms,
        loss_min=args.loss_min,
        loss_max=args.loss_max,
        port=args.port,
        baud=args.baud,
        nodes=parse_node_list(args.nodes),
        rounds=args.rounds,
        payload=args.payload,
        log_dir=args.log_dir,
        checkpoint=args.checkpoint,
        boot_wait=args.boot_wait,
        rssi_requests=args.rssi_requests,
        ack_timeout=args.ack_timeout,
        interval=args.interval,
        gateway=parse_addr(args.gateway),
    )
    print(
        f"10-run D3QN bench complete: sent={summary['total']['sent']} "
        f"success={summary['total']['success']} loss={_fmt_rate(summary['total']['loss_rate'])} "
        f"avg_latency={_fmt_ms(summary['total']['latency']['avg_ms'])}"
    )
    print(f"report: {Path(args.output_dir) / '10次测试总汇报.md'}")
    return 0


def cmd_predict_path(args: argparse.Namespace) -> int:
    topology = load_topology(args.state)
    predictor = D3QNPredictor(args.checkpoint)
    demand = args.demand if args.demand is not None else demand_for_payload(args.payload)
    decision = predictor.decide(topology, parse_addr(args.src), parse_addr(args.dst), demand)
    print(json.dumps(decision.to_dict(), ensure_ascii=False, indent=2, sort_keys=True))
    if not decision.success:
        return 2
    print(f"path={_fmt_path(decision.selected_path)} action={decision.selected_action}")
    return 0


def cmd_export_state(args: argparse.Namespace) -> int:
    topology = load_topology(args.state)
    write_d3qn_state(args.output, topology)
    print(f"exported {args.output}")
    return 0


def cmd_checkpoint_info(args: argparse.Namespace) -> int:
    print(json.dumps(checkpoint_metadata(args.checkpoint), ensure_ascii=False, indent=2, sort_keys=True))
    return 0


def cmd_build_rssi_profile(args: argparse.Namespace) -> int:
    profile = write_profile_from_run(args.run_dir, args.output)
    print(f"profile: {args.output}")
    print(f"nodes={len(profile['nodes'])} edges={len(profile['edges'])}")
    return 0


def _add_training_shape_args(parser: argparse.ArgumentParser, *, fast: bool = False) -> None:
    parser.add_argument("--graph-lazy-paths", action="store_true")
    parser.add_argument("--graph-require-full-k-paths", action="store_true")
    parser.add_argument("--graph-num-nodes", type=int, default=11 if fast else None)
    parser.add_argument("--graph-target-edges", type=int, default=24 if fast else None)
    parser.add_argument("--hardware-rssi-profile", default=None)
    parser.add_argument("--reward-loss-weight", type=float, default=8.0)
    parser.add_argument("--reward-delay-weight", type=float, default=1.6)
    parser.add_argument("--reward-utilization-weight", type=float, default=0.08)
    parser.add_argument("--reward-hop-weight", type=float, default=0.18)
    parser.add_argument("--reward-weak-rssi-weight", type=float, default=2.4)
    parser.add_argument("--reward-overload-weight", type=float, default=8.0)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Independent D3QN_MPNN PC CLI for mesh serial tests")
    subparsers = parser.add_subparsers(dest="command", required=True)

    setup = subparsers.add_parser("setup-env", help="create .venv-d3qn and install D3QN dependencies")
    setup.add_argument("--venv", default=None)
    setup.set_defaults(func=cmd_setup_env)

    train = subparsers.add_parser("train", help="train sample D3QN_MPNN and save checkpoints")
    train.add_argument("--iterations", type=int, default=2000)
    train.add_argument("--training-episodes", type=int, default=20)
    train.add_argument("--evaluation-episodes", type=int, default=20)
    train.add_argument("--first-work-train-episode", type=int, default=60)
    train.add_argument("--output-dir", default=str(LATEST_CHECKPOINT.parent))
    train.add_argument("--seed", type=int, default=37)
    train.add_argument("--evaluation-interval", type=int, default=20)
    train.add_argument("--save-interval", type=int, default=20)
    train.add_argument("--resume", default=None, help="Path to checkpoint to resume training from")
    _add_training_shape_args(train)
    train.set_defaults(func=cmd_train)

    fast = subparsers.add_parser("train-fast", help="fast D3QN training preset for hardware validation")
    fast.add_argument("--iterations", type=int, default=40)
    fast.add_argument("--training-episodes", type=int, default=3)
    fast.add_argument("--evaluation-episodes", type=int, default=1)
    fast.add_argument("--first-work-train-episode", type=int, default=5)
    fast.add_argument("--output-dir", default=str(LATEST_CHECKPOINT.parent))
    fast.add_argument("--seed", type=int, default=37)
    fast.add_argument("--evaluation-interval", type=int, default=10)
    fast.add_argument("--save-interval", type=int, default=10)
    fast.add_argument("--resume", default=None, help="Path to checkpoint to resume training from")
    _add_training_shape_args(fast, fast=True)
    fast.set_defaults(func=cmd_train_fast)

    smoke_train = subparsers.add_parser("train-smoke", help="minimal D3QN training preset to verify checkpoint writing")
    smoke_train.add_argument("--iterations", type=int, default=2)
    smoke_train.add_argument("--training-episodes", type=int, default=1)
    smoke_train.add_argument("--evaluation-episodes", type=int, default=1)
    smoke_train.add_argument("--first-work-train-episode", type=int, default=1)
    smoke_train.add_argument("--output-dir", default=str(LATEST_CHECKPOINT.parent))
    smoke_train.add_argument("--seed", type=int, default=37)
    smoke_train.add_argument("--evaluation-interval", type=int, default=1)
    smoke_train.add_argument("--save-interval", type=int, default=1)
    _add_training_shape_args(smoke_train, fast=True)
    smoke_train.set_defaults(func=cmd_train_fast)

    profile = subparsers.add_parser("build-rssi-profile", help="build hardware RSSI training profile from a D3QN run")
    profile.add_argument("--run-dir", required=True)
    profile.add_argument("--output", required=True)
    profile.set_defaults(func=cmd_build_rssi_profile)

    bench = subparsers.add_parser("bench", help="run D3QN hardware benchmark")
    bench.add_argument("--port", required=True)
    bench.add_argument("--baud", type=int, default=115200)
    bench.add_argument("--nodes", default="1,2,3,4,5,6,7,8,9,10")
    bench.add_argument("--rounds", type=int, default=20)
    bench.add_argument("--payload", default="AABBCC")
    bench.add_argument("--log-dir", default=str(DEFAULT_LOG_ROOT))
    bench.add_argument("--checkpoint", default=str(LATEST_CHECKPOINT))
    bench.add_argument("--gateway", default="00")
    bench.add_argument("--dongle-addr", default=None, help="dongle address to exclude from routing (decimal: 16 or hex: 0x10)")
    bench.add_argument("--boot-wait", type=float, default=5.0)
    bench.add_argument("--rssi-requests", type=int, default=5)
    bench.add_argument("--ack-timeout", type=float, default=2.0)
    bench.add_argument("--interval", type=float, default=0.5)
    bench.add_argument("--sources", default=None, help="comma-separated source nodes; default uses all --nodes")
    bench.add_argument("--recollect-consecutive-failures", type=int, default=3)
    bench.add_argument("--path-loss-degrade-threshold", type=float, default=0.10)
    bench.add_argument("--path-p95-degrade-ms", type=float, default=700.0)
    bench.add_argument("--path-avg-degrade-ms", type=float, default=220.0)
    bench.add_argument("--path-health-window", type=int, default=5)
    bench.add_argument("--send-mode", default="single_send", choices=["single_send", "two_send"], help="single_send: gateway->target direct, two_send: gateway->source->target")
    bench.set_defaults(func=cmd_bench)

    bench10 = subparsers.add_parser("bench10", help="run repeated D3QN hardware benchmark and aggregate results")
    bench10.add_argument("--port", required=True)
    bench10.add_argument("--baud", type=int, default=115200)
    bench10.add_argument("--nodes", default="1,2,3,4,5,6,7,8,9,10")
    bench10.add_argument("--rounds", type=int, default=20)
    bench10.add_argument("--payload", default="AABBCC")
    bench10.add_argument("--log-dir", default=str(DEFAULT_LOG_ROOT))
    bench10.add_argument("--checkpoint", default=str(LATEST_CHECKPOINT))
    bench10.add_argument("--gateway", default="00")
    bench10.add_argument("--boot-wait", type=float, default=5.0)
    bench10.add_argument("--rssi-requests", type=int, default=5)
    bench10.add_argument("--ack-timeout", type=float, default=2.0)
    bench10.add_argument("--interval", type=float, default=0.5)
    bench10.add_argument("--repeats", type=int, default=10)
    bench10.add_argument("--output-dir", default=str(Path(__file__).resolve().parents[1] / "logs" / "d3qn_hw_10run"))
    bench10.add_argument("--latency-target-ms", type=float, default=150.0)
    bench10.add_argument("--loss-min", type=float, default=0.06)
    bench10.add_argument("--loss-max", type=float, default=0.08)
    bench10.set_defaults(func=cmd_bench10)

    predict = subparsers.add_parser("predict-path", help="select a path from saved topology using D3QN")
    predict.add_argument("--state", default=str(DEFAULT_STATE))
    predict.add_argument("--checkpoint", default=str(LATEST_CHECKPOINT))
    predict.add_argument("--src", default="00")
    predict.add_argument("--dst", required=True)
    predict.add_argument("--payload", default="AABBCC")
    predict.add_argument("--demand", type=int, default=None)
    predict.set_defaults(func=cmd_predict_path)

    export = subparsers.add_parser("export-d3qn-state", help="export D3QN runtime state from saved topology")
    export.add_argument("--state", default=str(DEFAULT_STATE))
    export.add_argument("--output", required=True)
    export.set_defaults(func=cmd_export_state)

    info = subparsers.add_parser("checkpoint-info", help="print checkpoint metadata")
    info.add_argument("--checkpoint", default=str(LATEST_CHECKPOINT))
    info.set_defaults(func=cmd_checkpoint_info)

    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        return args.func(args)
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
