import tempfile
import unittest

from pc_d3qn_cli.model import CHECKPOINT_DIR
from pc_d3qn_cli.online_learn import OnlineD3QNLearner


class _StubPredictor:
    def __init__(self):
        self.last_decision_inputs = None
        self._network = None
        self._torch = None
        self._checkpoint_data = {}


class OnlineLearnLogicTest(unittest.TestCase):
    def test_reward_shaping(self):
        learner = OnlineD3QNLearner(_StubPredictor(), tempfile.mkdtemp(), latency_norm_ms=1000.0)
        self.assertAlmostEqual(learner.reward(True, 0.0), 1.0)
        self.assertAlmostEqual(learner.reward(True, 500.0), 0.5)
        self.assertAlmostEqual(learner.reward(True, 1000.0), 0.0)
        self.assertAlmostEqual(learner.reward(True, 2000.0), 0.0)   # 超过归一化上限 → 截断到 0
        self.assertEqual(learner.reward(False, None), -1.0)
        self.assertEqual(learner.reward(True, None), -1.0)          # 无延迟视为失败

    def test_record_counts_and_skips_without_inputs(self):
        pred = _StubPredictor()
        learner = OnlineD3QNLearner(pred, tempfile.mkdtemp(), interval=50)
        learner.record(True, 100.0)                  # 无缓存输入 → 计数但不入经验
        self.assertEqual(learner.counter, 1)
        self.assertEqual(len(learner.buffer), 0)
        pred.last_decision_inputs = {"selected_action": 0}
        learner.record(False, None)
        self.assertEqual(learner.counter, 2)
        self.assertEqual(len(learner.buffer), 1)
        self.assertIsNone(pred.last_decision_inputs)  # 记录后清空，避免误用到下一轮

    def test_maybe_update_noop_without_network(self):
        pred = _StubPredictor()
        learner = OnlineD3QNLearner(pred, tempfile.mkdtemp(), interval=1)
        pred.last_decision_inputs = {"selected_action": 0}
        learner.record(False, None)
        self.assertEqual(learner.maybe_update(), 0.0)  # 网络未加载 → 不更新


@unittest.skipUnless((CHECKPOINT_DIR / "best.pt").exists(), "需要 best.pt 才能跑方向性学习测试")
class OnlineLearnDirectionalTest(unittest.TestCase):
    def _topo(self):
        from pc_d3qn_cli.topology import Topology
        from pc_d3qn_cli.protocol import RssiReport, RssiNeighbor

        t = Topology(edge_direction="src_to_neighbor")
        for s in range(11):
            t.update_from_rssi_report(
                RssiReport(s, [RssiNeighbor((s + 1) % 11, -55), RssiNeighbor((s + 2) % 11, -60)]), now=1.0
            )
        return t

    def _q_selected(self, pred, inputs):
        import torch

        with torch.no_grad():
            return float(pred.q_forward(inputs)[inputs["selected_action"]])

    def test_failure_lowers_q(self):
        from pc_d3qn_cli.model import D3QNPredictor

        best = CHECKPOINT_DIR / "best.pt"
        pred = D3QNPredictor(best)
        learner = OnlineD3QNLearner(pred, tempfile.mkdtemp(), base_checkpoint=best, interval=1, epochs=5, lr=1e-3, nudge_step=1.0)
        pred.decide(self._topo(), 0, 5, 8)
        inputs = dict(pred.last_decision_inputs)
        before = self._q_selected(pred, inputs)
        learner.record(False, None)
        self.assertGreater(learner.maybe_update(), 0.0)
        self.assertLess(self._q_selected(pred, inputs), before)

    def test_success_raises_q(self):
        from pc_d3qn_cli.model import D3QNPredictor

        best = CHECKPOINT_DIR / "best.pt"
        pred = D3QNPredictor(best)
        learner = OnlineD3QNLearner(pred, tempfile.mkdtemp(), base_checkpoint=best, interval=1, epochs=5, lr=1e-3, nudge_step=1.0)
        pred.decide(self._topo(), 0, 5, 8)
        inputs = dict(pred.last_decision_inputs)
        before = self._q_selected(pred, inputs)
        learner.record(True, 120.0)
        learner.maybe_update()
        self.assertGreater(self._q_selected(pred, inputs), before)


if __name__ == "__main__":
    unittest.main()
