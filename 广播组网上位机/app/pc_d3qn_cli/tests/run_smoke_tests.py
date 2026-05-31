from __future__ import annotations

from pathlib import Path
import tempfile

from pc_d3qn_cli.benchmark import RoundResult, summarize
from pc_d3qn_cli.model import D3QNPredictor
from pc_d3qn_cli.protocol import Ack, RssiNeighbor, RssiReport, build_send_command, parse_text_message
from pc_d3qn_cli.state import build_d3qn_state, k_candidate_paths
from pc_d3qn_cli.topology import Topology


def main() -> int:
    message = parse_text_message("RSSI_REPORT src=4 count=3 [1:-48] [3:-63] [2:-84]\r\n")
    assert isinstance(message, RssiReport)
    assert message.src_addr == 0x04
    assert message.neighbors[2].rssi == -84

    ack = parse_text_message("ACK 3 2\n")
    assert isinstance(ack, Ack)
    assert ack.src_addr == 0x03
    assert ack.seq == 2

    assert build_send_command(0x02, [0x00, 0x01, 0x02], "AABBCC") == "SEND 02 3 00 01 02 aabbcc\r\n"

    topology = Topology(edge_direction="src_to_neighbor")
    topology.update_from_rssi_report(RssiReport(0x01, [RssiNeighbor(0x02, -50)]), now=1.0)
    state = build_d3qn_state(topology)
    assert "01:02" in state["edgesDict"]
    assert state["edge_features"][0]["packet_loss"]["source"] == "default"

    graph = {0: {1: 1, 2: 1, 3: 1}, 1: {4: 1}, 2: {4: 1}, 3: {4: 1}}
    assert len(k_candidate_paths(graph, 0, 4, 2)) == 2

    with tempfile.TemporaryDirectory() as tmp:
        predictor = D3QNPredictor(Path(tmp) / "missing.pt")
        decision = predictor.decide(topology, 0x01, 0x02, 8)
        assert decision.status == "model_unavailable"
        assert "checkpoint not found" in (decision.error or "")

    summary = summarize(
        [
            RoundResult(1, 2, 1, [1, 2], "SEND 02 2 01 02 aabbcc", True, 10.0, 1, "AABBCC", 8, None, None, 0.5, "success", None, 0, 1, [1.0]),
            RoundResult(1, 3, 1, [], "", False, None, None, "AABBCC", 8, None, None, 0.5, "d3qn_route_failed", "no candidate path", None, 0, []),
        ],
        [1, 2, 3],
        0,
        sources=[1],
    )
    assert summary["total"]["planned_rounds"] == 2
    assert summary["total"]["sent"] == 1
    assert summary["total"]["route_failed"] == 1
    assert summary["total"]["loss_rate"] == 0.0

    print("pc_d3qn_cli smoke tests OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
