"""测试期在线学习器（自包含，不依赖 gym 训练环境）。

设计（经与用户确认）：
- 框架：bandit 单步。每一轮 SEND 就是一次「选路→ACK 奖励」，目标 Q = 即时奖励（无自举、无 target 网络）。
  好处：最稳，且天然实现适应性——某节点被移走后原路径超时→其 Q 被压低→下一次该 (src,dst) 自动选次优候选。
- 奖励：延迟塑形。成功 = 1 - 归一化延迟（越快越高）；超时 = 负奖励。
- 探索：无（贪心由 decide() 的 argmax 负责；本模块只负责学习）。
- checkpoint：以传入的基线（建议 best.pt）为起点 fine-tune，更新存到独立文件 online_latest.pt / online_best.pt，
  绝不覆盖 latest.pt / best.pt。
- 计时：每次更新耗时由调用方计入该轮 inference 时间。

每 `interval` 轮触发一次更新：对最近窗口内的经验做 `epochs` 轮小步梯度，保存 checkpoint。
"""
from __future__ import annotations

import random
from collections import deque
from pathlib import Path
from time import perf_counter


class OnlineD3QNLearner:
    def __init__(
        self,
        predictor,
        save_dir: str | Path,
        *,
        base_checkpoint: str | Path | None = None,
        interval: int = 50,
        lr: float = 1e-4,
        epochs: int = 2,
        buffer_size: int | None = None,
        latency_norm_ms: float = 5000.0,
        fail_reward: float = -1.0,
        nudge_step: float = 0.5,
        logger=None,
    ):
        self.predictor = predictor
        self.save_dir = Path(save_dir)
        self.base_checkpoint = str(base_checkpoint) if base_checkpoint else None
        self.interval = max(1, int(interval))
        self.lr = float(lr)
        self.epochs = max(1, int(epochs))
        self.latency_norm_ms = max(1.0, float(latency_norm_ms))
        self.fail_reward = float(fail_reward)
        self.nudge_step = float(nudge_step)
        self.logger = logger

        # 默认只保留最近 interval 个窗口：更新耗时恒定，且只学最近经验（节点移动后适应更快）
        self.buffer: deque = deque(maxlen=int(buffer_size) if buffer_size else self.interval)
        self.counter = 0          # 累计已记录的传输轮次（全局，不是 per-pair）
        self._update_count = 0
        self._optimizer = None
        self._best_window_reward = float("-inf")
        self._recent_rewards: deque = deque(maxlen=self.interval)

    # ---- 奖励 ----
    def reward(self, success: bool, latency_ms: float | None) -> float:
        if not success or latency_ms is None:
            return self.fail_reward
        return 1.0 - min(max(float(latency_ms), 0.0) / self.latency_norm_ms, 1.0)

    # ---- 记录一轮经验 ----
    def record(self, success: bool, latency_ms: float | None) -> None:
        """在一轮 SEND 出结果后调用。复用 predictor 上一次 decide() 缓存的输入。"""
        inputs = getattr(self.predictor, "last_decision_inputs", None)
        self.counter += 1
        if inputs is None:
            return  # 该轮没有有效 decide（不可达/模型不可用等），不计入学习
        r = self.reward(success, latency_ms)
        self.buffer.append({"inputs": inputs, "reward": r})
        self._recent_rewards.append(r)
        # 记录后清空缓存，避免误用到下一轮
        self.predictor.last_decision_inputs = None

    # ---- 是否到点更新 ----
    def maybe_update(self) -> float:
        """返回本次更新耗时(ms)；未到更新点或无法更新时返回 0。耗时由调用方计入 inference 时间。"""
        if self.counter == 0 or self.counter % self.interval != 0:
            return 0.0
        if not self.buffer or self.predictor._network is None or self.predictor._torch is None:
            return 0.0
        t0 = perf_counter()
        mean_loss = self._do_update()
        self._update_count += 1
        self.predictor._online_update_count = self._update_count
        window_reward = sum(self._recent_rewards) / len(self._recent_rewards) if self._recent_rewards else float("-inf")
        latest_path = self._save("online_latest.pt", mean_loss, window_reward)
        promoted = False
        if window_reward > self._best_window_reward:
            self._best_window_reward = window_reward
            self._save("online_best.pt", mean_loss, window_reward)
            promoted = True
        elapsed_ms = (perf_counter() - t0) * 1000.0
        if self.logger is not None:
            self.logger.event(
                "d3qn_online_update",
                update_index=self._update_count,
                rounds_seen=self.counter,
                buffer_size=len(self.buffer),
                mean_loss=mean_loss,
                window_reward=window_reward,
                promoted_best=promoted,
                update_ms=elapsed_ms,
                checkpoint=str(latest_path),
            )
        return elapsed_ms

    # ---- 梯度更新（bandit 单步：Q[选中动作] 回归到即时奖励）----
    def _do_update(self) -> float:
        torch = self.predictor._torch
        net = self.predictor._network
        self._ensure_optimizer()
        opt = self._optimizer
        net.train()
        losses = []
        try:
            for _ in range(self.epochs):
                batch = list(self.buffer)
                random.shuffle(batch)
                for exp in batch:
                    inputs = exp["inputs"]
                    action = int(inputs["selected_action"])
                    q = self.predictor.q_forward(inputs)
                    if action >= q.shape[0]:
                        continue
                    q_sel = q[action]
                    # 方向性目标：把选中动作的 Q 朝「当前值 + 步长×奖励」推。
                    # 奖励>0(成功且快)→上推；奖励<0(超时)→下压。与 Q 绝对尺度无关，方向恒正确。
                    target = (q_sel.detach() + self.nudge_step * float(exp["reward"]))
                    loss = torch.nn.functional.smooth_l1_loss(q_sel, target)
                    opt.zero_grad()
                    loss.backward()
                    opt.step()
                    losses.append(float(loss.detach().cpu()))
        finally:
            net.eval()  # 恢复推理模式，供后续 decide()
        return sum(losses) / len(losses) if losses else float("nan")

    def _ensure_optimizer(self) -> None:
        if self._optimizer is not None:
            return
        torch = self.predictor._torch
        # 全新 Adam + 小学习率：在线 fine-tune 不沿用预训练的动量估计，避免大步长破坏 1000+ 轮成果
        self._optimizer = torch.optim.Adam(self.predictor._network.parameters(), lr=self.lr)

    # ---- 保存独立 checkpoint（克隆基线元数据，仅替换权重）----
    def _save(self, name: str, mean_loss: float, window_reward: float) -> Path:
        torch = self.predictor._torch
        payload = dict(self.predictor._checkpoint_data or {})
        payload["model_state_dict"] = self.predictor._network.state_dict()
        if self._optimizer is not None:
            payload["optimizer_state_dict"] = self._optimizer.state_dict()
        payload["online_meta"] = {
            "base_checkpoint": self.base_checkpoint,
            "updates": self._update_count,
            "rounds_seen": self.counter,
            "mean_loss": mean_loss,
            "window_reward": window_reward,
            "interval": self.interval,
            "lr": self.lr,
            "epochs": self.epochs,
            "framing": "bandit_1step",
        }
        self.save_dir.mkdir(parents=True, exist_ok=True)
        path = self.save_dir / name
        torch.save(payload, path)
        return path
