from __future__ import annotations

from pc_d3qn_cli.model import D3QNPredictor
from pc_d3qn_cli.protocol import Ack, RssiNeighbor, RssiReport, build_send_command, parse_text_message
from pc_d3qn_cli.state import build_d3qn_state, k_candidate_paths
from pc_d3qn_cli.topology import Topology


def test_parse_text_rssi_report():
    message = parse_text_message("RSSI_REPORT src=4 count=3 [1:-48] [3:-63] [2:-84]\r\n")

    assert isinstance(message, RssiReport)
    assert message.src_addr == 0x04
    assert message.neighbors == [
        RssiNeighbor(addr=0x01, rssi=-48),
        RssiNeighbor(addr=0x03, rssi=-63),
        RssiNeighbor(addr=0x02, rssi=-84),
    ]


def test_parse_text_ack():
    message = parse_text_message("ACK 3 2\n")

    assert isinstance(message, Ack)
    assert message.src_addr == 0x03
    assert message.seq == 2


def test_send_command_format():
    assert build_send_command(0x02, [0x00, 0x01, 0x02], "AABBCC") == "SEND 2 3 00 01 02 aabbcc\r\n"


def test_topology_and_d3qn_state_uses_src_to_neighbor():
    topology = Topology(edge_direction="src_to_neighbor")
    topology.update_from_rssi_report(RssiReport(0x01, [RssiNeighbor(0x02, -50)]), now=1.0)

    state = build_d3qn_state(topology)

    # 与 sample 环境一致的无向图模型：edgesDict 两个方向都登记并指向同一条边索引
    assert "01:02" in state["edgesDict"]
    assert "02:01" in state["edgesDict"]
    assert state["edgesDict"]["01:02"] == state["edgesDict"]["02:01"]
    # edge_features 为唯一无向边(小节点在前)，来源为真实测量，无单独反向特征
    feats = {(f["src"], f["dst"]): f for f in state["edge_features"]}
    assert (0x01, 0x02) in feats
    assert (0x02, 0x01) not in feats
    assert feats[(0x01, 0x02)]["rssi"]["source"] == "real_rssi"
    assert feats[(0x01, 0x02)]["packet_loss"]["source"] == "default"


def test_candidate_paths_limited_to_k():
    graph = {
        0: {1: 1, 2: 1, 3: 1},
        1: {4: 1},
        2: {4: 1},
        3: {4: 1},
    }

    paths = k_candidate_paths(graph, 0, 4, 2)

    assert len(paths) == 2
    assert all(path[0] == 0 and path[-1] == 4 for path in paths)


def test_missing_checkpoint_is_d3qn_failure_not_fallback(tmp_path):
    topology = Topology(edge_direction="src_to_neighbor")
    topology.update_from_rssi_report(RssiReport(0x00, [RssiNeighbor(0x01, -50)]), now=1.0)
    predictor = D3QNPredictor(tmp_path / "missing.pt")

    decision = predictor.decide(topology, 0x00, 0x01, 8)

    assert decision.status == "model_unavailable"
    assert decision.selected_path == []
    assert "checkpoint not found" in (decision.error or "")


def test_no_candidate_path_is_unreachable_without_model_load(tmp_path):
    topology = Topology(edge_direction="src_to_neighbor")
    topology.nodes.update({0x00, 0x02})
    predictor = D3QNPredictor(tmp_path / "missing.pt")

    decision = predictor.decide(topology, 0x00, 0x02, 8)

    assert decision.status == "unreachable"
    assert decision.selected_path == []
