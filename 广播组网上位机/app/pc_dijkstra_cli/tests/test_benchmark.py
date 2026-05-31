import tempfile
import unittest
import zipfile
from pathlib import Path

from pc_dijkstra_cli.benchmark import (
    RoundResult,
    build_hardware_test_record,
    build_readable_report,
    build_simulation_aligned_metrics,
    build_simulation_aligned_rows,
    demand_for_payload,
    enrich_summary_with_rssi,
    enrich_summary_with_metrics,
    select_best_optimization_result,
    summarize,
    write_report,
    write_excel_summary,
    write_text_topology,
)
from pc_dijkstra_cli.routing import rssi_to_reliable_weight
from pc_dijkstra_cli.defaults import default_simulation_params
from pc_dijkstra_cli.protocol import RssiNeighbor, RssiReport
from pc_dijkstra_cli.topology_svg import build_topology_svg, rssi_color
from pc_dijkstra_cli.topology import Topology


class BenchmarkTest(unittest.TestCase):
    def test_summarize_counts_loss_and_latency(self):
        summary = summarize(
            [
                RoundResult(1, 1, [0, 1], 1, "SEND 1 2 00 01 AABBCC", True, 10.0, 0),
                RoundResult(1, 2, [0, 1], 1, "SEND 1 2 00 01 AABBCC", False, None, None),
                RoundResult(2, 1, [0, 2], 1, "SEND 2 2 00 02 AABBCC", True, 20.0, 1),
            ],
            [1, 2],
        )

        self.assertEqual(summary["total"]["sent"], 3)
        self.assertEqual(summary["total"]["success"], 2)
        self.assertAlmostEqual(summary["total"]["loss_rate"], 1 / 3)
        self.assertEqual(summary["targets"]["01"]["lost"], 1)
        self.assertEqual(summary["targets"]["02"]["latency"]["avg_ms"], 20.0)

    def test_summarize_excludes_unreachable_from_ack_loss(self):
        summary = summarize(
            [
                RoundResult(1, 1, [0, 1], 1, "SEND 1 2 00 01 AABBCC", True, 10.0, 0),
                RoundResult(2, 1, [], float("inf"), "", False, None, None, status="unreachable"),
            ],
            [1, 2],
        )

        self.assertEqual(summary["total"]["planned_rounds"], 2)
        self.assertEqual(summary["total"]["sent"], 1)
        self.assertEqual(summary["total"]["route_failed"], 1)
        self.assertEqual(summary["total"]["ack_timeout_loss"], 0)
        self.assertEqual(summary["total"]["loss_rate"], 0.0)

    def test_write_report_creates_markdown(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            topology = Topology(stale_seconds=None)
            topology.update_from_rssi_report(RssiReport(1, [RssiNeighbor(0, -45)]), now=1.0)
            summary = summarize(
                [RoundResult(1, 1, [0, 1], 1, "SEND 1 2 00 01 AABBCC", True, 10.0, 0)],
                [1],
            )

            write_report(Path(tmpdir), summary, topology)

            self.assertIn("Dijkstra Hardware Benchmark Report", (Path(tmpdir) / "report.md").read_text(encoding="utf-8"))

    def test_demand_for_payload_uses_default_bins(self):
        self.assertEqual(demand_for_payload("AABBCC"), 8)
        self.assertEqual(demand_for_payload("00" * 20), 32)
        self.assertEqual(demand_for_payload("00" * 80), 64)

    def test_default_params_are_marked_default(self):
        params = default_simulation_params()

        self.assertEqual(params["humidity"]["source"], "default")
        self.assertEqual(params["capacity"]["source"], "default")

    def test_enrich_summary_adds_path_rssi(self):
        topology = Topology(stale_seconds=None, edge_direction="src_to_neighbor")
        topology.update_from_rssi_report(RssiReport(0, [RssiNeighbor(1, -45)]), now=1.0)
        summary = summarize(
            [RoundResult(1, 1, [0, 1], 1, "SEND 1 2 00 01 AABBCC", True, 10.0, 0)],
            [1],
        )

        enrich_summary_with_rssi(summary, topology)

        self.assertEqual(summary["targets"]["01"]["path_rssi"]["min_rssi"], -45)
        self.assertEqual(summary["targets"]["01"]["path_rssi"]["source"], "real_rssi")

    def test_simulation_aligned_metrics_marks_real_and_default_sources(self):
        results = [
            RoundResult(1, 1, [0, 1], 1, "SEND 1 2 00 01 AABBCC", True, 100.0, 1),
            RoundResult(1, 2, [0, 1], 1, "SEND 1 2 00 01 AABBCC", False, None, None),
        ]
        summary = summarize(results, [1])

        metrics = build_simulation_aligned_metrics(summary, "logs/test")
        rows = build_simulation_aligned_rows(summary, results, ack_timeout=2.0)

        self.assertEqual(metrics["schema"], "simulation_aligned_metrics.v1")
        self.assertEqual(metrics["metrics"]["packet_loss_rate"]["source"], "real_ack")
        self.assertEqual(metrics["metrics"]["link_utilization"]["source"], "default")
        self.assertEqual(rows[-1]["sources"]["total_delay"], "ack_timeout")

    def test_hardware_test_record_contains_firmware_params(self):
        topology = Topology(stale_seconds=None, edge_direction="src_to_neighbor")
        topology.update_from_rssi_report(RssiReport(0, [RssiNeighbor(1, -45)]), now=1.0)
        summary = summarize(
            [RoundResult(1, 1, [0, 1], 1, "SEND 1 2 00 01 AABBCC", True, 10.0, 0)],
            [1],
        )

        record = build_hardware_test_record(
            summary=summary,
            topology=topology,
            port="/dev/ttyUSB0",
            baud=115200,
            nodes=[1],
            rounds=1,
            payload="AABBCC",
            log_dir="logs/test",
            boot_wait=5.0,
            rssi_seconds=8.0,
            rssi_requests=5,
            ack_timeout=2.0,
            interval=1.0,
            gateway=0,
        )

        self.assertEqual(record["schema"], "hardware_test_record.v1")
        self.assertEqual(record["firmware_params"]["advertising"]["actual_announce_tx_power"], 20)
        self.assertEqual(record["test_config"]["commands"]["command_interval_s"], 1.0)

    def test_build_readable_report_includes_tables(self):
        topology = Topology(stale_seconds=None, edge_direction="src_to_neighbor")
        topology.update_from_rssi_report(RssiReport(0, [RssiNeighbor(1, -45)]), now=1.0)
        summary = summarize(
            [RoundResult(1, 1, [0, 1], 1, "SEND 1 2 00 01 AABBCC", True, 10.0, 0)],
            [1],
        )
        enrich_summary_with_rssi(summary, topology)
        aligned = build_simulation_aligned_metrics(summary, "logs/test")
        record = build_hardware_test_record(
            summary=summary,
            topology=topology,
            port="/dev/ttyUSB0",
            baud=115200,
            nodes=[1],
            rounds=1,
            payload="AABBCC",
            log_dir="logs/test",
            boot_wait=5.0,
            rssi_seconds=8.0,
            rssi_requests=5,
            ack_timeout=2.0,
            interval=0.5,
            gateway=0,
        )

        report = build_readable_report(summary, record, aligned, "logs/test")

        self.assertIn("Dijkstra 真实硬件测试汇总报告", report)
        self.assertIn("丢包率目标", report)
        self.assertIn("ACK timeout：`2.0s`", report)
        self.assertIn("是否达标", report)
        self.assertIn("算法模式", report)
        self.assertIn("| `01` | `00 -> 01` | `1/1` |", report)
        self.assertIn("![Dijkstra RSSI 拓扑图](拓扑图.svg)", report)
        self.assertIn("文本拓扑文件", report)
        self.assertIn("Excel 汇总文件", report)
        self.assertIn("算法计算延时", report)
        self.assertIn("指标总结对比", report)
        self.assertIn("完整指标汇总", report)
        self.assertIn("端到端实际传输平均延时", report)
        self.assertIn("RSSI 实时波动", report)
        self.assertIn("命令间隔 | `0.5s`", report)

    def test_topology_svg_contains_edges_and_highlight(self):
        topology = Topology(stale_seconds=None, edge_direction="src_to_neighbor")
        topology.update_from_rssi_report(RssiReport(0, [RssiNeighbor(1, -45), RssiNeighbor(2, -80)]), now=1.0)
        summary = summarize(
            [RoundResult(1, 1, [0, 1], 1, "SEND 1 2 00 01 AABBCC", True, 10.0, 0)],
            [1],
        )

        svg = build_topology_svg(topology, summary)

        self.assertIn("<svg", svg)
        self.assertIn("Dijkstra RSSI Layered Topology", svg)
        self.assertIn("优  -45dBm  w=1", svg)
        self.assertIn("#111827", svg)
        self.assertNotIn("marker-end", svg)
        self.assertNotIn("<marker", svg)
        self.assertEqual(rssi_color(-80), "#c53030")

    def test_reliable_weight_penalizes_weak_links(self):
        self.assertEqual(rssi_to_reliable_weight(-55), 1.0)
        self.assertEqual(rssi_to_reliable_weight(-80), 16.0)
        self.assertEqual(rssi_to_reliable_weight(-85), 32.0)
        self.assertIsNone(rssi_to_reliable_weight(-86))

    def test_reliable_route_avoids_weak_link_when_alternative_exists(self):
        topology = Topology(stale_seconds=None, edge_direction="src_to_neighbor")
        topology.update_from_rssi_report(RssiReport(0, [RssiNeighbor(1, -80), RssiNeighbor(2, -75)]), now=1.0)
        topology.update_from_rssi_report(RssiReport(2, [RssiNeighbor(1, -75)]), now=1.0)

        baseline = topology.route(0, 1, route_mode="baseline_dijkstra")
        reliable = topology.route(0, 1, route_mode="reliable_dijkstra_v1")

        self.assertEqual(baseline.path, [0, 1])
        self.assertEqual(reliable.path, [0, 2, 1])

    def test_select_best_optimization_result_prefers_target_range(self):
        best = select_best_optimization_result(
            [
                {"loss_rate": 0.03, "in_target": False, "latency": {"avg_ms": 20}},
                {"loss_rate": 0.05, "in_target": True, "latency": {"avg_ms": 30}},
                {"loss_rate": 0.055, "in_target": True, "latency": {"avg_ms": 25}},
            ]
        )

        self.assertEqual(best["loss_rate"], 0.055)

    def test_text_topology_and_excel_are_written(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            topology = Topology(stale_seconds=None, edge_direction="src_to_neighbor")
            topology.update_from_rssi_report(RssiReport(0, [RssiNeighbor(1, -45)]), now=1.0)
            summary = summarize(
                [RoundResult(1, 1, [0, 1], 1, "SEND 1 2 00 01 AABBCC", True, 10.0, 0)],
                [1],
            )
            enrich_summary_with_rssi(summary, topology)
            summary["rounds"] = [
                {
                    "target": 1,
                    "success": True,
                    "latency_ms": 10.0,
                }
            ]
            enrich_summary_with_metrics(summary, topology, route_compute_ms=0.5)

            write_text_topology(root / "拓扑图.txt", topology, summary)
            write_excel_summary(root / "测试指标汇总.xlsx", summary, topology, {"nodes": [1]})

            self.assertIn("00 -> 01", (root / "拓扑图.txt").read_text(encoding="utf-8"))
            self.assertGreater((root / "测试指标汇总.xlsx").stat().st_size, 1000)
            with zipfile.ZipFile(root / "测试指标汇总.xlsx") as archive:
                workbook = archive.read("xl/workbook.xml").decode("utf-8")
                self.assertIn("完整指标汇总", workbook)
                self.assertIn("优化参数对比", workbook)


if __name__ == "__main__":
    unittest.main()
