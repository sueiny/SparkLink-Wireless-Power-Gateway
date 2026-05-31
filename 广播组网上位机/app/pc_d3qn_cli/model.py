from __future__ import annotations

import json
import os
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

from .defaults import DEFAULTS
from .protocol import format_addr
from .state import build_d3qn_state
from .topology import Topology, edge_key


SAMPLE_DIR = Path(__file__).resolve().parents[2] / "sample" / "D3QN_MPNN_TimePER"
CHECKPOINT_DIR = Path(__file__).resolve().parents[1] / "checkpoints" / "d3qn_mpnn"
LATEST_CHECKPOINT = CHECKPOINT_DIR / "latest.pt"

DEFAULT_HPARAMS = {
    "l2": 1e-5,
    "dropout_rate": 0.02,
    "learning_rate": 1.6e-4,
    "batch_size": 32,
    "hidden_dim": 112,
    "readout_units": 128,
    "T": 3,
    "gamma": 0.15,
    "edge_feature_dim": 3,
    "num_demands": len(DEFAULTS.demands),
}


class D3QNUnavailable(RuntimeError):
    pass


@dataclass
class D3QNDecision:
    status: str
    src: int
    dst: int
    demand: int
    selected_action: int | None
    selected_path: list[int]
    candidate_paths: list[list[int]]
    q_values: list[float]
    checkpoint: str | None
    error: str | None = None
    node_mapping: dict[int, int] | None = None

    @property
    def success(self) -> bool:
        return self.status == "valid"

    def to_dict(self) -> dict:
        return {
            "status": self.status,
            "src": self.src,
            "dst": self.dst,
            "source": format_addr(self.src),
            "destination": format_addr(self.dst),
            "demand": self.demand,
            "selected_action": self.selected_action,
            "selected_path": self.selected_path,
            "candidate_paths": self.candidate_paths,
            "q_values": self.q_values,
            "checkpoint": self.checkpoint,
            "error": self.error,
            "node_mapping": {f"{key:02X}": value for key, value in sorted((self.node_mapping or {}).items())},
        }


