from __future__ import annotations

import argparse
import gc
import json
import os
import random
import sys
from pathlib import Path

import numpy as np

from .defaults import DEFAULTS
from .model import DEFAULT_HPARAMS, SAMPLE_DIR


def _prepare_sample_imports():
    sample_root = SAMPLE_DIR.parent
    for path in (str(SAMPLE_DIR), str(sample_root)):
        if path not in sys.path:
            sys.path.insert(0, path)


def _save_checkpoint(path: Path, agent, env_training, training_config: dict, eval_reward: float | None, iteration: int) -> None:
    import torch

    payload = {
        "schema": "pc_d3qn_cli.checkpoint.v1",
        "algorithm": "D3QN_MPNN",
        "model_state_dict": agent.primary_network.state_dict(),
        "target_model_state_dict": agent.target_network.state_dict(),
        "optimizer_state_dict": agent.optimizer.state_dict(),
        "hparams": dict(DEFAULT_HPARAMS),
        "num_actions": int(agent.num_actions),
        "route_feature_dim": int(agent.route_feature_dim),
        "k_paths": int(agent.K),
        "num_nodes": int(env_training.numNodes),
        "num_edges": int(env_training.numEdges),
        "training_iteration": int(iteration),
        "eval_reward": eval_reward,
        "training_config": training_config,
        "field_sources": {
            "training_topology": "sample_simulation",
            "hardware_rssi_profile": training_config.get("hardware_rssi_profile"),
            "runtime_rssi": "real_rssi",
            "runtime_ack": "real_ack",
            "unmeasured_runtime_fields": "default",
        },
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    torch.save(payload, path)


def _evaluate(agent, env_eval, episodes: int) -> float:
    if episodes <= 0:
        return float("-inf")
    rewards = []
    for _ in range(episodes):
        state, demand, source, destination = env_eval.reset()
        total = 0.0
        while True:
            action, _ = agent.act(env_eval, state, demand, source, destination, True)
            state, reward, done, demand, source, destination, _info = env_eval.make_step(
                state, action, demand, source, destination
            )
            total += float(reward)
            if done:
                break
        rewards.append(total)
    return float(np.mean(rewards)) if rewards else float("-inf")


def run_training(args: argparse.Namespace) -> int:
    _prepare_sample_imports()
    import gym
    import gym_environments  # noqa: F401
    import torch
    from d3qn_mpnn_torch import D3QNMPNNAgent, hparams, listofDemands

    os.environ["PYTHONHASHSEED"] = str(args.seed)
    np.random.seed(args.seed)
    random.seed(args.seed)
    torch.manual_seed(1)
    if torch.cuda.is_available():
        torch.cuda.manual_seed_all(1)
        torch.backends.cudnn.deterministic = True
        torch.backends.cudnn.benchmark = False

    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)
    training_config = {
        "iterations": args.iterations,
        "training_episodes": args.training_episodes,
        "evaluation_episodes": args.evaluation_episodes,
        "first_work_train_episode": args.first_work_train_episode,
        "seed": args.seed,
        "env_name": "GraphEnv-v1",
        "graph_topology": args.graph_topology,
        "graph_num_nodes": os.environ.get("GRAPH_NUM_NODES"),
        "graph_target_edges": os.environ.get("GRAPH_TARGET_EDGES"),
        "hardware_rssi_profile": os.environ.get("GRAPH_HARDWARE_RSSI_PROFILE"),
        "reward_profile": {
            "loss_weight": os.environ.get("D3QN_REWARD_LOSS_WEIGHT", "8.0"),
            "delay_weight": os.environ.get("D3QN_REWARD_DELAY_WEIGHT", "1.6"),
            "utilization_weight": os.environ.get("D3QN_REWARD_UTILIZATION_WEIGHT", "0.08"),
            "hop_weight": os.environ.get("D3QN_REWARD_HOP_WEIGHT", "0.18"),
            "weak_rssi_weight": os.environ.get("D3QN_REWARD_WEAK_RSSI_WEIGHT", "2.4"),
            "overload_weight": os.environ.get("D3QN_REWARD_OVERLOAD_WEIGHT", "8.0"),
        },
        "listofDemands": list(listofDemands),
        "hparams": dict(hparams),
        "sample_dir": str(SAMPLE_DIR),
    }
    (output_dir / "training_config.json").write_text(
        json.dumps(training_config, ensure_ascii=False, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )

    env_training = gym.make("GraphEnv-v1", disable_env_checker=True)
    env_training.seed(args.seed)
    env_training.generate_environment(args.graph_topology, listofDemands)
    env_eval = gym.make("GraphEnv-v1", disable_env_checker=True)
    env_eval.seed(args.seed)
    env_eval.generate_environment(args.graph_topology, listofDemands)

    agent = D3QNMPNNAgent(env_training, hparams["batch_size"])
    best_reward = float("-inf")
    start_iteration = 0

    # Auto-resume from latest checkpoint if it exists and has remaining iterations
    latest_path = output_dir / "latest.pt"
    if latest_path.exists():
        try:
            checkpoint = torch.load(latest_path, map_location="cpu")
            prev_iteration = checkpoint.get("training_iteration", 0)
            if prev_iteration < args.iterations - 1:
                agent.primary_network.load_state_dict(checkpoint["model_state_dict"])
                agent.target_network.load_state_dict(checkpoint["target_model_state_dict"])
                agent.optimizer.load_state_dict(checkpoint["optimizer_state_dict"])
                start_iteration = prev_iteration + 1
                best_reward = checkpoint.get("eval_reward") or float("-inf")
                for _ in range(start_iteration):
                    if _ > 30 and agent.epsilon > agent.epsilon_min:
                        agent.epsilon *= agent.epsilon_decay
                print(f"Auto-resume from iteration {start_iteration}, epsilon={agent.epsilon:.4f}")
            else:
                print(f"Checkpoint already at iteration {prev_iteration}, training complete")
                return 0
        except Exception as e:
            print(f"Failed to load checkpoint, starting fresh: {e}")

    if getattr(args, "resume", None):
        print(f"Resuming from checkpoint: {args.resume}")
        checkpoint = torch.load(args.resume, map_location="cpu")
        agent.primary_network.load_state_dict(checkpoint["model_state_dict"])
        agent.target_network.load_state_dict(checkpoint["target_model_state_dict"])
        agent.optimizer.load_state_dict(checkpoint["optimizer_state_dict"])
        start_iteration = checkpoint.get("training_iteration", 0) + 1
        best_reward = checkpoint.get("eval_reward") or float("-inf")
        # Fast-forward epsilon
        for _ in range(start_iteration):
            if _ > 30 and agent.epsilon > agent.epsilon_min:
                agent.epsilon *= agent.epsilon_decay
        print(f"Resumed at iteration {start_iteration}, epsilon={agent.epsilon:.4f}")

    for iteration in range(start_iteration, args.iterations):
        agent.set_training_iteration(iteration)
        train_episodes = args.first_work_train_episode if iteration == 0 else args.training_episodes
        for _ in range(train_episodes):
            state, demand, source, destination = env_training.reset()
            while True:
                action, state_action = agent.act(env_training, state, demand, source, destination, False)
                new_state, reward, done, new_demand, new_source, new_destination, info = env_training.make_step(
                    state,
                    action,
                    demand,
                    source,
                    destination,
                )
                agent.add_sample(
                    state_action,
                    action,
                    agent.compute_training_reward(info),
                    done,
                    new_state,
                    new_demand,
                    new_source,
                    new_destination,
                )
                state = new_state
                demand = new_demand
                source = new_source
                destination = new_destination
                if done:
                    break
        agent.replay(iteration)
        if iteration > 30 and agent.epsilon > agent.epsilon_min:
            agent.epsilon *= agent.epsilon_decay

        eval_reward = None
        if args.evaluation_episodes > 0 and (iteration % args.evaluation_interval == 0 or iteration == args.iterations - 1):
            eval_reward = _evaluate(agent, env_eval, args.evaluation_episodes)
            if eval_reward > best_reward:
                best_reward = eval_reward
                _save_checkpoint(output_dir / "best.pt", agent, env_training, training_config, eval_reward, iteration)
        if iteration % args.save_interval == 0 or iteration == args.iterations - 1:
            _save_checkpoint(output_dir / "latest.pt", agent, env_training, training_config, eval_reward, iteration)
        if iteration % 5 == 0:
            print(f"iteration={iteration} epsilon={agent.epsilon:.4f} eval_reward={eval_reward}")
        gc.collect()

    if not (output_dir / "best.pt").exists():
        _save_checkpoint(output_dir / "best.pt", agent, env_training, training_config, None, args.iterations - 1)
    agent.writer.flush()
    agent.writer.close()
    print(f"training complete: latest={output_dir / 'latest.pt'} best={output_dir / 'best.pt'}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Train D3QN_MPNN and save checkpoints for pc_d3qn_cli")
    parser.add_argument("--iterations", type=int, default=DEFAULTS.training_iterations)
    parser.add_argument("--training-episodes", type=int, default=DEFAULTS.training_episodes)
    parser.add_argument("--evaluation-episodes", type=int, default=DEFAULTS.evaluation_episodes)
    parser.add_argument("--first-work-train-episode", type=int, default=DEFAULTS.first_work_train_episode)
    parser.add_argument("--evaluation-interval", type=int, default=20)
    parser.add_argument("--save-interval", type=int, default=20)
    parser.add_argument("--resume", default=None, help="Path to checkpoint to resume training from")
    parser.add_argument("--output-dir", default=str(Path(__file__).resolve().parents[1] / "checkpoints" / "d3qn_mpnn"))
    parser.add_argument("--seed", type=int, default=37)
    parser.add_argument("--graph-topology", type=int, default=0)
    return parser


def main(argv: list[str] | None = None) -> int:
    return run_training(build_parser().parse_args(argv))


if __name__ == "__main__":
    raise SystemExit(main())
