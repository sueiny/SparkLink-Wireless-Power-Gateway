import gc
import os
import random
from pathlib import Path

import numpy as np
import torch
import torch.nn.functional as F
import torch.optim as optim
from torch.utils.tensorboard import SummaryWriter

from mpnn import DuelingMPNN


differentiation_str = os.environ.get("D3QN_MPNN_TIMEPER_RUN_NAME", "D3QN_MPNN_TimePER")
experiment_root = Path(__file__).resolve().parent
project_root = experiment_root.parent
artifacts_root = project_root / "artifacts"
checkpoint_dir = artifacts_root / "checkpoints" / differentiation_str
train_dir = artifacts_root / "tensorboard" / differentiation_str
legacy_logs_dir = experiment_root / "D3QN_MPNN_pytorch" / "Logs"

checkpoint_dir.mkdir(parents=True, exist_ok=True)
train_dir.mkdir(parents=True, exist_ok=True)
legacy_logs_dir.mkdir(parents=True, exist_ok=True)

summary_writer = SummaryWriter(log_dir=str(train_dir))
device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
checkpoint_prefix = str(checkpoint_dir / "checkpoint.pth")

MAX_QUEUE_SIZE = 15000
listofDemands = [8, 32, 64]
target_update_interval = 20
evaluation_interval = 20
epsilon_start_decay = 30
gradient_steps_per_update = 3
replay_interval = 1
target_update_tau = 0.04
guided_exploration_probability = 0.75
guided_best_probability = 0.92
heuristic_bias_scale = 0.08
guidance_warmup_steps = 40
guidance_ramp_steps = 220
priority_alpha = 0.45
priority_epsilon = 1e-3
time_factor = 0.35
recency_half_life = 9000.0
importance_beta_start = 0.35
importance_beta_steps = 320000
uniform_mix_ratio = 0.20
new_sample_priority_blend = 0.25
priority_smoothing = 0.40
reward_loss_weight = float(os.environ.get("D3QN_REWARD_LOSS_WEIGHT", "8.0"))
reward_delay_weight = float(os.environ.get("D3QN_REWARD_DELAY_WEIGHT", "1.6"))
reward_utilization_weight = float(os.environ.get("D3QN_REWARD_UTILIZATION_WEIGHT", "0.08"))
reward_hop_weight = float(os.environ.get("D3QN_REWARD_HOP_WEIGHT", "0.18"))
reward_weak_rssi_weight = float(os.environ.get("D3QN_REWARD_WEAK_RSSI_WEIGHT", "2.4"))
reward_overload_weight = float(os.environ.get("D3QN_REWARD_OVERLOAD_WEIGHT", "8.0"))

hparams = {
    "l2": 1e-5,
    "dropout_rate": 0.02,
    "learning_rate": 1.6e-4,
    "batch_size": 32,
    "hidden_dim": 112,
    "readout_units": 128,
    "T": 3,
    "gamma": 0.15,
    "edge_feature_dim": 3,
    "num_demands": len(listofDemands),
}


