import socket
import tempfile
import unittest
from pathlib import Path

from pc_dijkstra_cli.launcher import create_server_auto_port


class LauncherTest(unittest.TestCase):
    def test_auto_port_uses_next_available_port(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            occupied = socket.socket()
            occupied.bind(("127.0.0.1", 0))
            occupied.listen(1)
            start_port = occupied.getsockname()[1]
            try:
                server = create_server_auto_port("127.0.0.1", start_port, Path(tmpdir), attempts=3)
                try:
                    self.assertNotEqual(server.server_port, start_port)
                    self.assertGreaterEqual(server.server_port, start_port + 1)
                finally:
                    server.server_close()
            finally:
                occupied.close()


if __name__ == "__main__":
    unittest.main()
