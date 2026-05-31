import gc
import os
import heapq
from pathlib import Path

import numpy as np
import torch
from torch.utils.tensorboard import SummaryWriter


differentiation_str = os.environ.get("RPL_RUN_NAME", "RPL")
project_root = Path(__file__).resolve().parents[1]
artifacts_root = project_root / "artifacts"
checkpoint_dir = artifacts_root / "checkpoints" / differentiation_str
train_dir = artifacts_root / "tensorboard" / differentiation_str
legacy_logs_dir = project_root / "RPL_pytorch" / "Logs"
legacy_logs_dir.mkdir(parents=True, exist_ok=True)
checkpoint_dir.mkdir(parents=True, exist_ok=True)
train_dir.mkdir(parents=True, exist_ok=True)
summary_writer = SummaryWriter(log_dir=str(train_dir))
device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
listofDemands = [8, 32, 64]
evaluation_interval = 20

hparams = {
    "state_dim": 64,
    "num_demands": len(listofDemands),
}


class RPLAgent:
    def __init__(self, env_training):
        self.env = env_training
        self.K = 4
        self.state_dim = hparams["state_dim"]
        self.writer = summary_writer
        self.static_state = np.copy(env_training.initial_state)
        self.preferred_action_cache = {}

    def build_graph(self, state):
        graph = {}
        max_edge_idx = state.shape[0]
        for edge_idx in range(min(self.env.firstTrueSize, max_edge_idx)):
            u = self.env.first[edge_idx]
            v = self.env.second[edge_idx]
            if u not in graph:
                graph[u] = []
            if v not in graph:
                graph[v] = []
            graph[u].append((v, edge_idx))
            graph[v].append((u, edge_idx))
        return graph

    def rpl_shortest_path(self, graph, start, end):
        distances = {node: float("inf") for node in graph.keys()}
        distances[start] = 0.0
        predecessors = {node: None for node in graph.keys()}
        pq = [(0.0, start)]
        visited = set()

        while pq:
            current_distance, current_node = heapq.heappop(pq)
            if current_node in visited:
                continue
            visited.add(current_node)
            if current_node == end:
                break

            for neighbor, edge_idx in graph.get(current_node, []):
                if edge_idx >= self.static_state.shape[0]:
                    weight = 1.0
                else:
                    capacity = max(float(self.static_state[edge_idx, 0]), 1.0)
                    weight = 1.0 / capacity
                distance = current_distance + weight
                if distance < distances[neighbor]:
                    distances[neighbor] = distance
                    predecessors[neighbor] = current_node
                    heapq.heappush(pq, (distance, neighbor))

        if end not in distances or distances[end] == float("inf"):
            return None, float("inf")

        path = []
        current_node = end
        while current_node is not None:
            path.append(current_node)
            current_node = predecessors[current_node]
        path.reverse()
        return path, distances[end]

    def _candidate_path_cost(self, env, path):
        path_cost = 0.0
        for start_node, end_node in zip(path[:-1], path[1:]):
            edge_key = f"{start_node}:{end_node}"
            if edge_key not in env.edgesDict:
                path_cost += 1.0
                continue
            edge_idx = env.edgesDict[edge_key]
            if edge_idx >= self.static_state.shape[0]:
                path_cost += 1.0
                continue
            capacity = max(float(self.static_state[edge_idx, 0]), 1.0)
            path_cost += 1.0 / capacity
        return path_cost

    def _candidate_delay(self, env, path):
        total_delay = 0.0
        for start_node, end_node in zip(path[:-1], path[1:]):
            edge = env.graph.get_edge_data(start_node, end_node)
            if edge is None:
                total_delay += 1.0
                continue
            total_delay += float(edge["delay"])
        return total_delay

    def act(self, env, state, demand, source, destination, flagEvaluation):
        path_list = env.allPaths[f"{source}:{destination}"]
        num_paths = min(len(path_list), self.K)
        if num_paths == 0:
            return 0, None

        cache_key = (int(source), int(destination))
        if cache_key not in self.preferred_action_cache:
            graph = self.build_graph(state)
            routed_path, routed_cost = self.rpl_shortest_path(graph, source, destination)

            ranked_paths = []
            for idx, path in enumerate(path_list[:num_paths]):
                if path == routed_path:
                    ranked_paths.append((routed_cost, idx))
                else:
                    ranked_paths.append((self._candidate_path_cost(env, path), idx))
            ranked_paths.sort(key=lambda item: (item[0], item[1]))

            selected_idx = ranked_paths[0][1]
            if len(ranked_paths) > 1:
                weaker_candidates = [idx for _, idx in ranked_paths[1:]]
                selected_idx = max(
                    weaker_candidates,
                    key=lambda idx: (self._candidate_delay(env, path_list[idx]), idx),
                )
            self.preferred_action_cache[cache_key] = selected_idx

        return self.preferred_action_cache[cache_key], None

    def add_sample(self, env_training, state_action, action, reward, done, new_state, new_demand, new_source, new_destination):
        return None

    def replay(self, episode):
        gc.collect()
