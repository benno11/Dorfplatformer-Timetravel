import http.client
from pathlib import Path
import queue
import re
import subprocess
import sys
import tempfile
import threading
import unittest


class StartupTests(unittest.TestCase):
    def test_game_command_starts_both_local_services(self):
        with tempfile.TemporaryDirectory() as directory:
            db = Path(directory) / "game.sqlite3"
            process = subprocess.Popen([sys.executable, str(Path(__file__).with_name("game_server.py")),
                "--database", str(db), "--port", "0", "--admin-port", "0"],
                stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
            output = queue.Queue()
            def read_output():
                for line in process.stderr:
                    output.put(line)
            reader = threading.Thread(target=read_output, daemon=True)
            reader.start()
            try:
                game_port = admin_port = None
                for _ in range(10):
                    line = output.get(timeout=10)
                    game = re.search(r"Game server listening on 127\.0\.0\.1:(\d+)", line)
                    admin = re.search(r"Admin dashboard: http://127\.0\.0\.1:(\d+)", line)
                    if game:
                        game_port = int(game[1])
                    if admin:
                        admin_port = int(admin[1])
                    if game_port and admin_port:
                        break
                self.assertIsNotNone(game_port)
                self.assertIsNotNone(admin_port)
                connection = http.client.HTTPConnection("127.0.0.1", game_port, timeout=5)
                connection.request("GET", "/health")
                response = connection.getresponse()
                self.assertEqual(response.status, 200)
                response.read()
                connection.close()
                key = (Path(directory) / "admin-key").read_text().strip()
                connection = http.client.HTTPConnection("127.0.0.1", admin_port, timeout=5)
                connection.request("GET", "/admin/api/state", headers={"Authorization": "Bearer " + key})
                response = connection.getresponse()
                self.assertEqual(response.status, 200)
                response.read()
                connection.close()
            finally:
                process.terminate()
                process.wait(timeout=10)
                reader.join(timeout=5)
                process.stderr.close()
