import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from pc_dijkstra_cli.main import main
from pc_dijkstra_cli.protocol import RssiNeighbor, RssiReport
from pc_dijkstra_cli.topology import Topology, save_topology


class FakeSerial:
    writes = []
    in_waiting = 0

    def __init__(self, port, baudrate, timeout, **kwargs):
        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout
        self.dtr = True
        self.rts = True
        self.in_waiting = 0

    def read(self, size):
        return b""

    def write(self, data):
        self.writes.append(data)

    def flush(self):
        pass

    def close(self):
        pass


class CliTest(unittest.TestCase):
    def test_path_command_uses_saved_state(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            state_path = Path(tmpdir) / "state.json"
            topology = Topology(stale_seconds=None)
            topology.update_from_rssi_report(RssiReport(0x05, [RssiNeighbor(0x00, -60)]), now=1.0)
            topology.update_from_rssi_report(RssiReport(0x12, [RssiNeighbor(0x05, -60)]), now=1.0)
            save_topology(state_path, topology)

            self.assertEqual(main(["path", "--state", str(state_path), "--src", "00", "--dst", "0x12"]), 0)

    def test_export_command_writes_routes(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            state_path = Path(tmpdir) / "state.json"
            routes_path = Path(tmpdir) / "routes.json"
            topology = Topology(stale_seconds=None)
            topology.update_from_rssi_report(RssiReport(0x12, [RssiNeighbor(0x00, -45)]), now=1.0)
            save_topology(state_path, topology)

            self.assertEqual(main(["export", "--state", str(state_path), "--routes", str(routes_path)]), 0)
            exported = json.loads(routes_path.read_text(encoding="utf-8"))
            self.assertIn("00:12", exported["routes"])

    def test_send_command_writes_serial_command(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            state_path = Path(tmpdir) / "state.json"
            topology = Topology(stale_seconds=None)
            topology.update_from_rssi_report(RssiReport(0x12, [RssiNeighbor(0x00, -45)]), now=1.0)
            save_topology(state_path, topology)

            FakeSerial.writes = []
            with patch.dict("sys.modules", {"serial": type("SerialModule", (), {"Serial": FakeSerial})}):
                rc = main(
                    [
                        "send",
                        "--port",
                        "FAKE",
                        "--state",
                        str(state_path),
                        "--src",
                        "00",
                        "--dst",
                        "0x12",
                        "--payload",
                        "0102",
                    ]
                )

            self.assertEqual(rc, 0)
            # 目标 0x12=18 渲染为十进制，路径字节用十六进制(00 12)
            self.assertEqual(FakeSerial.writes, [b"SEND 18 2 00 12 0102\r\n"])

    def test_bench_command_writes_report(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            log_dir = Path(tmpdir) / "logs"
            with patch("pc_dijkstra_cli.main.run_benchmark") as run_benchmark:
                run_benchmark.return_value = {"total": {"sent": 1, "success": 1, "loss_rate": 0.0}}
                rc = main([
                    "bench",
                    "--port",
                    "FAKE",
                    "--nodes",
                    "1,2",
                    "--rounds",
                    "3",
                    "--log-dir",
                    str(log_dir),
                ])

            self.assertEqual(rc, 0)
            run_benchmark.assert_called_once()
            self.assertEqual(run_benchmark.call_args.kwargs["nodes"], [1, 2])
            self.assertEqual(run_benchmark.call_args.kwargs["rounds"], 3)
            self.assertEqual(run_benchmark.call_args.kwargs["rssi_requests"], 10)
            self.assertEqual(run_benchmark.call_args.kwargs["rssi_seconds"], 20.0)
            self.assertEqual(run_benchmark.call_args.kwargs["interval"], 1.0)
            self.assertEqual(run_benchmark.call_args.kwargs["route_mode"], "baseline_dijkstra")

    def test_sweep_command_runs_interval_sweep(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            log_dir = Path(tmpdir) / "sweep"
            with patch("pc_dijkstra_cli.main.run_interval_sweep") as run_sweep:
                run_sweep.return_value = {
                    "results": [
                        {"interval": 0.3, "sent": 1, "success": 1, "loss_rate": 0.0},
                        {"interval": 1.0, "sent": 1, "success": 1, "loss_rate": 0.0},
                    ]
                }
                rc = main([
                    "sweep",
                    "--port",
                    "FAKE",
                    "--nodes",
                    "1,2",
                    "--rounds",
                    "2",
                    "--intervals",
                    "0.3,1.0",
                    "--log-dir",
                    str(log_dir),
                ])

            self.assertEqual(rc, 0)
            run_sweep.assert_called_once()
            self.assertEqual(run_sweep.call_args.kwargs["intervals"], [0.3, 1.0])

    def test_optimize_command_runs_optimization_sweep(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            log_dir = Path(tmpdir) / "opt"
            with patch("pc_dijkstra_cli.main.run_optimization_sweep") as run_opt:
                run_opt.return_value = {
                    "best": {
                        "route_mode": "reliable_dijkstra_v1",
                        "interval": 1.0,
                        "rssi_requests": 8,
                        "sent": 1,
                        "success": 1,
                        "loss_rate": 0.05,
                        "in_target": True,
                    }
                }
                rc = main([
                    "optimize",
                    "--port",
                    "FAKE",
                    "--nodes",
                    "1,2",
                    "--rounds",
                    "2",
                    "--intervals",
                    "0.5,1.0",
                    "--route-modes",
                    "baseline_dijkstra,reliable_dijkstra_v1",
                    "--rssi-requests-values",
                    "5,8",
                    "--log-dir",
                    str(log_dir),
                ])

            self.assertEqual(rc, 0)
            run_opt.assert_called_once()
            self.assertEqual(run_opt.call_args.kwargs["rssi_requests_values"], [5, 8])
            self.assertEqual(run_opt.call_args.kwargs["route_modes"], ["baseline_dijkstra", "reliable_dijkstra_v1"])

    def test_ui_command_starts_server(self):
        with patch("pc_dijkstra_cli.main.serve_ui") as serve_ui:
            rc = main(["ui", "--host", "127.0.0.1", "--port", "0", "--log-root", "logs"])

        self.assertEqual(rc, 0)
        serve_ui.assert_called_once_with(host="127.0.0.1", port=0, log_root="logs")

    def test_launch_command_opens_ui_launcher(self):
        with patch("pc_dijkstra_cli.main.launch_ui") as launch_ui:
            rc = main([
                "launch",
                "--host",
                "127.0.0.1",
                "--port",
                "0",
                "--log-root",
                "logs",
                "--no-browser",
                "--port-attempts",
                "3",
            ])

        self.assertEqual(rc, 0)
        launch_ui.assert_called_once_with(
            host="127.0.0.1",
            port=0,
            log_root="logs",
            open_browser=False,
            attempts=3,
        )


if __name__ == "__main__":
    unittest.main()
