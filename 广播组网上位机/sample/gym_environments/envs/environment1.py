import gc
import heapq
import json
import os
import random
from itertools import islice
from pathlib import Path


def _rssi_to_weight(rssi):
    if rssi >= -55:
        return 1
    if rssi >= -65:
        return 3
    if rssi >= -75:
        return 6
    if rssi >= -85:
        return 12
    return None


def _weighted_k_shortest_paths(graph, src, dst, k):
    adj = {}
    for u, v, data in graph.edges(data=True):
        rssi = float(data.get("rssi", -85.0))
        w = _rssi_to_weight(rssi)
        if w is None:
            continue
        adj.setdefault(u, []).append((v, w))
        adj.setdefault(v, []).append((u, w))
    paths = []
    seen = set()
    queue = [(0.0, [src])]
    while queue and len(paths) < k:
        cost, path = heapq.heappop(queue)
        current = path[-1]
        if tuple(path) in seen:
            continue
        seen.add(tuple(path))
        if current == dst:
            paths.append(path)
            continue
        for neighbor, weight in adj.get(current, []):
            if neighbor not in path:
                heapq.heappush(queue, (cost + weight, path + [neighbor]))
    return paths

import gym
import matplotlib.pyplot as plt
import networkx as nx
import numpy as np
import pylab
from gym import spaces, utils


class CandidatePathCache(dict):
    def __init__(self, env, pair_keys):
        super().__init__()
        self._env = env
        self._pair_keys = tuple(pair_keys)
        self._pair_key_set = set(self._pair_keys)

    def __contains__(self, key):
        return key in self._pair_key_set

    def __len__(self):
        return len(self._pair_keys)

    def __iter__(self):
        return iter(self._pair_keys)

    def keys(self):
        return self._pair_keys

    def get(self, key, default=None):
        if key not in self._pair_key_set:
            return default
        return self[key]

    def __missing__(self, key):
        if key not in self._pair_key_set:
            raise KeyError(key)
        paths = self._env._compute_candidate_paths_for_key(key)
        super().__setitem__(key, paths)
        return paths


def create_dynamic_topology(num_nodes=10, target_edges=20):
    graph = nx.Graph()
    graph.add_nodes_from(range(num_nodes))
    target_edges = max(int(target_edges), max(0, num_nodes - 1))

    # Build a random spanning tree first so the generated topology is connected.
    available_nodes = list(range(num_nodes))
    random.shuffle(available_nodes)
    connected = [available_nodes.pop()]
    while available_nodes:
        src = random.choice(connected)
        dst = available_nodes.pop()
        graph.add_edge(src, dst)
        connected.append(dst)

    edges_added = graph.number_of_edges()
    while edges_added < target_edges:
        src = random.randint(0, num_nodes - 1)
        dst = random.randint(0, num_nodes - 1)
        if src != dst and not graph.has_edge(src, dst):
            graph.add_edge(src, dst)
            edges_added += 1
    return graph


def _hardware_node_index(addr):
    value = int(addr)
    if value == 0:
        return 0
    if value == 0x10:
        return 10
    if 1 <= value <= 9:
        return value
    return value


