import json
import tempfile
import unittest
from pathlib import Path
from urllib.error import HTTPError
from urllib.request import Request, urlopen

from pc_dijkstra_cli.benchmark import (
    RoundResult,
    build_hardware_test_record,
    build_simulation_aligned_metrics,
    enrich_summary_with_rssi,
    summarize,
    write_readable_report,
)
from pc_dijkstra_cli.topology import Topology, save_topology
from pc_dijkstra_cli.topology import routes_to_dict
from pc_dijkstra_cli.protocol import RssiNeighbor, RssiReport
from pc_dijkstra_cli.topology_svg import write_topology_svg
from pc_dijkstra_cli.ui_server import create_server


class UiServerTest(unittest.TestCase):
    def _make_run(self, root: Path) -> Path:
        run_dir = root / "dijkstra_hw" / "sample_run"
        run_dir.mkdir(parents=True)
        topology = Topology(stale_seconds=None, edge_direction="src_to_neighbor")
        topology.update_from_rssi_report(RssiReport(0, [RssiNeighbor(1, -45)]), now=1.0)
        summary = summarize(
            [RoundResult(1, 1, [0, 1], 1, "SEND 1 2 00 01 AABBCC", True, 10.0, 0)],
            [1],
        )
        enrich_summary_with_rssi(summary, topology)
        summary["config"] = {
            "nodes": [1],
            "interval": 0.5,
            "port": "/dev/ttyUSB0",
            "baud": 115200,
            "rounds": 1,
            "payload": "AABBCC",
            "gateway": 0,
            "ack_timeout": 2.0,
            "rssi_seconds": 8.0,
            "rssi_requests": 5,
        }
        json_dir = run_dir / "原始JSON数据"
        json_dir.mkdir()
        save_topology(json_dir / "state.json", topology)
        (json_dir / "summary.json").write_text(json.dumps(summary), encoding="utf-8")
        (json_dir / "routes.json").write_text(json.dumps(routes_to_dict(topology.routes())), encoding="utf-8")
        aligned = build_simulation_aligned_metrics(summary, run_dir)
        (json_dir / "simulation_aligned_metrics.json").write_text(json.dumps(aligned), encoding="utf-8")
        record = build_hardware_test_record(
            summary=summary,
            topology=topology,
            port="/dev/ttyUSB0",
            baud=115200,
            nodes=[1],
            rounds=1,
            payload="AABBCC",
            log_dir=run_dir,
            boot_wait=5.0,
            rssi_seconds=8.0,
            rssi_requests=5,
            ack_timeout=2.0,
            interval=0.5,
            gateway=0,
        )
        (json_dir / "hardware_test_record.json").write_text(json.dumps(record), encoding="utf-8")
        write_topology_svg(run_dir / "拓扑图.svg", topology, summary)
        write_readable_report(run_dir, summary, record, aligned)
        return run_dir

    def test_runs_api_and_svg(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            root = Path(tmpdir)
            self._make_run(root)
            server = create_server("127.0.0.1", 0, root)
            try:
                port = server.server_port
                import threading

                thread = threading.Thread(target=server.serve_forever, daemon=True)
                thread.start()

                runs = self._get_json(port, "/api/runs")
                self.assertEqual(runs["runs"][0]["id"], "dijkstra_hw/sample_run")

                run = self._get_json(port, "/api/runs/dijkstra_hw/sample_run")
                self.assertIn("summary", run)
                self.assertIn("topology", run)

                svg = self._get_text(port, "/api/runs/dijkstra_hw/sample_run/topology.svg")
                self.assertIn("<svg", svg)

                report = self._get_text(port, "/api/runs/dijkstra_hw/sample_run/file/%E6%B5%8B%E8%AF%95%E7%BB%93%E6%9E%9C%E6%B1%87%E6%8A%A5.md")
                self.assertIn("Dijkstra 真实硬件测试汇总报告", report)

                status = self._get_json(port, "/api/bench/status")
                self.assertEqual(status["status"], "idle")
            finally:
                server.shutdown()
                server.server_close()

    def _get_json(self, port: int, path: str):
        return json.loads(self._get_text(port, path))

    def _get_text(self, port: int, path: str) -> str:
        with urlopen(f"http://127.0.0.1:{port}{path}", timeout=5) as response:
            return response.read().decode("utf-8")


if __name__ == "__main__":
    unittest.main()
