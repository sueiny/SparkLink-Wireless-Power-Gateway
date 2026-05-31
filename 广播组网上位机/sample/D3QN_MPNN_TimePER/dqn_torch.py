import multiprocessing
import time as tt
import glob
from tqdm import tqdm
from collections import deque
import torch
import torch.nn as nn
import torch.nn.functional as F
import torch.optim as optim
from torch.utils.tensorboard import SummaryWriter
import numpy as np
import random
import mpnn as gnn
import os
import gc
from pathlib import Path

# 基本配置
differentiation_str = "DQN_MPNN_test"
project_root = Path(__file__).resolve().parents[1]
artifacts_root = project_root / "artifacts"
checkpoint_dir = artifacts_root / "checkpoints" / differentiation_str
train_dir = artifacts_root / "tensorboard" / differentiation_str
legacy_logs_dir = project_root / "DQN_pytorch" / "Logs"
legacy_logs_dir.mkdir(parents=True, exist_ok=True)
checkpoint_dir.mkdir(parents=True, exist_ok=True)
train_dir.mkdir(parents=True, exist_ok=True)
summary_writer = SummaryWriter(log_dir=str(train_dir))
device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
summary_writer = SummaryWriter(log_dir=train_dir) #TensorBoard日志记录器
device = torch.device('cuda' if torch.cuda.is_available() else 'cpu')
MAX_QUEUE_SIZE = 20000  # 增加经验回放大小
listofDemands = [8, 32, 64]  #需求列表，用于环境交互
copy_weights_interval = 10  # 更频繁地更新目标网络
evaluation_interval = 20 #每隔多少个回合进行一次评估
epsilon_start_decay = 70 #开始衰减epsilon的回合数
MULTI_FACTOR_BATCH = 6  # 训练中使用的批次数量因子
store_loss = 3  # 每隔多少个批次存储一次损失

if not os.path.exists(checkpoint_dir):
    os.makedirs(checkpoint_dir)
checkpoint_prefix = os.path.join(checkpoint_dir, 'checkpoint.pth')

# 超参数
hparams = {
    # 1. 降低模型复杂度
    'l2': 0.0001,  # 降低L2正则化强度，避免过度约束
    'dropout_rate': 0.1,  # 降低dropout率，保留更多信息
    'state_dim': 64,  # 减小状态维度
    'hidden_dim': 128,  # 减小隐藏层维度
    'readout_units': 64,  # 减小读出层维度
    
    # 2. 优化训练参数
    'learning_rate': 0.0001,  # 降低学习率，使训练更稳定
    'batch_size': 64,  # 减小批次大小，增加更新频率
    'T': 2,  # 减少消息传递轮数，降低过拟合风险
    'gamma': 0.95,  # 略微降低折扣因子，更注重即时奖励
    
    # 3. 保持不变的参数
    'link_state_dim': 64,  # 保持链路状态维度不变
    'num_demands': len(listofDemands),  # 需求数量保持不变
}

class myModel(nn.Module):
    def __init__(self, hparams, num_actions):
        super(myModel, self).__init__()
        
        # 1. 分离局部和全局特征处理
        self.local_processor = nn.Sequential(
            nn.Linear(hparams['link_state_dim'], hparams['hidden_dim']),
            nn.ReLU()
        )
        
        # 2. 简化消息传递
        self.T = 1  # 减少消息传递轮数
        
        # 3. 添加残差连接
        self.residual = True

