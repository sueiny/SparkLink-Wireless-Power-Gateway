import unittest

from pc_dijkstra_cli.protocol import (
    Ack,
    ProtocolError,
    RssiNeighbor,
    RssiReport,
    build_send_command,
    parse_ack,
    parse_frame,
    parse_rssi_report,
    parse_text_message,
)


class ProtocolTest(unittest.TestCase):
    def test_parse_rssi_report_with_int8_values(self):
        report = parse_rssi_report(bytes.fromhex("4D530101120205D308C4"))

        self.assertEqual(report.src_addr, 0x12)
        self.assertEqual([(item.addr, item.rssi) for item in report.neighbors], [(0x05, -45), (0x08, -60)])

    def test_parse_ack_big_endian_seq(self):
        ack = parse_ack(bytes.fromhex("4D53010512000034"))

        self.assertEqual(ack, Ack(src_addr=0x12, dst_addr=0x00, seq=0x0034))

    def test_parse_frame_dispatch(self):
        message = parse_frame(bytes.fromhex("4D530101120105D3"))

        self.assertIsInstance(message, RssiReport)

    def test_reject_invalid_rssi_length(self):
        with self.assertRaises(ProtocolError):
            parse_rssi_report(bytes.fromhex("4D530101120205D3"))

    def test_build_send_command(self):
        command = build_send_command(0x12, [0x05, 0x08, 0x12], "01 02 03 04")

        self.assertEqual(command, "SEND 12 3 05 08 12 01020304\r\n")

    def test_build_send_command_rejects_wrong_destination(self):
        with self.assertRaises(ProtocolError):
            build_send_command(0x12, [0x05, 0x08], "0102")

    def test_parse_text_ack(self):
        ack = parse_text_message("ACK 3 2\r\n")

        self.assertEqual(ack, Ack(src_addr=0x03, dst_addr=0x00, seq=2))

    def test_parse_text_rssi_report(self):
        report = parse_text_message("RSSI_REPORT src=4 count=3 [1:-48] [3:-63] [2:-48]\r\n")

        self.assertEqual(report, RssiReport(0x04, [RssiNeighbor(0x01, -48), RssiNeighbor(0x03, -63), RssiNeighbor(0x02, -48)]))

    def test_parse_text_ignores_unrelated_log(self):
        self.assertIsNone(parse_text_message("APP|[SYS INFO] mem: used:1\r\n"))


if __name__ == "__main__":
    unittest.main()
