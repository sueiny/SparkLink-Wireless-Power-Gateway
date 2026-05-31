import torch
import torch.nn as nn
import torch.nn.functional as F

class myModel(nn.Module):
    def __init__(self, hparams):
        super(myModel, self).__init__()
        self.hparams = hparams

        # Define layers here
        self.Message = nn.Sequential(
            nn.Linear(self.hparams['link_state_dim']*2, self.hparams['link_state_dim']),
            nn.SELU()
        )

        self.Update = nn.GRUCell(self.hparams['link_state_dim'], self.hparams['link_state_dim'])

        self.Readout = nn.Sequential(
            nn.Linear(self.hparams['link_state_dim'], self.hparams['readout_units']),
            nn.SELU(),
            nn.Dropout(p=hparams['dropout_rate']),
            nn.Linear(self.hparams['readout_units'], self.hparams['readout_units']),
            nn.SELU(),
            nn.Dropout(p=hparams['dropout_rate']),
            nn.Linear(self.hparams['readout_units'], 1)
        )

    def forward(self, states_action, states_graph_ids, states_first, states_second, sates_num_edges, training=False):
        link_state = states_action

        # Execute T times
        for _ in range(self.hparams['T']):
            # We have the combination of the hidden states of the main edges with the neighbours
            mainEdges = torch.index_select(link_state, 0, states_first)
            neighEdges = torch.index_select(link_state, 0, states_second)

            edgesConcat = torch.cat([mainEdges, neighEdges], dim=1)

            # 1.a Message passing for link with all its neighbours
            outputs = self.Message(edgesConcat)

            # 1.b Sum of output values according to link id index
            edges_inputs = torch.zeros((sates_num_edges, self.hparams['link_state_dim'])).to(states_action.device)
            edges_inputs.index_add_(0, states_second, outputs)

            # 2. Update for each link
            # GRUCell needs a 3D tensor as state because there is a matmul: Wrap the link state
            link_state = self.Update(edges_inputs, link_state)

        # Perform sum of all hidden states
        edges_combi_outputs = torch.zeros_like(link_state).to(states_action.device)
        edges_combi_outputs.index_add_(0, states_graph_ids, link_state)

        r = self.Readout(edges_combi_outputs)
        return r
