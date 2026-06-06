import unittest

from pc_dijkstra_cli.protocol import RssiNeighbor, RssiReport
from pc_dijkstra_cli.routing import dijkstra, rssi_to_weight
from pc_dijkstra_cli.topology import Topology


class RoutingTopologyTest(unittest.TestCase):
    def test_rssi_to_weight_segments(self):
        self.assertEqual(rssi_to_weight(-55), 4)
        self.assertEqual(rssi_to_weight(-56), 5)
        self.assertEqual(rssi_to_weight(-66), 6)
        self.assertEqual(rssi_to_weight(-76), 12)
        self.assertIsNone(rssi_to_weight(-86))

    def test_rssi_report_builds_directed_edges(self):
        topology = Topology()

        topology.update_from_rssi_report(RssiReport(src_addr=0x12, neighbors=[RssiNeighbor(0x05, -45)]), now=1.0)

        graph = topology.graph(now=2.0)
        self.assertEqual(graph, {0x05: {0x12: 4}})
        self.assertNotIn(0x05, graph.get(0x12, {}))

    def test_dijkstra_multihop(self):
        graph = {
            0x00: {0x05: 3, 0x12: 12},
            0x05: {0x12: 3},
        }

        route = dijkstra(graph, 0x00, 0x12)

        self.assertEqual(route.status, "valid")
        self.assertEqual(route.path, [0x00, 0x05, 0x12])
        self.assertEqual(route.cost, 6)

    def test_unreachable(self):
        route = dijkstra({0x00: {0x05: 1}}, 0x05, 0x00)

        self.assertEqual(route.status, "unreachable")
        self.assertEqual(route.path, [])

    def test_weak_links_are_filtered(self):
        topology = Topology()

        topology.update_from_rssi_report(RssiReport(src_addr=0x12, neighbors=[RssiNeighbor(0x05, -90)]), now=1.0)

        self.assertEqual(topology.graph(now=2.0), {})


if __name__ == "__main__":
    unittest.main()

