from pathlib import Path
import tempfile
import unittest
import sys
import json
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
import configure_game


class ConfigTests(unittest.TestCase):
    def test_configures_all_packaged_endpoints(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "assets").mkdir()
            (root / "assets/config.json").write_text('{"version":"2.3.1"}')
            (root / "assets/client_settings.json").write_text('{"settings":{"network":{}}}')
            with patch.object(configure_game, "ROOT", root):
                configure_game.configure("https://game.example.com/")
            cfg = json.loads((root / "assets/config.json").read_text())
            self.assertEqual(cfg["version"], "2.3.1")
            self.assertEqual(cfg["windows_update_manifest_url"], configure_game.DEFAULT_UPDATE_MANIFEST_URL)
            settings = json.loads((root / "assets/client_settings.json").read_text())
            self.assertEqual(settings["settings"]["network"]["level_server_url"], "https://game.example.com")

    def test_allows_manifest_override(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "assets").mkdir()
            (root / "assets/config.json").write_text('{"version":"2.3.1"}')
            (root / "assets/client_settings.json").write_text('{"settings":{"network":{}}}')
            with patch.object(configure_game, "ROOT", root):
                configure_game.configure("https://game.example.com/", "https://updates.example.com/update-manifest.json")
            cfg = json.loads((root / "assets/config.json").read_text())
            self.assertEqual(cfg["windows_update_manifest_url"], "https://updates.example.com/update-manifest.json")

    def test_rejects_invalid_urls_without_writing(self):
        for url in ("ftp://host", "https://user:secret@host", "https://host/api", "https://host?q=x", "https://host#frag"):
            with self.assertRaises(ValueError):
                configure_game.configure(url)
