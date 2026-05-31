# Dijkstra PC CLI

This is a real PC-side CLI for the mesh serial protocol. It listens to dongle
serial frames, builds a directed RSSI topology, computes routes with Dijkstra,
and sends payloads using the existing `SEND` command.

## Install Dependency

```bash
python3 -m pip install pyserial
```

Offline route/protocol tests do not require `pyserial`.

## Commands

## Direct UI Launcher

The easiest way to use the upper-computer is to start the local Web UI. It opens
the browser automatically, and the page lets you choose serial port, baud rate,
nodes, packet interval, rounds, payload, RSSI collection settings, listener, and
manual send target.

Linux / WSL:

```bash
bash app/广播组网上位机/app/start_pc_dijkstra_ui.sh
```

Windows:

```bat
app\广播组网上位机\app\start_pc_dijkstra_ui.bat
```

Python fallback:

```bash
python3 app/广播组网上位机/app/start_pc_dijkstra_ui.py
```

The launcher starts at `127.0.0.1:8080`. If that port is occupied, it
automatically tries `8081`, `8082`, and later ports. Logs are written under
`app/广播组网上位机/app/logs` by default.

You can also launch through the CLI:

```bash
PYTHONPATH=app/广播组网上位机/app python3 -m pc_dijkstra_cli.main launch
```

Optional single-file executable packaging with PyInstaller:

```bash
python3 -m pip install pyinstaller
pyinstaller --onefile --name pc_dijkstra_ui app/广播组网上位机/app/start_pc_dijkstra_ui.py
```

For normal development and hardware testing, the launcher scripts above are
preferable because they keep the log paths and Python environment explicit.

## CLI Commands

```bash
python3 -m pc_dijkstra_cli.main listen --port /dev/ttyUSB0 --baud 115200
python3 -m pc_dijkstra_cli.main routes --state app/广播组网上位机/app/pc_dijkstra_cli/state.json
python3 -m pc_dijkstra_cli.main path --state app/广播组网上位机/app/pc_dijkstra_cli/state.json --src 00 --dst 12
python3 -m pc_dijkstra_cli.main send --port /dev/ttyUSB0 --state app/广播组网上位机/app/pc_dijkstra_cli/state.json --src 00 --dst 12 --payload 01020304
python3 -m pc_dijkstra_cli.main bench --port /dev/ttyUSB0 --nodes 1,2,3,4,5,6,7,8,9,10 --rounds 20 --payload AABBCC --interval 1.0 --log-dir app/广播组网上位机/app/logs/dijkstra_hw
python3 -m pc_dijkstra_cli.main sweep --port /dev/ttyUSB0 --nodes 1,2,3,4,5,6,7,8,9,10 --rounds 10 --intervals 0.3,0.5,0.8,1.0,1.5 --payload AABBCC --log-dir app/广播组网上位机/app/logs/dijkstra_hw_sweep
python3 -m pc_dijkstra_cli.main export --state app/广播组网上位机/app/pc_dijkstra_cli/state.json --routes routes.json
python3 -m pc_dijkstra_cli.main ui --host 127.0.0.1 --port 8080
python3 -m pc_dijkstra_cli.main launch
```

Run from the repository root with:

```bash
PYTHONPATH=app/广播组网上位机/app python3 -m pc_dijkstra_cli.main ...
```

## Hardware Text Mode

Current dongle firmware uses text output for hardware bring-up:

- PC sends `RSSI_REQ\r\n` to request cached neighbor information. `bench` sends it multiple times during topology collection because the dongle may return partial cached data.
- Dongle prints `RSSI_REPORT src=4 count=3 [1:-48] [3:-63] [2:-48]`.
- Dongle prints `ACK 3 2` after a target node receives data.
- PC sends only `RSSI_REQ` and `SEND`; `ACK` is not a PC command.
- `bench` computes downlink `SEND` paths with `src -> neighbor` edges, matching the observed successful paths such as `SEND 3 3 0 2 3 AABBCC`.
- Keep command spacing conservative. The default benchmark interval is `1.0s`; lower intervals can make the dongle or nodes stall and inflate packet loss.
- Use `sweep` to test a range of command intervals from fast to slow. Each interval gets its own subdirectory plus a top-level `sweep_report.md`.

`bench` stores:

- `raw_serial.log`: raw TX/RX with timestamps and hex.
- `events.jsonl`: structured RSSI, route, send, ACK, and timeout events.
- `topology_snapshots.jsonl`: topology snapshots during RSSI collection and late reports.
- `routes.json`: final Dijkstra route table.
- `rounds.jsonl`: one structured record per SEND attempt.
- `summary.json`: packet loss and latency statistics.
- `simulation_aligned_metrics.json`: simulation-style aggregate metrics such as `packet_loss_rate` and `total_delay`.
- `simulation_aligned_metrics.jsonl`: per-target and per-round simulation-style metric records.
- `hardware_test_record.json`: serial, command, firmware broadcast/scan, routing, topology, and result parameters for reproducibility.
- `report.md`: route-heavy benchmark report.
- `topology.svg`: SVG topology graph with RSSI-colored edges and highlighted target paths.
- `topology.txt`: text topology graph for papers and terminal review.
- `metrics.xlsx`: Excel-readable result table and metric table.
- `readable_report.md`: presentation-oriented report with result tables, simulation-aligned metrics, and key test parameters.

Fields that cannot be measured on current hardware are exported with
`source=default`. Real hardware fields use `source=real_rssi` or
`source=real_ack`; computed fields use `source=derived`.

## Test

```bash
PYTHONPATH=app/广播组网上位机/app python3 -m unittest discover -s app/广播组网上位机/app/pc_dijkstra_cli/tests
```