class DQN(nn.Module): #定义DQN网络
    # def __init__(self, state_dim, hidden_dim, num_actions):
    #     super(DQN, self).__init__()
    #     self.fc1 = nn.Linear(state_dim, hidden_dim)  #定义第一个全连接层，将输入的状态维度映射到隐藏层维度
    #     self.fc2 = nn.Linear(hidden_dim, hidden_dim) #定义第二个全连接层，将隐藏层维度映射到另一个隐藏层维度
    #     self.fc3 = nn.Linear(hidden_dim, num_actions) #定义第三个全连接层，将隐藏层维度映射到动作维度

    def __init__(self, state_dim, hidden_dim, num_actions):
        super(DQN, self).__init__()
        self.fc1 = nn.Linear(state_dim, hidden_dim)  #定义第一个全连接层，将输入的状态维度映射到隐藏层维度
        self.fc2 = nn.Linear(hidden_dim, hidden_dim) #定义第二个全连接层，将隐藏层维度映射到另一个隐藏层维度
        self.fc3 = nn.Linear(hidden_dim, num_actions) #定义第三个全连接层，将隐藏层维度映射到动作维度

    def forward(self, x):
        x = F.relu(self.fc1(x)) #通过第一个全连接层，并应用ReLu激活函数
        x = F.relu(self.fc2(x)) #通过第二个全连接层，并应用ReLu激活函数
        x = self.fc3(x) #通过第三个全连接层，得到动作的Q值
        return x
def cummax(alist, extractor):
    with torch.no_grad():
        maxes = [torch.max(extractor(v)) + 1 for v in alist]
        cummaxes = [torch.zeros_like(maxes[0])]
        for i in range(len(maxes) - 1):
            cummaxes.append(sum(maxes[0:i + 1]))
    return cummaxes
