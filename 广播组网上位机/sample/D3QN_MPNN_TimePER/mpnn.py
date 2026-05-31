import torch
import torch.nn as nn


class DuelingMPNN(nn.Module):
    def __init__(self, hparams, num_actions, route_feature_dim):
        super().__init__()
        self.num_actions = num_actions
        self.hidden_dim = hparams["hidden_dim"]
        self.message_passing_steps = hparams["T"]

        self.edge_encoder = nn.Sequential(
            nn.Linear(hparams["edge_feature_dim"], self.hidden_dim),
            nn.ReLU(),
            nn.Linear(self.hidden_dim, self.hidden_dim),
            nn.ReLU(),
        )
        self.message_mlp = nn.Sequential(
            nn.Linear(self.hidden_dim * 2, self.hidden_dim),
            nn.ReLU(),
            nn.Linear(self.hidden_dim, self.hidden_dim),
            nn.ReLU(),
        )
        self.update_cell = nn.GRUCell(self.hidden_dim, self.hidden_dim)
        self.route_encoder = nn.Sequential(
            nn.Linear(route_feature_dim, self.hidden_dim),
            nn.ReLU(),
        )
        self.value_head = nn.Sequential(
            nn.Linear(self.hidden_dim * 2, hparams["readout_units"]),
            nn.ReLU(),
            nn.Dropout(p=hparams["dropout_rate"]),
            nn.Linear(hparams["readout_units"], hparams["readout_units"]),
            nn.ReLU(),
            nn.Dropout(p=hparams["dropout_rate"]),
            nn.Linear(hparams["readout_units"], 1),
        )
        self.advantage_head = nn.Sequential(
            nn.Linear(self.hidden_dim * 3, hparams["readout_units"]),
            nn.ReLU(),
            nn.Dropout(p=hparams["dropout_rate"]),
            nn.Linear(hparams["readout_units"], hparams["readout_units"]),
            nn.ReLU(),
            nn.Dropout(p=hparams["dropout_rate"]),
            nn.Linear(hparams["readout_units"], 1),
        )

    def forward(self, edge_features, first, second, route_features, path_edge_indices):
        if edge_features.dim() == 2:
            edge_features = edge_features.unsqueeze(0)
        if route_features.dim() == 1:
            route_features = route_features.unsqueeze(0)

        hidden = self.edge_encoder(edge_features)
        batch_size, _, hidden_dim = hidden.shape

        if first.numel() > 0 and second.numel() > 0:
            scatter_index = first.view(1, -1, 1).expand(batch_size, -1, hidden_dim)
            for _ in range(self.message_passing_steps):
                main_edges = hidden[:, first, :]
                neighbor_edges = hidden[:, second, :]
                messages = self.message_mlp(torch.cat([main_edges, neighbor_edges], dim=-1))

                aggregated = torch.zeros_like(hidden)
                aggregated.scatter_add_(1, scatter_index, messages)

                hidden = self.update_cell(
                    aggregated.reshape(-1, hidden_dim),
                    hidden.reshape(-1, hidden_dim),
                ).reshape(batch_size, -1, hidden_dim)

        graph_embeddings = hidden.mean(dim=1)
        route_embeddings = self.route_encoder(route_features)
        q_values = hidden.new_full((batch_size, self.num_actions), -1e9)

        for batch_index, candidate_paths in enumerate(path_edge_indices):
            graph_context = graph_embeddings[batch_index]
            route_context = route_embeddings[batch_index]
            value_input = torch.cat([graph_context, route_context], dim=-1).unsqueeze(0)
            state_value = self.value_head(value_input).squeeze(-1).squeeze(0)

            valid_action_indices = []
            advantages = []
            for action_index, edge_indices in enumerate(candidate_paths[: self.num_actions]):
                if edge_indices.numel() == 0:
                    continue
                path_embedding = hidden[batch_index].index_select(0, edge_indices).mean(dim=0)
                advantage_input = torch.cat([graph_context, route_context, path_embedding], dim=-1).unsqueeze(0)
                advantages.append(self.advantage_head(advantage_input).squeeze(-1).squeeze(0))
                valid_action_indices.append(action_index)

            if not valid_action_indices:
                continue

            centered_advantages = torch.stack(advantages)
            centered_advantages = centered_advantages - centered_advantages.mean()

            for action_position, action_index in enumerate(valid_action_indices):
                q_values[batch_index, action_index] = state_value + centered_advantages[action_position]

        return q_values