def _load_hardware_rssi_graph(profile_path):
    profile = json.loads(Path(profile_path).read_text(encoding="utf-8"))
    graph = nx.Graph()
    node_indices = sorted({_hardware_node_index(node) for node in profile.get("nodes", [])})
    graph.add_nodes_from(node_indices)
    if not graph.has_node(0):
        graph.add_node(0)

    edge_id = 1
    for edge in profile.get("edges", []):
        src = _hardware_node_index(edge["src"])
        dst = _hardware_node_index(edge["dst"])
        if src == dst:
            continue
        rssi = float(edge.get("rssi_median", edge.get("rssi_mean", -85.0)))
        jitter = max(1.0, float(edge.get("rssi_stddev", 0.0)))
        augmented_rssi = max(-95.0, min(-35.0, random.gauss(rssi, min(jitter, 4.0))))
        capacity = max(1.0, float(edge.get("capacity", 1.0)))
        packet_loss = min(0.7, max(0.0, float(edge.get("packet_loss", 0.3))))
        delay = min(0.5, max(0.02, float(edge.get("delay", 0.15))))
        graph.add_edge(
            src,
            dst,
            edgeId=edge_id,
            betweenness=0.0,
            numsp=0,
            rssi=augmented_rssi,
            capacity=capacity,
            bw_allocated=0.0,
            delay=delay,
            packet_loss=packet_loss,
            queueing_delay=float(edge.get("queueing_delay", 0.0)),
            source="hardware_rssi_profile",
        )
        edge_id += 1

    if not nx.is_connected(graph):
        components = [list(component) for component in nx.connected_components(graph)]
        components.sort(key=lambda item: min(item))
        for left, right in zip(components[:-1], components[1:]):
            graph.add_edge(
                min(left),
                min(right),
                edgeId=edge_id,
                betweenness=0.0,
                numsp=0,
                rssi=-85.0,
                capacity=200.0 / 12.0,
                bw_allocated=0.0,
                delay=0.235,
                packet_loss=0.28,
                queueing_delay=0.0,
                source="derived_connectivity_bridge",
            )
            edge_id += 1

    for node in graph.nodes():
        angle = 2.0 * np.pi * (node / max(1, graph.number_of_nodes()))
        graph.nodes[node]["pos"] = (50.0 + 42.0 * np.cos(angle), 50.0 + 42.0 * np.sin(angle))
    return graph


def generate_nx_graph(topology):
    hardware_profile = os.environ.get("GRAPH_HARDWARE_RSSI_PROFILE")
    if hardware_profile:
        return _load_hardware_rssi_graph(hardware_profile)

    custom_num_nodes = os.environ.get("GRAPH_NUM_NODES")
    custom_target_edges = os.environ.get("GRAPH_TARGET_EDGES")
    if custom_num_nodes is not None:
        num_nodes = int(custom_num_nodes)
        target_edges = int(custom_target_edges) if custom_target_edges is not None else max(20, 2 * num_nodes)
        graph = create_dynamic_topology(num_nodes=num_nodes, target_edges=target_edges)
    elif topology == 0:
        graph = create_dynamic_topology(num_nodes=10)
    elif topology == 1:
        graph = create_dynamic_topology(num_nodes=15)
    else:
        graph = create_dynamic_topology(num_nodes=20)

    for node in graph.nodes():
        graph.nodes[node]["pos"] = (random.uniform(0, 100), random.uniform(0, 100))

    edge_id = 1
    for src, dst in graph.edges():
        edge_data = graph.get_edge_data(src, dst)
        edge_data["edgeId"] = edge_id
        edge_data["betweenness"] = 0.0
        edge_data["numsp"] = 0

        src_pos = graph.nodes[src]["pos"]
        dst_pos = graph.nodes[dst]["pos"]
        distance = float(np.sqrt((src_pos[0] - dst_pos[0]) ** 2 + (src_pos[1] - dst_pos[1]) ** 2))

        edge_data["capacity"] = float(max(50.0, 200.0 - distance))
        edge_data["bw_allocated"] = 0.0
        edge_data["delay"] = distance / 100.0
        edge_data["packet_loss"] = min(0.3, distance / 500.0 + random.uniform(0, 0.05))
        edge_id += 1

    return graph


def compute_link_betweenness(graph, k):
    num_nodes = len(graph.nodes())
    betweenness_values = []
    for src, dst in graph.edges():
        value = graph.get_edge_data(src, dst)["numsp"] / ((2.0 * num_nodes * (num_nodes - 1) * k) + 1e-8)
        graph.get_edge_data(src, dst)["betweenness"] = value
        betweenness_values.append(value)

    mean_bet = float(np.mean(betweenness_values)) if betweenness_values else 0.0
    std_bet = float(np.std(betweenness_values)) if betweenness_values else 1.0
    return mean_bet, max(std_bet, 1e-8)