class DQNAgent:
    def __init__(self, env_training, batch_size):
        self.env = env_training
        self.memory = deque(maxlen=MAX_QUEUE_SIZE)
        self.gamma = 0.99  # 与hparams保持一致
        self.epsilon = 1.0
        self.epsilon_min = 0.2  # 降低最小探索率
        self.epsilon_decay = 0.997  # 调整衰减率
        self.writer = summary_writer
        self.K = 4
        self.numbersamples = batch_size
        self.action = None
        self.global_step = 0

        self.num_actions = self.K
        self.state_dim = hparams['state_dim']
        self.bw_allocated_feature = np.zeros((env_training.numEdges, len(env_training.listofDemands)))

        # 初始化 DQN 网络
        # self.primary_network = DQN(self.state_dim, hparams['hidden_dim'], self.num_actions).to(device)
        # self.target_network = DQN(self.state_dim, hparams['hidden_dim'], self.num_actions).to(device)
        # self.target_network.load_state_dict(self.primary_network.state_dict())
        self.primary_network = gnn.myModel(hparams,self.num_actions).to(device)
        self.target_network = gnn.myModel(hparams,self.num_actions).to(device)
        self.optimizer = optim.SGD(params=self.primary_network.parameters(), lr=hparams['learning_rate'], momentum=0.9, nesterov=True)
    def get_graph_features(self, env, copyGraph, source_nodes, destination_nodes):
        """
        提取图的特征
        env: 环境
        copyGraph: 当前图状态
        source_nodes: 源节点
        destination_nodes: 目标节点
        返回: 图的特征字典
        """
        self.bw_allocated_feature.fill(0.0)#容量
        # 归一化容量特征
        self.capacity_feature = (copyGraph[:, 0] - 100.00000001) / 200.0#利用率
        iter = 0
        for i in copyGraph[:, 1]:
            if i == 8:
                self.bw_allocated_feature[iter][0] = 1
            elif i == 32:
                self.bw_allocated_feature[iter][1] = 1
            elif i == 64:
                self.bw_allocated_feature[iter][2] = 1
            iter = iter + 1

        # 将源节点和目标节点转换为张量 - 确保它们是一维张量
        if isinstance(source_nodes, (int, float, np.integer)):
            source_tensor = torch.tensor([int(source_nodes)], dtype=torch.int64)
        else:
            source_tensor = torch.tensor(source_nodes, dtype=torch.int64)
            if source_tensor.dim() == 0:  # 如果是零维张量，转换为一维
                source_tensor = source_tensor.unsqueeze(0)
        
        if isinstance(destination_nodes, (int, float, np.integer)):
            destination_tensor = torch.tensor([int(destination_nodes)], dtype=torch.int64)
        else:
            destination_tensor = torch.tensor(destination_nodes, dtype=torch.int64)
            if destination_tensor.dim() == 0:  # 如果是零维张量，转换为一维
                destination_tensor = destination_tensor.unsqueeze(0)

        sample = {
            'num_edges': env.numEdges,
            'length': env.firstTrueSize,
            'betweenness': torch.tensor(env.between_feature, dtype=torch.float32),
            'bw_allocated': torch.tensor(self.bw_allocated_feature, dtype=torch.float32),
            'capacities': torch.tensor(self.capacity_feature, dtype=torch.float32),
            'first': torch.tensor(env.first, dtype=torch.int32),  # 确保类型一致
            'second': torch.tensor(env.second, dtype=torch.int32),  # 确保类型一致
            'source_nodes': source_tensor,  # 添加源节点张量到sample字典
            'destination_nodes': destination_tensor  # 添加目标节点张量到sample字典
        }

        sample['capacities'] = sample['capacities'][0:sample['num_edges']].view(sample['num_edges'], 1)
        sample['betweenness'] = sample['betweenness'][0:sample['num_edges']].view(sample['num_edges'], 1)
        
        # 创建源节点和目标节点的one-hot编码
        src_onehot = np.zeros(10)
        dst_onehot = np.zeros(10)
        src_onehot[source_nodes % 10] = 1
        dst_onehot[destination_nodes % 10] = 1
        
        # 将NumPy数组转换为PyTorch张量
        src_onehot_tensor = torch.tensor(src_onehot, dtype=torch.float32).unsqueeze(0).repeat(sample['num_edges'], 1)
        dst_onehot_tensor = torch.tensor(dst_onehot, dtype=torch.float32).unsqueeze(0).repeat(sample['num_edges'], 1)
        
        # 拼接所有特征
        hiddenStates = torch.cat([sample['capacities'], sample['betweenness'], sample['bw_allocated'], 
                                 src_onehot_tensor, dst_onehot_tensor], dim=1)

        paddings = (0, hparams['link_state_dim'] - 2 - hparams['num_demands'] - 2 * 10)
        link_state = torch.nn.functional.pad(hiddenStates, (0, paddings[1]), mode="constant")

        inputs = {
            'link_state': link_state, 
            'first': sample['first'][0:sample['length']],
            'second': sample['second'][0:sample['length']], 
            'num_edges': sample['num_edges'],
            'source_nodes': sample['source_nodes'],  # 从sample字典中获取源节点张量
            'destination_nodes': sample['destination_nodes']  # 从sample字典中获取目标节点张量
        }

        return inputs
    def flatten_state(self, state, demand, source, destination):
        capacities = state[:, 0] / 200.0  # 将状态中的容量进行归一化处理
        bw_allocated = state[:, 1] / max(listofDemands)   # 将状态中的已分配带宽进行归一化处理
        flat_state = np.concatenate([capacities, bw_allocated])   # 将容量和已分配带宽拼接成一个扁平化的特征向量
        demand_idx = listofDemands.index(demand) if demand in listofDemands else 0    # 获取需求在需求列表中的索引
        demand_onehot = np.zeros(len(listofDemands))    # 创建一个需求的one-hot编码向量
        demand_onehot[demand_idx] = 1
        flat_state = np.concatenate([flat_state, demand_onehot])   # 将需求的one-hot编码拼接到特征向量中
        src_onehot = np.zeros(10)   # 创建源节点的one-hot编码向量
        dst_onehot = np.zeros(10)
        src_onehot[source % 10] = 1   # 创建目标节点的one-hot编码向量
        dst_onehot[destination % 10] = 1
        flat_state = np.concatenate([flat_state, src_onehot, dst_onehot])  # 将源节点和目标节点的one-hot编码拼接到特征向量中
        if len(flat_state) < self.state_dim:   # 如果特征向量长度小于状态维度，则进行填充
            flat_state = np.pad(flat_state, (0, self.state_dim - len(flat_state)), mode='constant')
        else:   # 如果特征向量长度大于状态维度，则进行截断
            flat_state = flat_state[:self.state_dim]
        return torch.FloatTensor(flat_state).to(device)  # 将特征向量转换为PyTorch张量并移动到指定设备
    # 根据当前状态选择一个动作。
    # 训练模式下，使用epsilon-greedy策略选择动作，评估模式下，选择Q值最大的动作（也就是已经有模型参数了）。选择Q值最大的动作。
    # 返回选择的动作和扁平化的状态。
    def act(self, env, state, demand, source, destination, flagEvaluation):
        """
        根据当前状态和需求，选择一个动作（路径）
        env: 环境
        state: 当前状态
        demand: 需求量
        source: 源节点
        destination: 目的节点
        flagEvaluation: 是否为评估模式
        返回: 选择的动作（路径索引）和对应的状态特征
        """

        # 获取源-目的节点之间的K条路径
        pathList = env.allPaths[str(source) + ':' + str(destination)]
        num_paths = len(pathList)  # 获取实际可用的路径数量
        
        if num_paths == 0:
            raise ValueError(f"No paths found between source {source} and destination {destination}")

        # 是否需要计算K条路径的Q值并取最大值
        takeMax_epsilon = False
        # 存储K条路径对应的状态
        listGraphs = []
        # 存储K条路径对应的状态特征
        list_k_features = list()
        # 初始化动作
        action = 0

        path = 0

        # 根据是否为评估模式选择动作选择策略
        if flagEvaluation:
            # 评估模式，计算K条路径的Q值并选择最大值对应的路径
            takeMax_epsilon = True
        else:
            # 训练模式，使用epsilon-greedy策略选择动作
            z = np.random.random()
            if z > self.epsilon:
                # 计算K条路径的Q值并选择最大值对应的路径
                takeMax_epsilon = True
            else:
                # 随机选择一条路径
                path = np.random.randint(0, num_paths)  # 使用实际路径数量
                action = path

        # 遍历K条路径并分配需求
        while path < num_paths:  # 使用实际路径数量而不是self.K
            state_copy = np.copy(state)
            currentPath = pathList[path]
            i = 0
            j = 1

            while (j < len(currentPath)):
                state_copy[env.edgesDict[str(currentPath[i]) + ':' + str(currentPath[j])]][1] = demand
                i = i + 1
                j = j + 1

                # 使用flatten_state获取扁平化特征
                flat_features = self.flatten_state(state_copy, demand, source, destination)

                # 获取图结构信息
                graph_features = self.get_graph_features(env, state_copy, source, destination)

                # 组合特征，使用flat_features替代link_state
                features = {
                    'link_state': flat_features,  # 使用扁平化特征
                    'first': graph_features['first'],
                    'second': graph_features['second'],
                    'num_edges': graph_features['num_edges'],
                    'source_nodes': graph_features['source_nodes'],
                    'destination_nodes': graph_features['destination_nodes']
                }
                list_k_features.append(features)

            if not takeMax_epsilon:
                # 如果不需要计算K条路径的Q值，则只处理一条路径后退出
                break

            path = path + 1

        vs = [v for v in list_k_features]

        # 计算图ID和偏移量
        graph_ids = [torch.full((1,), it, dtype=torch.int64) for it, v in enumerate(vs)]  # 修改为1，因为每个flat_state是一个向量
        first_offset = cummax(vs, lambda v: v['first'])
        second_offset = cummax(vs, lambda v: v['second'])

        # 构建张量，使用flat_state替代link_state
        tensors = ({
            'graph_id': torch.cat([v for v in graph_ids], dim=0).to(device),
            'link_state': torch.stack([v['link_state'] for v in vs], dim=0).to(device),  # 使用stack而不是cat
            'first': torch.cat([v['first'] + m for v, m in zip(vs, first_offset)], dim=0).to(device),
            'second': torch.cat([v['second'] + m for v, m in zip(vs, second_offset)], dim=0).to(device),
            'num_edges': vs[0]['num_edges'],
            'source_nodes': torch.cat([v['source_nodes'].unsqueeze(0) for v in vs], dim=0).to(device),
            'destination_nodes': torch.cat([v['destination_nodes'].unsqueeze(0) for v in vs], dim=0).to(device)
        })

        # 使用主网络预测Q值，传入flat_state而不是link_state
        self.listQValues = self.primary_network(
            tensors['link_state'],  # 使用扁平化特征
            tensors['graph_id'],
            tensors['first'],
            tensors['second'],
            tensors['num_edges'],
            tensors['source_nodes'],
            tensors['destination_nodes'],
            training=False
        ).detach().cpu().numpy()

        if takeMax_epsilon:
            # 选择Q值最大的路径作为动作
            action = np.argmax(self.listQValues)
        else:
            return path, list_k_features[0]

        # if action < len(list_k_features):
        #     return action, list_k_features[action]
        # else:
        #     return len(list_k_features) - 1, list_k_features[len(list_k_features) - 1]
        if action < len(list_k_features):
            return 0, list_k_features[0]
        else:
            return 0, list_k_features[0]

    # 将经验样本添加到经验回放队列中。
    # 将当前状态、动作、奖励、下一个状态和是否结束标志存储到队列中，以便后续训练使用。
    def add_sample(self, env_training, state_action, action, reward, done, new_state, new_demand, new_source, new_destination):
        """
        添加样本到经验回放缓冲区
        env_training: 训练环境
        state_action: 当前状态特征
        action: 执行的动作
        reward: 获得的奖励
        done: 是否结束
        new_state: 新状态
        new_demand: 新需求
        new_source: 新源节点
        new_destination: 新目的节点
        """
        self.bw_allocated_feature.fill(0.0)
        new_state_copy = np.copy(new_state)

        # 确保当前状态的维度正确
        if state_action['link_state'].dim() == 1:
            state_action['link_state'] = state_action['link_state'].unsqueeze(0)
        state_action['graph_id'] = torch.full((state_action['link_state'].size(0),), 0, dtype=torch.int64)

        # 获取新源-目的节点之间的K条路径
        pathList = env_training.allPaths[str(new_source) + ':' + str(new_destination)]
        path = 0
        list_k_features = list()

        # 遍历K条路径并分配新需求
        while path < len(pathList):
            currentPath = pathList[path]
            i = 0
            j = 1

            while (j < len(currentPath)):
                new_state_copy[env_training.edgesDict[str(currentPath[i]) + ':' + str(currentPath[j])]][1] = new_demand
                i = i + 1
                j = j + 1

            # 使用flatten_state获取扁平化特征
            flat_features = self.flatten_state(new_state_copy, new_demand, new_source, new_destination)
            # 确保flat_features是二维张量
            if flat_features.dim() == 1:
                flat_features = flat_features.unsqueeze(0)
            
            # 提取路径对应的状态特征
            features = self.get_graph_features(env_training, new_state_copy, new_source, new_destination)

            # 组合特征，使用flat_features替代link_state
            features1 = {
                'link_state': flat_features,  # 使用扁平化特征
                'first': features['first'],
                'second': features['second'],
                'num_edges': features['num_edges'],
                'source_nodes': features['source_nodes'],
                'destination_nodes': features['destination_nodes']
            }
            list_k_features.append(features1)
            path = path + 1
            new_state_copy[:, 1] = 0
        
        vs = [v for v in list_k_features]

        # 计算图ID和偏移量
        graph_ids = [torch.full((v['link_state'].size(0),), it, dtype=torch.int64) for it, v in enumerate(vs)]
        first_offset = cummax(vs, lambda v: v['first'])
        second_offset = cummax(vs, lambda v: v['second'])

        # 构建next_state的tensors
        next_tensors = {
            'link_state': torch.cat([v['link_state'] for v in vs], dim=0),
            'graph_id': torch.cat([v for v in graph_ids], dim=0),
            'first': torch.cat([v['first'] + m for v, m in zip(vs, first_offset)], dim=0),
            'second': torch.cat([v['second'] + m for v, m in zip(vs, second_offset)], dim=0),
            'num_edges': sum(v['num_edges'] for v in vs),
            'source_nodes': torch.cat([v['source_nodes'] for v in vs], dim=0),
            'destination_nodes': torch.cat([v['destination_nodes'] for v in vs], dim=0)
        }

        # 将所有张量移动到正确的设备上
        state_tensors = {
            'link_state': state_action['link_state'].to(device),
            'graph_id': state_action['graph_id'].to(device),
            'first': state_action['first'].to(device),
            'second': state_action['second'].to(device),
            'num_edges': torch.tensor(state_action['num_edges'], device=device),
            'source_nodes': state_action['source_nodes'].to(device),
            'destination_nodes': state_action['destination_nodes'].to(device)
        }

        #next_tensors = {k: v.to(device) for k, v in next_tensors.items()}

        # 将样本添加到经验回放缓冲区，确保所有张量都在正确的设备上
        self.memory.append((
            state_tensors['link_state'],
            state_tensors['graph_id'],
            state_tensors['first'],
            state_tensors['second'],
            state_tensors['num_edges'],
            state_tensors['source_nodes'],
            state_tensors['destination_nodes'],
            torch.tensor(reward, dtype=torch.float32, device=device),
            next_tensors['link_state'],
            next_tensors['graph_id'],
            torch.tensor(int(done == True), dtype=torch.float32, device=device),
            next_tensors['first'],
            next_tensors['second'],
            torch.tensor(next_tensors['num_edges'], device=device),
            next_tensors['source_nodes'],
            next_tensors['destination_nodes']
        ))

    def _forward_pass(self, x):
        """
        前向传播
        x: 输入数据
        返回: 当前状态的预测Q值和目标网络的预测Q值
        """

        prediction_state = self.primary_network(x[0].to(device), x[1].to(device), x[2].to(device), x[3].to(device),
                                                x[4].to(device), x[5].to(device), x[6].to(device), training=True)
        with torch.no_grad():
            preds_next_target = self.target_network(x[8].to(device), x[9].to(device), x[11].to(device),
                                                    x[12].to(device),
                                                    x[13].to(device), x[14].to(device), x[15].to(device), training=True)
        return prediction_state, preds_next_target

    def _train_step(self, batch):
        """
        训练步骤
        batch: 一批数据
        返回: 梯度和损失
        """
        preds_state = []
        target = []

        for x in batch:
            prediction_state, preds_next_target = self._forward_pass(x)
            # 获取执行动作的Q值
            preds_state.append(prediction_state[0])
            # 计算目标Q值
            target.append(x[7] + self.gamma * torch.max(preds_next_target) * (1 - x[10]))

        # 堆叠目标和预测
        target = torch.stack(target, dim=0).unsqueeze(dim=0)
        preds_state = torch.stack(preds_state, dim=1)

        # 计算损失
        loss = F.mse_loss(preds_state, target)
        # 添加L2正则化
        regularization_loss = sum(param.norm(2) for param in self.primary_network.parameters())
        loss = loss + regularization_loss

        self.optimizer.zero_grad()
        loss.backward()
        nn.utils.clip_grad_value_(self.primary_network.parameters(), clip_value=1.0)
        self.optimizer.step()

        return [param.grad for param in self.primary_network.parameters()], loss

    def replay(self, episode):
        # 从经验回放队列中采样批次数据进行训练
        # 如果队列中的样本数量足够，则从中随机采样进行训练，并定期将primary网络的权重复制到target网络。
        if len(self.memory) < self.numbersamples:
            return
        if episode % copy_weights_interval == 0:
            for i in range(1):
                batch = random.sample(self.memory, self.numbersamples)
                grad, loss = self._train_step(batch)
                self.global_step += 1
            self.target_network.load_state_dict(self.primary_network.state_dict())
        gc.collect()

    def _write_summary(self, gradients, loss): 
        # 记录训练过程中的损失和梯度信息
        self.writer.add_scalar("loss", loss.item(), self.global_step)
        self.writer.add_histogram('gradients_0', gradients[0], self.global_step)
        self.writer.add_histogram('gradients_2', gradients[2], self.global_step)
        self.writer.add_histogram('gradients_4', gradients[4], self.global_step)
        self.writer.flush()
        self.global_step += 1
