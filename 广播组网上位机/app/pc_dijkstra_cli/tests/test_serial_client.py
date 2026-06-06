import unittest

from pc_dijkstra_cli.protocol import Ack, RssiReport
from pc_dijkstra_cli.serial_client import MixedMessageReader


class MixedMessageReaderRobustnessTest(unittest.TestCase):
    def test_bad_binary_frame_does_not_drop_co_resident_frames(self):
        # [合法 RSSI 帧][脏帧: MS + 不支持的类型 0xFF][合法 ACK 帧]
        good_rssi = bytes.fromhex("4D530101120105D3")  # src=0x12, 邻居 0x05@-45
        garbage = bytes.fromhex("4D5301FF")            # 不支持的帧类型 → 旧实现会毒死整批
        good_ack = bytes.fromhex("4D53010512000034")   # src=0x12 dst=0x00 seq=0x34

        reader = MixedMessageReader()
        messages = reader.feed(good_rssi + garbage + good_ack)

        kinds = [type(m).__name__ for m in messages]
        self.assertIn("RssiReport", kinds)
        self.assertIn("Ack", kinds)
        rssi = next(m for m in messages if isinstance(m, RssiReport))
        ack = next(m for m in messages if isinstance(m, Ack))
        self.assertEqual(rssi.src_addr, 0x12)
        self.assertEqual(ack.seq, 0x34)

    def test_bad_text_line_does_not_drop_following_good_line(self):
        # 第一行 count=3 但只有 1 个邻居 → ProtocolError；不应丢掉第二行合法报告
        chunk = b"RSSI_REPORT src=4 count=3 [1:-48]\r\nRSSI_REPORT src=5 count=1 [2:-50]\r\n"

        reader = MixedMessageReader()
        messages = reader.feed(chunk)

        reports = [m for m in messages if isinstance(m, RssiReport)]
        self.assertEqual(len(reports), 1)
        self.assertEqual(reports[0].src_addr, 0x05)


if __name__ == "__main__":
    unittest.main()
