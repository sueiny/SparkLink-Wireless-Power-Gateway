# D3QN_MPNN 独立上位机

这个包是独立的 D3QN 上位机实现，不使用 Dijkstra 作为路由 fallback。D3QN 无法给出路径时，本轮按 `d3qn_route_failed` 计入失败。

## 安装依赖

```bash
cd /home/sueiny/rk3506_linux6.1_v1.2.0
bash app/广播组网上位机/app/setup_d3qn_env.sh
```

## 完整训练

```bash
PYTHONPATH='app/广播组网上位机/app' \
app/广播组网上位机/app/.venv-d3qn/bin/python -m pc_d3qn_cli.main train
```

默认训练参数对齐 sample：

- `iterations=2000`
- `training_episodes=20`
- `evaluation_episodes=20`
- `K=4`

## 快速训练

用于先把 checkpoint 和硬件测试链路跑通，不等同于完整训练模型：

```bash
PYTHONPATH='app/广播组网上位机/app' \
app/广播组网上位机/app/.venv-d3qn/bin/python -m pc_d3qn_cli.main train-fast
```

默认快速参数：

- `iterations=40`
- `training_episodes=3`
- `evaluation_episodes=1`
- `first_work_train_episode=5`
- `graph_num_nodes=11`，对应真实测试的网关 `00` 加 10 个节点
- `graph_target_edges=24`

只验证 checkpoint 写入时可用：

```bash
PYTHONPATH='app/广播组网上位机/app' \
app/广播组网上位机/app/.venv-d3qn/bin/python -m pc_d3qn_cli.main train-smoke
```

训练完成后生成：

- `app/广播组网上位机/app/checkpoints/d3qn_mpnn/latest.pt`
- `app/广播组网上位机/app/checkpoints/d3qn_mpnn/best.pt`
- `app/广播组网上位机/app/checkpoints/d3qn_mpnn/training_config.json`

## 硬件测试

```bash
PYTHONPATH='app/广播组网上位机/app' \
app/广播组网上位机/app/.venv-d3qn/bin/python -m pc_d3qn_cli.main bench \
  --port /dev/ttyUSB0 \
  --baud 115200 \
  --nodes 1,2,3,4,5,6,7,8,9,10 \
  --rounds 20 \
  --payload AABBCC \
  --interval 0.5 \
  --ack-timeout 2.0 \
  --checkpoint app/广播组网上位机/app/checkpoints/d3qn_mpnn/latest.pt
```

日志输出在 `app/广播组网上位机/app/logs/d3qn_hw/第N次测试/`，包含中文 Markdown、Excel、拓扑图和原始 JSON。