class TimeAwarePrioritizedReplayBuffer:
    def __init__(
        self,
        capacity,
        priority_alpha_value,
        priority_epsilon_value,
        time_factor_value,
        recency_half_life_value,
        importance_beta_start_value,
        importance_beta_steps_value,
        uniform_mix_ratio_value,
        new_sample_priority_blend_value,
        priority_smoothing_value,
    ):
        self.capacity = int(capacity)
        self.priority_alpha = float(priority_alpha_value)
        self.priority_epsilon = float(priority_epsilon_value)
        self.time_factor = float(time_factor_value)
        self.recency_half_life = float(recency_half_life_value)
        self.importance_beta_start = float(importance_beta_start_value)
        self.importance_beta_steps = int(importance_beta_steps_value)
        self.uniform_mix_ratio = float(uniform_mix_ratio_value)
        self.new_sample_priority_blend = float(new_sample_priority_blend_value)
        self.priority_smoothing = float(priority_smoothing_value)
        self.samples = [None] * self.capacity
        self.priorities = np.zeros(self.capacity, dtype=np.float32)
        self.timestamps = np.zeros(self.capacity, dtype=np.int64)
        self.position = 0
        self.size = 0
        self.insert_count = 0

    def __len__(self):
        return self.size

    def append(self, sample):
        if self.size > 0:
            current_priorities = self.priorities[: self.size]
            mean_priority = float(current_priorities.mean())
            max_priority = float(current_priorities.max())
            blended_priority = (
                (1.0 - self.new_sample_priority_blend) * mean_priority
                + self.new_sample_priority_blend * max_priority
            )
            initial_priority = max(blended_priority, self.priority_epsilon)
        else:
            initial_priority = 1.0
        self.samples[self.position] = sample
        self.priorities[self.position] = max(initial_priority, self.priority_epsilon)
        self.timestamps[self.position] = self.insert_count
        self.position = (self.position + 1) % self.capacity
        self.size = min(self.size + 1, self.capacity)
        self.insert_count += 1

    def _current_beta(self):
        progress = min(1.0, self.insert_count / max(1, self.importance_beta_steps))
        return self.importance_beta_start + progress * (1.0 - self.importance_beta_start)

    def _sampling_scores(self):
        valid_priorities = self.priorities[: self.size]
        valid_timestamps = self.timestamps[: self.size].astype(np.float32)
        ages = np.maximum(0.0, float(self.insert_count) - valid_timestamps)
        recency_scores = 1.0 / (1.0 + ages / self.recency_half_life)
        priority_scores = np.power(valid_priorities + self.priority_epsilon, self.priority_alpha)
        combined_scores = priority_scores * (1.0 + self.time_factor * recency_scores)
        return combined_scores, recency_scores

    def sample(self, batch_size):
        if self.size < batch_size:
            return None

        sampling_scores, recency_scores = self._sampling_scores()
        probabilities = sampling_scores / np.maximum(sampling_scores.sum(), 1e-12)
        probabilities = (
            (1.0 - self.uniform_mix_ratio) * probabilities
            + self.uniform_mix_ratio / max(1, self.size)
        )
        probabilities = probabilities / np.maximum(probabilities.sum(), 1e-12)
        indices = np.random.choice(self.size, size=batch_size, replace=False, p=probabilities)
        beta = self._current_beta()
        importance_weights = np.power(self.size * probabilities[indices] + 1e-12, -beta)
        importance_weights = importance_weights / np.maximum(importance_weights.max(), 1e-12)

        replay_stats = {
            "mean_priority": float(np.mean(self.priorities[indices])),
            "mean_recency": float(np.mean(recency_scores[indices])),
            "importance_beta": float(beta),
        }
        batch = [self.samples[idx] for idx in indices]
        return batch, indices, importance_weights.astype(np.float32), replay_stats

    def update_priorities(self, indices, td_errors):
        updated_priorities = np.abs(np.asarray(td_errors, dtype=np.float32)) + self.priority_epsilon
        self.priorities[indices] = np.maximum(
            (1.0 - self.priority_smoothing) * self.priorities[indices]
            + self.priority_smoothing * updated_priorities,
            self.priority_epsilon,
        )


