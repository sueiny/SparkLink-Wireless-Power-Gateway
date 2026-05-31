import multiprocessing
import time as tt
import glob
from tqdm import tqdm
from collections import deque
import torch
import numpy as np
import random
import os
import gc
from torch.utils.tensorboard import SummaryWriter
import heapq
from pathlib import Path

# 基本配置
differentiation_str = os.environ.get("DIJKSTRA_RUN_NAME", "Dijkstra")
project_root = Path(__file__).resolve().parents[1]
artifacts_root = project_root / "artifacts"
checkpoint_dir = artifacts_root / "checkpoints" / differentiation_str
train_dir = artifacts_root / "tensorboard" / differentiation_str
legacy_logs_dir = project_root / "Dijkstra_pytorch" / "Logs"
legacy_logs_dir.mkdir(parents=True, exist_ok=True)
checkpoint_dir.mkdir(parents=True, exist_ok=True)
train_dir.mkdir(parents=True, exist_ok=True)
summary_writer = SummaryWriter(log_dir=str(train_dir))
device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
listofDemands = [8, 32, 64]
evaluation_interval = 20
if not os.path.exists(checkpoint_dir):
    os.makedirs(checkpoint_dir)
checkpoint_prefix = os.path.join(checkpoint_dir, 'checkpoint.pth')

# 超参数
hparams = {
    'state_dim': 64,
    'num_demands': len(listofDemands),
}

class DijkstraAgent:
    def __init__(self, env_training):
        self.env = env_training
        self.K = 4
        self.state_dim = hparams['state_dim']
        self.writer = summary_writer

    def flatten_state(self, state, demand, source, destination):
        capacities = state[:, 0] / 200.0
        bw_allocated = state[:, 1] / max(listofDemands)
        flat_state = np.concatenate([capacities, bw_allocated])

        demand_idx = listofDemands.index(demand) if demand in listofDemands else 0
        demand_onehot = np.zeros(len(listofDemands))
        demand_onehot[demand_idx] = 1
        flat_state = np.concatenate([flat_state, demand_onehot])

        src_onehot = np.zeros(10)
        dst_onehot = np.zeros(10)
        src_onehot[source % 10] = 1
        dst_onehot[destination % 10] = 1
        flat_state = np.concatenate([flat_state, src_onehot, dst_onehot])

        if len(flat_state) < self.state_dim:
            flat_state = np.pad(flat_state, (0, self.state_dim - len(flat_state)), mode='constant')
        else:
            flat_state = flat_state[:self.state_dim]

        return torch.FloatTensor(flat_state).to(device)

    def build_graph(self, state):
        graph = {}
        max_edge_idx = state.shape[0]
        for edge_idx, edge in enumerate(self.env.ordered_edges):
            if edge_idx >= max_edge_idx:
                break
            u, v = edge
            if u not in graph:
                graph[u] = []
            if v not in graph:
                graph[v] = []
            graph[u].append((v, edge_idx))
            graph[v].append((u, edge_idx))
        return graph

    def dijkstra(self, graph, start, end, state):
        distances = {node: float('inf') for node in range(self.env.numNodes)}
        distances[start] = 0
        predecessors = {node: None for node in range(self.env.numNodes)}
        pq = [(0, start)]
        visited = set()

        while pq:
            current_distance, current_node = heapq.heappop(pq)

            if current_node in visited:
                continue

            visited.add(current_node)

            if current_node == end:
                break

            neighbors = graph.get(current_node, [])
            for neighbor, edge_idx in neighbors:
                if edge_idx >= state.shape[0]:
                    print(f"Warning: edge_idx {edge_idx} out of bounds for state with shape {state.shape}")
                    weight = 1.0
                else:
                    capacity = state[edge_idx, 0]
                    allocated = state[edge_idx, 1]
                    remaining_capacity = max(0, capacity - allocated)
                    weight = 1 / max(remaining_capacity, 1e-6)

                distance = current_distance + weight

                if distance < distances[neighbor]:
                    distances[neighbor] = distance
                    predecessors[neighbor] = current_node
                    heapq.heappush(pq, (distance, neighbor))

        if distances[end] == float('inf'):
            return None, float('inf')

        path = []
        current_node = end
        while current_node is not None:
            path.append(current_node)
            current_node = predecessors[current_node]
        path.reverse()
        return path, distances[end]

    def act(self, env, state, demand, source, destination, flagEvaluation):


        flat_state = self.flatten_state(state, demand, source, destination)

        pathList = env.allPaths[str(source) + ':' + str(destination)]
        num_paths = min(len(pathList), self.K)

        if num_paths == 0:
            return 0, flat_state

        graph = self.build_graph(state)

        dijkstra_path, dijkstra_weight = self.dijkstra(graph, source, destination, state)
        if dijkstra_path is None:
            return 0, flat_state

        best_path_idx = 0
        best_path_weight = float('inf')
        best_path = None

        for idx, path in enumerate(pathList[:num_paths]):
            if path == dijkstra_path:
                best_path_idx = idx
                best_path_weight = dijkstra_weight
                best_path = path
                break
            else:
                path_weight = 0
                for i in range(len(path) - 1):
                    edge_key = str(path[i]) + ':' + str(path[i + 1])
                    if edge_key in env.edgesDict:
                        edge_idx = env.edgesDict[edge_key]
                        if edge_idx >= state.shape[0]:
                            print(f"Warning: edge_idx {edge_idx} out of bounds for state with shape {state.shape}")
                            path_weight += 1.0
                        else:
                            capacity = state[edge_idx, 0]
                            allocated = state[edge_idx, 1]
                            remaining_capacity = max(0, capacity - allocated)
                            path_weight += 1 / max(remaining_capacity, 1e-6)
                if path_weight < best_path_weight:
                    best_path_weight = path_weight
                    best_path_idx = idx
                    best_path = path

        return best_path_idx, flat_state

    def add_sample(self, env_training, state_action, action, reward, done, new_state, new_demand, new_source, new_destination):
        pass

    def replay(self, episode):
        pass
