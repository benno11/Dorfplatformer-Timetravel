import copy
from pathlib import Path
import sys
import time
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
from game_server import GameServer
import test_admin_dashboard
from updater_config import validate_config


class UpdaterTests(unittest.TestCase):
    setUp = test_admin_dashboard.AdminTests.setUp
    tearDown = test_admin_dashboard.AdminTests.tearDown
    request = test_admin_dashboard.AdminTests.request

    def configuration(self, **changes):
        return {"enabled": True, "command": [sys.executable, "-c", "print('configured')"],
                "cwd": self.temp.name, "timeout_seconds": 30,
                "manifest_url": "https://updates.example.com/manifest.json", **changes}

    def public(self, method, body=None):
        return self.request(method, "/api/server/update", body, key=self.token, public=True)

    def wait_for_update(self):
        for _ in range(100):
            status, result = self.public("GET")
            self.assertEqual(status, 200, result)
            if not result["running"]:
                return result
            time.sleep(0.05)
        self.fail("Updater did not finish")

    def test_configuration_is_local_operator_only(self):
        for route in ("/admin/api/updater", "/admin/api/updater/validate"):
            self.assertEqual(self.request("POST", route, self.configuration(), key=None)[0], 401)
            self.assertEqual(self.request("POST", route, self.configuration(), key=self.token)[0], 401)
            self.assertEqual(self.request("POST", route, self.configuration(), public=True)[0], 404)
            self.assertEqual(self.request("POST", route, self.configuration(),
                                          headers={"Origin": "https://other.example"})[0], 403)
        self.assertEqual(self.request("GET", "/admin/api/updater", key=None)[0], 401)

    def test_validation_does_not_save_or_execute(self):
        before = self.request("GET", "/admin/api/updater")[1]["config"]
        marker = Path(self.temp.name) / "must-not-run.txt"
        config = self.configuration(command=[sys.executable, "-c", f"from pathlib import Path; Path({str(marker)!r}).touch()"])
        code, result = self.request("POST", "/admin/api/updater/validate", config)
        self.assertEqual(code, 200, result)
        self.assertTrue(result["valid"])
        self.assertFalse(result["saved"])
        self.assertFalse(marker.exists())
        self.assertEqual(self.request("GET", "/admin/api/updater")[1]["config"], before)

    def test_invalid_settings_leave_configuration_unchanged(self):
        original = self.configuration()
        self.assertEqual(self.request("POST", "/admin/api/updater", original)[0], 200)
        for changes in ({"enabled": 1}, {"command": "echo unsafe"}, {"command": []},
                        {"command": [""]}, {"command": [sys.executable, None]},
                        {"command": [sys.executable, "bad\0argument"]},
                        {"command": ["no-such-executable-df-123456789"]},
                        {"cwd": str(Path(self.temp.name) / "missing")}, {"cwd": ""},
                        {"timeout_seconds": True}, {"timeout_seconds": 0}, {"timeout_seconds": 86401},
                        {"manifest_url": "file:///tmp/manifest.json"},
                        {"manifest_url": "https://user:password@example.com/manifest.json"},
                        {"manifest_url": "https://example.com:bad/manifest.json"},
                        {"manifest_url": "https://example.com/\nmanifest.json"}):
            with self.subTest(changes=changes):
                code, result = self.request("POST", "/admin/api/updater", {**original, **changes})
                self.assertEqual(code, 400, result)
        missing = copy.deepcopy(original)
        del missing["enabled"]
        self.assertEqual(self.request("POST", "/admin/api/updater", missing)[0], 400)
        self.assertEqual(self.game.get_updater_config(), validate_config(original))

    def test_save_applies_live_and_survives_restart(self):
        config = self.configuration(command=[sys.executable, "-c", "raise SystemExit(0)"])
        code, result = self.request("POST", "/admin/api/updater", config)
        self.assertEqual(code, 200, result)
        self.assertTrue(result["saved"])
        self.assertEqual(self.game.get_updater_config(), result["config"])
        self.assertTrue(self.game.get_server_update_status()["configured"])
        restarted = GameServer(("127.0.0.1", 0), self.path)
        try:
            self.assertEqual(restarted.get_updater_config(), result["config"])
        finally:
            restarted.server_close()
        self.assertEqual(self.request("GET", "/api.json", public=True)[1]["windows_update_manifest_url"], config["manifest_url"])
        with self.game.store.connect() as db:
            event = db.execute("SELECT * FROM admin_events WHERE action='configure_server_updater'").fetchone()
            self.assertEqual(event["actor"], "dashboard")

    def test_moderator_runs_fixed_command_and_can_be_disabled(self):
        marker = Path(self.temp.name) / "updated.txt"
        config = self.configuration(command=[sys.executable, "-c",
            "from pathlib import Path; Path('updated.txt').write_text('ok')"])
        self.assertEqual(self.request("POST", "/admin/api/updater", config)[0], 200)
        self.assertFalse(marker.exists())  # Saving is never execution.
        self.assertEqual(self.public("PUT", {})[0], 403)
        self.game.store.set_moderator("player", True)
        # A client payload cannot override the stored command or directory.
        code, result = self.public("PUT", {"command": ["wrong-command"], "cwd": "/wrong"})
        self.assertEqual(code, 202, result)
        self.assertEqual(self.wait_for_update()["state"], "succeeded")
        self.assertEqual(marker.read_text(), "ok")
        self.assertEqual(self.request("GET", "/admin/api/updater")[1]["last_run"]["action"], "server_update_succeeded")
        config["enabled"] = False
        self.assertEqual(self.request("POST", "/admin/api/updater", config)[0], 200)
        self.assertEqual(self.public("PUT", {})[0], 503)
        self.assertFalse(self.public("GET")[1]["configured"])

    def test_timeout_stops_command_and_reports_failure(self):
        config = self.configuration(command=[sys.executable, "-c", "import time; time.sleep(60)"], timeout_seconds=1)
        self.assertEqual(self.request("POST", "/admin/api/updater", config)[0], 200)
        self.game.store.set_moderator("player", True)
        self.assertEqual(self.public("PUT", {})[0], 202)
        result = self.wait_for_update()
        self.assertEqual(result["state"], "failed")
        self.assertIn("timeout", result["detail"])
        self.assertIsNotNone(result["finished_at"])
        self.assertNotEqual(result["exit_code"], 0)

    def test_local_manifest_is_independent_of_disabled_command(self):
        config = self.configuration(enabled=False, command=[], manifest_url="")
        self.assertEqual(self.request("POST", "/admin/api/updater", config)[0], 200)
        self.assertEqual(self.game.get_updater_config()["manifest_url"], "")
        self.assertFalse(self.game.get_server_update_status()["configured"])
        releases = Path(self.temp.name) / "releases"
        releases.mkdir()
        self.game.releases = releases
        (releases / "update-manifest.json").write_text('{"version_id": 123}')
        self.assertEqual(self.request("GET", "/update-manifest.json", public=True), (200, {"version_id": 123}))

    def test_run_uses_snapshot_when_settings_change(self):
        original = validate_config(self.configuration())
        self.assertEqual(self.request("POST", "/admin/api/updater", original)[0], 200)
        self.game.store.set_moderator("player", True)
        with patch.object(self.game, "_run_server_update") as worker:
            self.assertEqual(self.public("PUT", {})[0], 202)
            for _ in range(100):
                if worker.called:
                    break
                time.sleep(0.01)
            self.assertEqual(worker.call_args.args[1], original)
            self.assertEqual(self.public("PUT", {})[0], 202)
            self.assertEqual(worker.call_count, 1)
            replacement = self.configuration(enabled=False, command=[])
            self.assertEqual(self.request("POST", "/admin/api/updater", replacement)[0], 200)
            self.assertEqual(worker.call_args.args[1], original)