class D3QNMPNNAgent:
    def __init__(self, env_training, batch_size):
        self.env = env_training
        self.memory = TimeAwarePrioritizedReplayBuffer(
            MAX_QUEUE_SIZE,
            priority_alpha,
            priority_epsilon,
            time_factor,
            recency_half_life,
            importance_beta_start,
            importance_beta_steps,
            uniform_mix_ratio,
            new_sample_priority_blend,
            priority_smoothing,
        )
        self.gamma = hparams["gamma"]
        self.epsilon = 1.0
        self.epsilon_min = 0.06
        self.epsilon_decay = 0.9965
        self.writer = summary_writer
        self.K = 4
        self.numbersamples = batch_size
        self.global_step = 0
        self.training_iteration = 0
        self.path_cache = {}

        self.num_actions = self.K
        self.num_nodes = env_training.numNodes
        self.route_feature_dim = hparams["num_demands"] + 1 + 2 * self.num_nodes
        self.initial_capacities = np.maximum(env_training.initial_state[:, 0].astype(np.float32), 1.0)
        self.betweenness = np.asarray(env_training.between_feature, dtype=np.float32)
        self.first_indices = torch.as_tensor(env_training.first, dtype=torch.long, device=device)
        self.second_indices = torch.as_tensor(env_training.second, dtype=torch.long, device=device)

        self.primary_network = DuelingMPNN(hparams, self.num_actions, self.route_feature_dim).to(device)
        self.target_network = DuelingMPNN(hparams, self.num_actions, self.route_feature_dim).to(device)
        self.target_network.load_state_dict(self.primary_network.state_dict())
        self.optimizer = optim.AdamW(
            params=self.primary_network.parameters(),
            lr=hparams["learning_rate"],
            weight_decay=hparams["l2"],
        )

    def set_training_iteration(self, episode):
        self.training_iteration = int(episode)

    def _guidance_progress(self):
        if self.training_iteration <= guidance_warmup_steps:
            return 0.0
        progress = (self.training_iteration - guidance_warmup_steps) / float(guidance_ramp_steps)
        return max(0.0, min(1.0, progress))

    def _current_guided_exploration_probability(self):
        return guided_exploration_probability * self._guidance_progress()

    def _current_guided_best_probability(self):
        progress = self._guidance_progress()
        return 0.55 + progress * (guided_best_probability - 0.55)

    def _current_heuristic_bias(self):
        return heuristic_bias_scale * self._guidance_progress()

    def _build_edge_feature_array(self, state):
        remaining_capacity = state[:, 0].astype(np.float32) / 200.0
        remaining_ratio = np.clip(state[:, 0].astype(np.float32) / self.initial_capacities, 0.0, 1.5)
        return np.stack([remaining_capacity, remaining_ratio, self.betweenness], axis=1)

    def _build_route_feature_array(self, demand, source, destination):
        demand_onehot = np.zeros(len(listofDemands), dtype=np.float32)
        demand_index = listofDemands.index(demand) if demand in listofDemands else 0
        demand_onehot[demand_index] = 1.0

        source_onehot = np.zeros(self.num_nodes, dtype=np.float32)
        destination_onehot = np.zeros(self.num_nodes, dtype=np.float32)
        source_onehot[int(source)] = 1.0
        destination_onehot[int(destination)] = 1.0

        demand_scalar = np.array([float(demand) / float(max(listofDemands))], dtype=np.float32)
        return np.concatenate([demand_onehot, demand_scalar, source_onehot, destination_onehot], axis=0)

    def _get_candidate_paths(self, env, source, destination):
        key = (int(source), int(destination))
        if key not in self.path_cache:
            path_key = f"{source}:{destination}"
            candidate_paths = []
            for path in env.allPaths[path_key][: self.K]:
                edge_indices = [
                    env.edgesDict[f"{start_node}:{end_node}"]
                    for start_node, end_node in zip(path[:-1], path[1:])
                ]
                candidate_paths.append(torch.as_tensor(edge_indices, dtype=torch.long, device=device))
            self.path_cache[key] = candidate_paths
        return self.path_cache[key]

    def _predict_q_values(self, env, state, demand, source, destination):
        edge_features = torch.as_tensor(
            self._build_edge_feature_array(state),
            dtype=torch.float32,
            device=device,
        ).unsqueeze(0)
        route_features = torch.as_tensor(
            self._build_route_feature_array(demand, source, destination),
            dtype=torch.float32,
            device=device,
        ).unsqueeze(0)
        candidate_paths = [self._get_candidate_paths(env, source, destination)]
        return self.primary_network(
            edge_features,
            self.first_indices,
            self.second_indices,
            route_features,
            candidate_paths,
        )

    def _estimate_path_metrics(self, env, state, demand, source, destination, action):
        path = env.allPaths[f"{source}:{destination}"][action]
        total_packet_loss = 0.0
        total_delay = 0.0
        total_link_utilization = 0.0
        edges_used = 0
        overload_penalty = 0.0
        weak_rssi_penalty = 0.0

        for start_node, end_node in zip(path[:-1], path[1:]):
            edge_key = f"{start_node}:{end_node}"
            edge_index = env.edgesDict[edge_key]
            edge_data = env.graph.get_edge_data(start_node, end_node)

            capacity = float(edge_data.get("capacity", 0.0))
            allocated = float(edge_data.get("bw_allocated", 0.0))
            packet_loss = float(edge_data.get("packet_loss", 0.0))
            delay = float(edge_data.get("delay", 0.0))
            rssi = float(edge_data.get("rssi", -55.0))

            remaining_after = float(state[edge_index][0]) - float(demand)
            if remaining_after < 0:
                overload_penalty += abs(remaining_after) / max(float(demand), 1.0)

            new_allocated = allocated + float(demand)
            utilization = 1.0 if capacity <= 0.0 else min(1.0, new_allocated / capacity)

            total_packet_loss = 1.0 - (1.0 - total_packet_loss) * (1.0 - packet_loss)
            total_delay += delay
            total_link_utilization += utilization
            if rssi < -75.0:
                weak_rssi_penalty += min(1.0, (-75.0 - rssi) / 15.0)
            edges_used += 1

        avg_link_utilization = total_link_utilization / max(1, edges_used)
        return total_packet_loss, total_delay, avg_link_utilization, overload_penalty, weak_rssi_penalty, edges_used

    def estimate_action_cost(self, env, state, demand, source, destination, action):
        total_packet_loss, total_delay, avg_link_utilization, overload_penalty, weak_rssi_penalty, hop_count = self._estimate_path_metrics(
            env,
            state,
            demand,
            source,
            destination,
            action,
        )
        return (
            reward_loss_weight * total_packet_loss
            + reward_delay_weight * total_delay
            + reward_overload_weight * overload_penalty
            + reward_weak_rssi_weight * weak_rssi_penalty
            + reward_hop_weight * hop_count
            - 0.08 * avg_link_utilization
        )

    def sample_guided_action(self, env, state, demand, source, destination, num_paths):
        scored_actions = [
            (self.estimate_action_cost(env, state, demand, source, destination, action), action)
            for action in range(num_paths)
        ]
        scored_actions.sort(key=lambda item: item[0])

        if num_paths == 1 or random.random() < self._current_guided_best_probability():
            return scored_actions[0][1]

        upper_bound = min(2, len(scored_actions) - 1)
        return scored_actions[random.randint(1, upper_bound)][1]

    def _select_delay_aware_action(self, env, state, demand, source, destination, scores, num_paths):
        valid_scores = scores[:num_paths]
        if num_paths == 1:
            return 0
        if not torch.isfinite(valid_scores).all():
            finite_indices = torch.nonzero(torch.isfinite(valid_scores), as_tuple=False).flatten()
            if finite_indices.numel() == 0:
                return 0
            return int(finite_indices[torch.argmax(valid_scores[finite_indices])].item())
        best_score = valid_scores.max()
        score_spread = torch.clamp(best_score - valid_scores.min(), min=1e-6)
        score_margin = max(0.03, 0.18 * float(score_spread.item()))

        candidate_actions = [
            action_index
            for action_index in range(num_paths)
            if float(best_score.item() - valid_scores[action_index].item()) <= score_margin
        ]
        if not candidate_actions:
            return int(torch.argmax(valid_scores).item())
        if len(candidate_actions) == 1 or self.training_iteration < 80:
            return int(candidate_actions[0])

        ranked_actions = []
        for action_index in candidate_actions:
            total_packet_loss, total_delay, avg_link_utilization, overload_penalty, weak_rssi_penalty, hop_count = self._estimate_path_metrics(
                env,
                state,
                demand,
                source,
                destination,
                action_index,
            )
            ranked_actions.append(
                (
                    total_delay
                    + 0.35 * total_packet_loss
                    + 0.5 * overload_penalty
                    + 0.35 * weak_rssi_penalty
                    + 0.04 * hop_count
                    - 0.02 * avg_link_utilization,
                    action_index,
                )
            )
        ranked_actions.sort(key=lambda item: item[0])
        return int(ranked_actions[0][1])

    def act(self, env, state, demand, source, destination, flagEvaluation):
        candidate_paths = self._get_candidate_paths(env, source, destination)
        num_paths = min(len(candidate_paths), self.K)
        if num_paths == 0:
            return 0, {
                "state": np.array(state, copy=True),
                "demand": int(demand),
                "source": int(source),
                "destination": int(destination),
            }

        state_action = {
            "state": np.array(state, copy=True),
            "demand": int(demand),
            "source": int(source),
            "destination": int(destination),
        }

        if flagEvaluation or random.random() > self.epsilon:
            self.primary_network.eval()
            with torch.no_grad():
                q_values = self._predict_q_values(env, state, demand, source, destination)

            heuristic_bias = self._current_heuristic_bias() if not flagEvaluation else heuristic_bias_scale
            if heuristic_bias > 0.0:
                heuristic_costs = torch.as_tensor(
                    [
                        self.estimate_action_cost(env, state, demand, source, destination, action_index)
                        for action_index in range(num_paths)
                    ],
                    dtype=torch.float32,
                    device=device,
                )
                if num_paths == 1:
                    normalized_costs = torch.zeros_like(heuristic_costs)
                else:
                    normalized_costs = (heuristic_costs - heuristic_costs.mean()) / (
                        heuristic_costs.std(unbiased=False) + 1e-6
                    )
                combined_scores = q_values[0, :num_paths] - heuristic_bias * normalized_costs
                action = self._select_delay_aware_action(
                    env,
                    state,
                    demand,
                    source,
                    destination,
                    combined_scores,
                    num_paths,
                )
            else:
                action = self._select_delay_aware_action(
                    env,
                    state,
                    demand,
                    source,
                    destination,
                    q_values[0, :num_paths],
                    num_paths,
                )
        else:
            self.primary_network.train()
            if random.random() < self._current_guided_exploration_probability():
                action = self.sample_guided_action(env, state, demand, source, destination, num_paths)
            else:
                action = random.randrange(num_paths)

        return action, state_action

    def compute_training_reward(self, info):
        packet_loss_rate = float(info["packet_loss_rate"])
        total_delay = float(info["total_delay"])
        link_utilization = float(info["link_utilization"])
        hop_count = float(info.get("route_length_hops", 0.0))
        weak_rssi_penalty = 0.0
        return (
            reward_utilization_weight * link_utilization
            - reward_loss_weight * packet_loss_rate
            - reward_delay_weight * total_delay
            - reward_hop_weight * hop_count
            - reward_weak_rssi_weight * weak_rssi_penalty
        )

    def add_sample(
        self,
        state_action,
        action,
        reward,
        done,
        new_state,
        new_demand,
        new_source,
        new_destination,
    ):
        self.memory.append(
            (
                np.array(state_action["state"], copy=True),
                int(state_action["demand"]),
                int(state_action["source"]),
                int(state_action["destination"]),
                int(action),
                float(reward),
                np.array(new_state, copy=True),
                int(new_demand),
                int(new_source),
                int(new_destination),
                float(done),
            )
        )

    def _prepare_batch(self, batch):
        edge_features = []
        route_features = []
        next_edge_features = []
        next_route_features = []
        candidate_paths = []
        next_candidate_paths = []
        actions = []
        rewards = []
        dones = []

        for sample in batch:
            state, demand, source, destination, action, reward, next_state, next_demand, next_source, next_destination, done = sample
            edge_features.append(self._build_edge_feature_array(state))
            route_features.append(self._build_route_feature_array(demand, source, destination))
            next_edge_features.append(self._build_edge_feature_array(next_state))
            next_route_features.append(self._build_route_feature_array(next_demand, next_source, next_destination))
            candidate_paths.append(self._get_candidate_paths(self.env, source, destination))
            next_candidate_paths.append(self._get_candidate_paths(self.env, next_source, next_destination))
            actions.append(action)
            rewards.append(reward)
            dones.append(done)

        return (
            torch.as_tensor(np.stack(edge_features), dtype=torch.float32, device=device),
            torch.as_tensor(np.stack(route_features), dtype=torch.float32, device=device),
            candidate_paths,
            torch.as_tensor(actions, dtype=torch.long, device=device),
            torch.as_tensor(rewards, dtype=torch.float32, device=device),
            torch.as_tensor(np.stack(next_edge_features), dtype=torch.float32, device=device),
            torch.as_tensor(np.stack(next_route_features), dtype=torch.float32, device=device),
            next_candidate_paths,
            torch.as_tensor(dones, dtype=torch.float32, device=device),
        )

    def _soft_update_target_network(self):
        for target_param, param in zip(self.target_network.parameters(), self.primary_network.parameters()):
            target_param.data.lerp_(param.data, target_update_tau)

    def _train_step(self, batch, importance_weights):
        (
            edge_features,
            route_features,
            candidate_paths,
            actions,
            rewards,
            next_edge_features,
            next_route_features,
            next_candidate_paths,
            dones,
        ) = self._prepare_batch(batch)
        sample_weights = torch.as_tensor(importance_weights, dtype=torch.float32, device=device)

        current_q_values = self.primary_network(
            edge_features,
            self.first_indices,
            self.second_indices,
            route_features,
            candidate_paths,
        ).gather(1, actions.unsqueeze(1)).squeeze(1)

        with torch.no_grad():
            next_online_q_values = self.primary_network(
                next_edge_features,
                self.first_indices,
                self.second_indices,
                next_route_features,
                next_candidate_paths,
            )
            next_actions = next_online_q_values.argmax(dim=1, keepdim=True)
            next_target_q_values = self.target_network(
                next_edge_features,
                self.first_indices,
                self.second_indices,
                next_route_features,
                next_candidate_paths,
            ).gather(1, next_actions).squeeze(1)
            target_q_values = rewards + self.gamma * next_target_q_values * (1.0 - dones)

        td_errors = target_q_values - current_q_values
        per_sample_loss = F.smooth_l1_loss(current_q_values, target_q_values, reduction="none")
        loss = (sample_weights * per_sample_loss).mean()

        self.optimizer.zero_grad()
        loss.backward()
        torch.nn.utils.clip_grad_norm_(self.primary_network.parameters(), max_norm=1.0)
        self.optimizer.step()
        return loss, td_errors.detach().abs().cpu().numpy()

    def replay(self, episode):
        if len(self.memory) < self.numbersamples or episode % replay_interval != 0:
            return

        self.primary_network.train()
        for _ in range(gradient_steps_per_update):
            sampled = self.memory.sample(self.numbersamples)
            if sampled is None:
                return
            batch, indices, importance_weights, replay_stats = sampled
            loss, td_errors = self._train_step(batch, importance_weights)
            self.memory.update_priorities(indices, td_errors)
            self._write_summary(loss, replay_stats)
            self._soft_update_target_network()

        if episode % target_update_interval == 0:
            self.target_network.load_state_dict(self.primary_network.state_dict())

        gc.collect()

    def _write_summary(self, loss, replay_stats):
        self.writer.add_scalar("loss", float(loss.item()), self.global_step)
        self.writer.add_scalar("replay_priority_mean", replay_stats["mean_priority"], self.global_step)
        self.writer.add_scalar("replay_recency_mean", replay_stats["mean_recency"], self.global_step)
        self.writer.add_scalar("importance_beta", replay_stats["importance_beta"], self.global_step)
        self.global_step += 1