def estimate_edge_betweenness(graph, sample_size):
    sample_size = max(1, min(int(sample_size), graph.number_of_nodes()))
    betweenness = nx.edge_betweenness_centrality(
        graph,
        k=sample_size if sample_size < graph.number_of_nodes() else None,
        normalized=True,
        seed=0,
    )
    values = []
    for src, dst in graph.edges():
        value = float(betweenness.get((src, dst), betweenness.get((dst, src), 0.0)))
        graph.get_edge_data(src, dst)["betweenness"] = value
        values.append(value)

    mean_bet = float(np.mean(values)) if values else 0.0
    std_bet = float(np.std(values)) if values else 1.0
    return mean_bet, max(std_bet, 1e-8)


class Env1(gym.Env):
    def __init__(self):
        self.graph = None
        self.initial_state = None
        self.source = None
        self.destination = None
        self.demand = None
        self.graph_state = None
        self.diameter = None

        self.first = None
        self.firstTrueSize = None
        self.second = None
        self.between_feature = None

        self.mu_bet = None
        self.std_bet = None
        self.max_demand = 0
        self.K = 4
        self.listofDemands = None
        self.nodes = None
        self.ordered_edges = None
        self.edgesDict = None
        self.numNodes = None
        self.numEdges = None

        self.state = None
        self.episode_over = True
        self.reward = 0.0
        self.allPaths = {}
        self.path_keys = ()
        self.lazy_paths_enabled = False
        self.packet_loss_rate = 0.0
        self.total_delay = 0.0
        self.link_utilization = 0.0

    def seed(self, seed):
        random.seed(seed)
        np.random.seed(seed)

    def _build_pair_keys(self):
        return tuple(
            f"{src}:{dst}"
            for src in self.graph
            for dst in self.graph
            if src != dst
        )

    def _compute_candidate_paths_for_key(self, path_key):
        source_str, destination_str = path_key.split(":")
        src = int(source_str)
        dst = int(destination_str)
        paths = _weighted_k_shortest_paths(self.graph, src, dst, self.K)
        return sorted(paths, key=lambda item: (len(item), item))[: self.K]

    def num_shortest_path(self, topology):
        del topology
        self.lazy_paths_enabled = os.environ.get("GRAPH_LAZY_PATHS", "0") not in {"0", "", "false", "False"}
        self.path_keys = self._build_pair_keys()

        if self.lazy_paths_enabled:
            self.diameter = None
            self.allPaths = CandidatePathCache(self, self.path_keys)
            sample_size = os.environ.get("GRAPH_BETWEENNESS_SAMPLE_SIZE")
            if sample_size is None:
                sample_size = min(max(self.graph.number_of_nodes() // 4, 32), 128)
            self.mu_bet, self.std_bet = estimate_edge_betweenness(self.graph, sample_size)
            return

        self.diameter = nx.diameter(self.graph)
        self.allPaths = {}

        for src in self.graph:
            for dst in self.graph:
                if src == dst:
                    continue

                key = f"{src}:{dst}"
                paths = _weighted_k_shortest_paths(self.graph, src, dst, self.K)
                paths = sorted(paths, key=lambda item: (len(item), item))
                self.allPaths[key] = paths[: self.K]

                for path in self.allPaths[key]:
                    for start_node, end_node in zip(path[:-1], path[1:]):
                        self.graph.get_edge_data(start_node, end_node)["numsp"] += 1

                gc.collect()

    def _has_full_k_candidate_paths(self):
        if self.lazy_paths_enabled:
            return all(len(self.allPaths[key]) >= self.K for key in self.path_keys)
        return all(len(paths) >= self.K for paths in self.allPaths.values())

    def _resolve_current_path(self, action, source, destination):
        path_key = f"{source}:{destination}"
        path_list = self.allPaths[path_key]
        if not path_list:
            raise RuntimeError(f"No candidate paths available for {path_key}.")

        if os.environ.get("GRAPH_CLAMP_ACTION_TO_PATHS", "0") not in {"0", "", "false", "False"}:
            action = max(0, min(int(action), len(path_list) - 1))

        return path_list[int(action)], int(action)

    def _first_second_between(self):
        self.first = []
        self.second = []
        for src, dst in self.ordered_edges:
            src_neighbors = self.graph.edges(src)
            for edge_src, edge_dst in src_neighbors:
                if (src, dst) != (edge_src, edge_dst) and (src, dst) != (edge_dst, edge_src):
                    self.first.append(self.edgesDict[f"{src}:{dst}"])
                    self.second.append(self.edgesDict[f"{edge_src}:{edge_dst}"])

            dst_neighbors = self.graph.edges(dst)
            for edge_src, edge_dst in dst_neighbors:
                if (src, dst) != (edge_src, edge_dst) and (src, dst) != (edge_dst, edge_src):
                    self.first.append(self.edgesDict[f"{src}:{dst}"])
                    self.second.append(self.edgesDict[f"{edge_src}:{edge_dst}"])

    def _reset_edge_allocations(self):
        self.graph_state[:, 1] = 0.0
        for edge_idx, edge in enumerate(self.ordered_edges):
            edge_data = self.graph.get_edge_data(edge[0], edge[1])
            edge_data["bw_allocated"] = 0.0
            edge_data["capacity"] = float(self.graph_state[edge_idx][0])

    def _compute_utilization_summary(self, current_path):
        global_link_utilizations = []
        path_link_utilizations = []

        for edge_idx, edge in enumerate(self.ordered_edges):
            edge_data = self.graph.get_edge_data(edge[0], edge[1])
            capacity = max(float(edge_data["capacity"]), 1e-6)
            allocated = float(edge_data["bw_allocated"])
            global_link_utilizations.append(allocated / capacity)

        for start_node, end_node in zip(current_path[:-1], current_path[1:]):
            edge_key = f"{start_node}:{end_node}"
            edge_idx = self.edgesDict[edge_key]
            edge = self.ordered_edges[edge_idx]
            edge_data = self.graph.get_edge_data(edge[0], edge[1])
            capacity = max(float(edge_data["capacity"]), 1e-6)
            allocated = float(edge_data["bw_allocated"])
            path_link_utilizations.append(allocated / capacity)

        global_array = np.asarray(global_link_utilizations, dtype=np.float32)
        path_array = np.asarray(path_link_utilizations, dtype=np.float32)

        return {
            "path_link_utilization": float(path_array.mean()) if path_array.size else 0.0,
            "path_max_link_utilization": float(path_array.max()) if path_array.size else 0.0,
            "global_link_utilization": float(global_array.mean()) if global_array.size else 0.0,
            "max_link_utilization": float(global_array.max()) if global_array.size else 0.0,
            "congested_link_ratio": float(np.mean(global_array >= 0.8)) if global_array.size else 0.0,
        }

    def _compute_delay_summary(self, current_path, demand):
        propagation_delay = 0.0
        transmission_delay = 0.0
        queueing_delay = 0.0
        hop_count = 0
        route_distance = 0.0

        for start_node, end_node in zip(current_path[:-1], current_path[1:]):
            edge_data = self.graph.get_edge_data(start_node, end_node)
            link_delay = float(edge_data["delay"])
            capacity = max(float(edge_data["capacity"]), 1e-6)
            allocated = max(float(edge_data["bw_allocated"]), 0.0)
            utilization = min(0.98, allocated / capacity)

            link_transmission_delay = float(demand) / capacity
            # Queueing proxy: grows sharply as utilization approaches saturation.
            link_queueing_delay = 0.25 * (utilization ** 2 / max(1.0 - utilization, 0.02)) * link_transmission_delay

            propagation_delay += link_delay
            transmission_delay += link_transmission_delay
            queueing_delay += link_queueing_delay
            route_distance += link_delay * 100.0
            hop_count += 1

        return {
            "propagation_delay": propagation_delay,
            "transmission_delay": transmission_delay,
            "queueing_delay": queueing_delay,
            "decomposed_total_delay": propagation_delay + transmission_delay + queueing_delay,
            "route_length_hops": hop_count,
            "mean_delay_per_hop": propagation_delay / max(1, hop_count),
            "route_distance": route_distance,
        }

    def generate_environment(self, topology, listofdemands):
        require_full_k_paths = os.environ.get("GRAPH_REQUIRE_FULL_K_PATHS", "0") not in {"0", "", "false", "False"}
        max_generation_attempts = int(os.environ.get("GRAPH_MAX_GENERATION_ATTEMPTS", "50"))

        self.listofDemands = listofdemands
        self.max_demand = int(np.amax(self.listofDemands))

        for attempt in range(1, max_generation_attempts + 1):
            self.graph = generate_nx_graph(topology)
            self.num_shortest_path(topology)
            if not require_full_k_paths or self._has_full_k_candidate_paths():
                break
        else:
            raise RuntimeError(
                f"Unable to generate a topology with at least {self.K} candidate paths per pair "
                f"after {max_generation_attempts} attempts."
            )

        if not self.lazy_paths_enabled:
            self.mu_bet, self.std_bet = compute_link_betweenness(self.graph, self.K)
        self.edgesDict = {}

        self.ordered_edges = sorted(tuple(sorted(edge)) for edge in self.graph.edges())
        self.numNodes = len(self.graph.nodes())
        self.numEdges = len(self.graph.edges())

        self.graph_state = np.zeros((self.numEdges, 2), dtype=np.float32)
        self.between_feature = np.zeros(self.numEdges, dtype=np.float32)

        for position, edge in enumerate(self.ordered_edges):
            src, dst = edge
            self.edgesDict[f"{src}:{dst}"] = position
            self.edgesDict[f"{dst}:{src}"] = position

            edge_data = self.graph.get_edge_data(src, dst)
            standardized_betweenness = (edge_data["betweenness"] - self.mu_bet) / self.std_bet
            edge_data["betweenness"] = standardized_betweenness
            self.graph_state[position][0] = float(edge_data["capacity"])
            self.graph_state[position][1] = 0.0
            self.between_feature[position] = float(standardized_betweenness)

        self.initial_state = np.copy(self.graph_state)
        self._first_second_between()
        self.firstTrueSize = len(self.first)
        self.nodes = list(range(self.numNodes))

    def compute_metrics(self, action, demand, source, destination):
        current_path, action = self._resolve_current_path(action, source, destination)
        total_packet_loss = 0.0
        total_delay = 0.0

        for start_node, end_node in zip(current_path[:-1], current_path[1:]):
            edge_data = self.graph.get_edge_data(start_node, end_node)
            total_packet_loss = 1.0 - (1.0 - total_packet_loss) * (1.0 - float(edge_data["packet_loss"]))
            total_delay += float(edge_data["delay"])

        utilization_summary = self._compute_utilization_summary(current_path)
        delay_summary = self._compute_delay_summary(current_path, demand)
        metrics_summary = {**utilization_summary, **delay_summary}
        return total_packet_loss, total_delay, utilization_summary["path_link_utilization"], metrics_summary

    def make_step(self, state, action, demand, source, destination):
        self.graph_state = np.copy(state)
        self.episode_over = True
        self.reward = 0.0
        current_path, action = self._resolve_current_path(action, source, destination)

        for start_node, end_node in zip(current_path[:-1], current_path[1:]):
            edge_key = f"{start_node}:{end_node}"
            edge_idx = self.edgesDict[edge_key]
            edge_data = self.graph.get_edge_data(start_node, end_node)

            self.graph_state[edge_idx][0] -= demand
            self.graph_state[edge_idx][1] += demand
            edge_data["bw_allocated"] = float(self.graph_state[edge_idx][1])

            if self.graph_state[edge_idx][0] < 0:
                self.packet_loss_rate, self.total_delay, self.link_utilization, utilization_summary = self.compute_metrics(
                    action,
                    demand,
                    source,
                    destination,
                )
                return self.graph_state, self.reward, self.episode_over, self.demand, self.source, self.destination, {
                    "packet_loss_rate": self.packet_loss_rate,
                    "total_delay": self.total_delay,
                    "link_utilization": self.link_utilization,
                    **utilization_summary,
                }

        self.packet_loss_rate, self.total_delay, self.link_utilization, utilization_summary = self.compute_metrics(
            action,
            demand,
            source,
            destination,
        )
        self.reward = 0.3 * self.link_utilization - 0.3 * self.packet_loss_rate - 0.4 * self.total_delay
        self.episode_over = False

        self.demand = random.choice(self.listofDemands)
        self.source = random.choice(self.nodes)
        while True:
            self.destination = random.choice(self.nodes)
            if self.destination != self.source:
                break

        return self.graph_state, self.reward, self.episode_over, self.demand, self.source, self.destination, {
            "packet_loss_rate": self.packet_loss_rate,
            "total_delay": self.total_delay,
            "link_utilization": self.link_utilization,
            **utilization_summary,
        }

    def reset(self):
        self.graph_state = np.copy(self.initial_state)
        self._reset_edge_allocations()

        num_edges_to_modify = max(1, int(0.1 * self.numEdges))
        edges_to_modify = np.random.choice(self.numEdges, num_edges_to_modify, replace=False)

        for edge_idx in edges_to_modify:
            original_capacity = float(self.initial_state[edge_idx][0])
            new_capacity = original_capacity * float(np.random.uniform(0.8, 1.2))
            self.graph_state[edge_idx][0] = new_capacity
            edge = self.ordered_edges[edge_idx]
            edge_data = self.graph.get_edge_data(edge[0], edge[1])
            edge_data["capacity"] = new_capacity
            if edge_data.get("source") == "hardware_rssi_profile":
                base_rssi = float(edge_data.get("rssi", -75.0))
                varied_rssi = max(-95.0, min(-35.0, base_rssi + float(np.random.normal(0.0, 2.0))))
                edge_data["rssi"] = varied_rssi
                if varied_rssi >= -55:
                    edge_data["packet_loss"] = 0.015
                    edge_data["delay"] = 0.055
                elif varied_rssi >= -65:
                    edge_data["packet_loss"] = 0.04
                    edge_data["delay"] = 0.075
                elif varied_rssi >= -75:
                    edge_data["packet_loss"] = 0.08
                    edge_data["delay"] = 0.105
                elif varied_rssi >= -80:
                    edge_data["packet_loss"] = 0.16
                    edge_data["delay"] = 0.155
                elif varied_rssi >= -85:
                    edge_data["packet_loss"] = 0.28
                    edge_data["delay"] = 0.235
                else:
                    edge_data["packet_loss"] = 0.45
                    edge_data["delay"] = 0.35

        self.demand = random.choice(self.listofDemands)
        self.source = random.choice(self.nodes)
        while True:
            self.destination = random.choice(self.nodes)
            if self.destination != self.source:
                break

        self.packet_loss_rate = 0.0
        self.total_delay = 0.0
        self.link_utilization = 0.0
        return self.graph_state, self.demand, self.source, self.destination

    def eval_sap_reset(self, demand, source, destination):
        self.graph_state = np.copy(self.initial_state)
        self._reset_edge_allocations()
        self.demand = demand
        self.source = source
        self.destination = destination

        self.packet_loss_rate = 0.0
        self.total_delay = 0.0
        self.link_utilization = 0.0
        return self.graph_state