def demand_for_payload(payload: str, demands: tuple[int, ...] = DEFAULTS.demands) -> int:
    payload_bytes = max(1, len("".join(payload.split())) // 2)
    for demand in sorted(demands):
        if payload_bytes <= demand:
            return int(demand)
    return int(max(demands))


def setup_env(venv: str | Path | None = None) -> int:
    app_root = Path(__file__).resolve().parents[1]
    venv_path = Path(venv) if venv else app_root / ".venv-d3qn"
    commands = [
        [sys.executable, "-m", "venv", str(venv_path)],
        [str(venv_path / "bin" / "python"), "-m", "pip", "install", "--upgrade", "pip"],
        [
            str(venv_path / "bin" / "python"),
            "-m",
            "pip",
            "install",
            "torch",
            "gym==0.25.2",
            "numpy",
            "networkx",
            "tqdm",
            "tensorboard",
            "openpyxl",
            "pyserial",
            "matplotlib",
        ],
    ]
    if sys.platform.startswith("win"):
        commands[1][0] = str(venv_path / "Scripts" / "python.exe")
        commands[2][0] = str(venv_path / "Scripts" / "python.exe")
    for command in commands:
        subprocess.run(command, check=True)
    return 0


def run_sample_training(
    iterations: int = DEFAULTS.training_iterations,
    training_episodes: int = DEFAULTS.training_episodes,
    evaluation_episodes: int = DEFAULTS.evaluation_episodes,
    first_work_train_episode: int = DEFAULTS.first_work_train_episode,
    output_dir: str | Path = CHECKPOINT_DIR,
    seed: int = 37,
    evaluation_interval: int = 20,
    save_interval: int = 20,
    graph_lazy_paths: bool = False,
    graph_require_full_k_paths: bool = False,
    graph_num_nodes: int | None = None,
    graph_target_edges: int | None = None,
    hardware_rssi_profile: str | Path | None = None,
    reward_profile: dict | None = None,
    resume: str | Path | None = None,
) -> Path:
    output_path = Path(output_dir)
    output_path.mkdir(parents=True, exist_ok=True)
    command = [
        sys.executable,
        "-m",
        "pc_d3qn_cli.train_runner",
        "--iterations",
        str(iterations),
        "--training-episodes",
        str(training_episodes),
        "--evaluation-episodes",
        str(evaluation_episodes),
        "--first-work-train-episode",
        str(first_work_train_episode),
        "--output-dir",
        str(output_path),
        "--seed",
        str(seed),
        "--evaluation-interval",
        str(evaluation_interval),
        "--save-interval",
        str(save_interval),
    ]
    if resume:
        command.extend(["--resume", str(resume)])
    env = dict(os.environ)
    if graph_lazy_paths:
        env["GRAPH_LAZY_PATHS"] = "1"
    if graph_require_full_k_paths:
        env["GRAPH_REQUIRE_FULL_K_PATHS"] = "1"
    if graph_num_nodes is not None:
        env["GRAPH_NUM_NODES"] = str(graph_num_nodes)
    if graph_target_edges is not None:
        env["GRAPH_TARGET_EDGES"] = str(graph_target_edges)
    if hardware_rssi_profile is not None:
        env["GRAPH_HARDWARE_RSSI_PROFILE"] = str(hardware_rssi_profile)
        env["GRAPH_NUM_NODES"] = "11"
    for key, value in (reward_profile or {}).items():
        env[key] = str(value)
    subprocess.run(command, check=True, env=env)
    return output_path / "latest.pt"


class D3QNPredictor:
    def __init__(self, checkpoint: str | Path = LATEST_CHECKPOINT):
        self.checkpoint_path = Path(checkpoint)
        self._torch = None
        self._network = None
        self._checkpoint_data = None
        self._device = None
        self._loaded_route_feature_dim = None
        self._loaded_num_actions = None

    def _load(self, route_feature_dim: int, num_actions: int):
        if self._network is not None:
            if self._loaded_route_feature_dim != route_feature_dim:
                raise D3QNUnavailable(f"route_feature_dim mismatch: loaded={self._loaded_route_feature_dim}, runtime={route_feature_dim}")
            if self._loaded_num_actions != num_actions:
                raise D3QNUnavailable(f"num_actions mismatch: loaded={self._loaded_num_actions}, runtime={num_actions}")
            return
        if not self.checkpoint_path.exists():
            raise D3QNUnavailable(f"checkpoint not found: {self.checkpoint_path}")
        try:
            import torch
        except ImportError as exc:
            raise D3QNUnavailable("torch is required for D3QN inference; run setup_d3qn_env first") from exc
        if str(SAMPLE_DIR) not in sys.path:
            sys.path.insert(0, str(SAMPLE_DIR))
        try:
            from mpnn import DuelingMPNN
        except ImportError as exc:
            raise D3QNUnavailable(f"cannot import sample DuelingMPNN from {SAMPLE_DIR}") from exc

        self._torch = torch
        self._device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
        data = torch.load(self.checkpoint_path, map_location=self._device)
        hparams = dict(DEFAULT_HPARAMS)
        hparams.update(data.get("hparams", {}))
        requested_route_dim = int(route_feature_dim)
        checkpoint_route_dim = int(data.get("route_feature_dim", route_feature_dim))
        checkpoint_actions = int(data.get("num_actions", num_actions))
        if checkpoint_route_dim != requested_route_dim:
            checkpoint_nodes = max(1, (checkpoint_route_dim - len(DEFAULTS.demands) - 1) // 2)
            runtime_nodes = max(1, (requested_route_dim - len(DEFAULTS.demands) - 1) // 2)
            if checkpoint_nodes < runtime_nodes:
                raise D3QNUnavailable(f"route_feature_dim mismatch: checkpoint={checkpoint_route_dim}, runtime={requested_route_dim}")
        if checkpoint_actions != num_actions:
            raise D3QNUnavailable(f"num_actions mismatch: checkpoint={checkpoint_actions}, runtime={num_actions}")
        network = DuelingMPNN(hparams, checkpoint_actions, checkpoint_route_dim).to(self._device)
        state_dict = data.get("model_state_dict") or data.get("primary_network_state_dict")
        if state_dict is None:
            raise D3QNUnavailable("checkpoint missing model_state_dict")
        network.load_state_dict(state_dict)
        network.eval()
        self._network = network
        self._checkpoint_data = data
        self._loaded_route_feature_dim = checkpoint_route_dim
        self._loaded_num_actions = checkpoint_actions

    def _checkpoint_num_nodes(self, runtime_nodes: int) -> int:
        if self._checkpoint_data is None:
            return runtime_nodes
        value = self._checkpoint_data.get("num_nodes")
        if value is not None:
            return int(value)
        route_dim = self._checkpoint_data.get("route_feature_dim")
        if route_dim is not None:
            return max(1, (int(route_dim) - len(DEFAULTS.demands) - 1) // 2)
        return runtime_nodes

    def validate_topology(self, topology: Topology) -> dict:
        state = build_d3qn_state(topology)
        nodes = sorted(int(node) for node in state["nodes"])
        route_feature_dim = len(DEFAULTS.demands) + 1 + 2 * len(nodes)
        self._load(route_feature_dim=route_feature_dim, num_actions=DEFAULTS.k_paths)
        return {
            "checkpoint": str(self.checkpoint_path),
            "runtime_nodes": len(nodes),
            "runtime_route_feature_dim": route_feature_dim,
            "num_actions": DEFAULTS.k_paths,
        }

    def decide(self, topology: Topology, src: int, dst: int, demand: int) -> D3QNDecision:
        state = build_d3qn_state(topology)
        nodes = sorted(int(node) for node in state["nodes"])
        if src not in nodes or dst not in nodes:
            return D3QNDecision("unreachable", src, dst, demand, None, [], [], [], str(self.checkpoint_path), "src or dst missing from topology")
        candidates = state["candidate_paths"].get(f"{src:02X}:{dst:02X}", [])
        if not candidates:
            return D3QNDecision("unreachable", src, dst, demand, None, [], [], [], str(self.checkpoint_path), "no candidate path")
        if not state["ordered_edges"]:
            return D3QNDecision("unreachable", src, dst, demand, None, [], candidates, [], str(self.checkpoint_path), "empty edge set")

        runtime_feature_dim = len(DEFAULTS.demands) + 1 + 2 * len(nodes)
        try:
            self._load(route_feature_dim=runtime_feature_dim, num_actions=DEFAULTS.k_paths)
        except D3QNUnavailable as exc:
            return D3QNDecision("model_unavailable", src, dst, demand, None, [], candidates, [], str(self.checkpoint_path), str(exc))
        torch = self._torch
        assert torch is not None and self._network is not None and self._device is not None

        model_num_nodes = self._checkpoint_num_nodes(len(nodes))
        anchor_node = 0 if 0 in nodes else src
        node_to_index = self._build_node_mapping(nodes, anchor_node, model_num_nodes)
        if src not in node_to_index or dst not in node_to_index:
            return D3QNDecision(
                "model_unavailable",
                src,
                dst,
                demand,
                None,
                [],
                candidates,
                [],
                str(self.checkpoint_path),
                f"node cannot be mapped into checkpoint model space: src={src:02X} dst={dst:02X}",
                node_mapping=node_to_index,
            )
        edge_features = self._edge_feature_array(state)
        route_features = self._route_feature_array(demand, node_to_index[src], node_to_index[dst], model_num_nodes)
        first, second = self._first_second_indices(state["ordered_edges"])
        try:
            candidate_edge_indices = self._candidate_edge_indices(candidates, state["edgesDict"])
        except D3QNUnavailable as exc:
            return D3QNDecision("model_unavailable", src, dst, demand, None, [], candidates, [], str(self.checkpoint_path), str(exc), node_mapping=node_to_index)
        import numpy as np

        with torch.no_grad():
            q_values_tensor = self._network(
                torch.as_tensor(edge_features, dtype=torch.float32, device=self._device).unsqueeze(0),
                torch.as_tensor(first, dtype=torch.long, device=self._device),
                torch.as_tensor(second, dtype=torch.long, device=self._device),
                torch.as_tensor(route_features, dtype=torch.float32, device=self._device).unsqueeze(0),
                [[torch.as_tensor(path, dtype=torch.long, device=self._device) for path in candidate_edge_indices]],
            )[0]
        valid_count = min(len(candidates), DEFAULTS.k_paths)
        q_values = [float(value) for value in q_values_tensor[:valid_count].detach().cpu().tolist()]
        if not q_values:
            return D3QNDecision("unreachable", src, dst, demand, None, [], candidates, [], str(self.checkpoint_path), "no valid q values", node_mapping=node_to_index)
        selected_action = int(np.argmax(np.asarray(q_values, dtype=np.float32)))
        return D3QNDecision("valid", src, dst, demand, selected_action, candidates[selected_action], candidates, q_values, str(self.checkpoint_path), node_mapping=node_to_index)

    @staticmethod
    def _build_node_mapping(nodes: list[int], gateway: int, model_num_nodes: int) -> dict[int, int]:
        mapping: dict[int, int] = {gateway: 0}
        for node in sorted(nodes):
            if node == gateway:
                continue
            next_index = len(mapping)
            if next_index >= model_num_nodes:
                break
            mapping[node] = next_index
        return mapping

    @staticmethod
    def _edge_feature_array(state: dict):
        import numpy as np

        values = []
        for feature in state["edge_features"]:
            capacity = max(float(feature["capacity"]["value"]), 1.0)
            remaining = max(float(feature["remaining_capacity"]["value"]), 0.0)
            initial = max(capacity, 1.0)
            values.append([
                remaining / 200.0,
                min(1.5, remaining / initial),
                float(feature["betweenness"]["value"]),
            ])
        return np.asarray(values, dtype=np.float32)

    @staticmethod
    def _route_feature_array(demand: int, source_index: int, destination_index: int, num_nodes: int):
        import numpy as np

        demand_onehot = np.zeros(len(DEFAULTS.demands), dtype=np.float32)
        demand_index = list(DEFAULTS.demands).index(demand) if demand in DEFAULTS.demands else 0
        demand_onehot[demand_index] = 1.0
        source_onehot = np.zeros(num_nodes, dtype=np.float32)
        destination_onehot = np.zeros(num_nodes, dtype=np.float32)
        source_onehot[source_index] = 1.0
        destination_onehot[destination_index] = 1.0
        demand_scalar = np.asarray([float(demand) / float(max(DEFAULTS.demands))], dtype=np.float32)
        return np.concatenate([demand_onehot, demand_scalar, source_onehot, destination_onehot], axis=0)

    @staticmethod
    def _first_second_indices(ordered_edges: list[list[int]]) -> tuple[list[int], list[int]]:
        first: list[int] = []
        second: list[int] = []
        edge_tuples = [tuple(edge) for edge in ordered_edges]
        for index, (src, dst) in enumerate(edge_tuples):
            for other_index, (other_src, other_dst) in enumerate(edge_tuples):
                if index == other_index:
                    continue
                if src in {other_src, other_dst} or dst in {other_src, other_dst}:
                    first.append(index)
                    second.append(other_index)
        return first, second

    @staticmethod
    def _candidate_edge_indices(candidates: list[list[int]], edges_dict: dict[str, int]) -> list[list[int]]:
        indexed_paths = []
        for path in candidates[: DEFAULTS.k_paths]:
            edge_indices = []
            for src, dst in zip(path[:-1], path[1:]):
                key = edge_key(src, dst)
                if key not in edges_dict:
                    raise D3QNUnavailable(f"candidate path contains unknown edge {key}")
                edge_indices.append(int(edges_dict[key]))
            indexed_paths.append(edge_indices)
        return indexed_paths


def checkpoint_metadata(path: str | Path) -> dict:
    checkpoint = Path(path)
    if not checkpoint.exists():
        return {"exists": False, "path": str(checkpoint)}
    try:
        import torch
    except ImportError:
        return {"exists": True, "path": str(checkpoint), "torch_available": False}
    data = torch.load(checkpoint, map_location="cpu")
    return {
        "exists": True,
        "path": str(checkpoint),
        "torch_available": True,
        "hparams": data.get("hparams", {}),
        "num_nodes": data.get("num_nodes"),
        "num_actions": data.get("num_actions"),
        "route_feature_dim": data.get("route_feature_dim"),
        "training_config": data.get("training_config", {}),
    }


def write_training_config(path: str | Path, config: dict) -> None:
    output = Path(path)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(config, ensure_ascii=False, indent=2, sort_keys=True) + "\n", encoding="utf-8")
