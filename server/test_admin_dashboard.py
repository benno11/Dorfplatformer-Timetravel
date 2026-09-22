import http.client
import json
import os
from pathlib import Path
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
from game_server import GameServer, ApiError
from admin_dashboard import AdminServer, load_admin_key, dashboard_state


class AdminTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.path = Path(self.temp.name) / "game.sqlite3"
        self.key = "local-test-key-" + "x" * 40
        self.game = GameServer(("127.0.0.1", 0), self.path)
        self.admin = AdminServer(("127.0.0.1", 0), self.path, self.key)
        self.threads = [threading.Thread(target=s.serve_forever, daemon=True) for s in (self.game, self.admin)]
        for thread in self.threads:
            thread.start()
        self.account = self.game.store.auth("register",
            {"email": "player@example.com", "displayName": "player", "password": "strong-password"}, "")
        self.user_id = self.account["localId"]
        self.token = self.account["idToken"]
        self.payload = {"name": "Test", "data": "level data"}
        self.game.store.levels("PUT", "test", False, False, self.payload, self.token)

    def tearDown(self):
        for server in (self.admin, self.game):
            server.shutdown()
            server.server_close()
        for thread in self.threads:
            thread.join()
        self.temp.cleanup()

    def request(self, method, path, body=None, key="default", headers=None, public=False):
        connection = http.client.HTTPConnection("127.0.0.1", (self.game if public else self.admin).server_port, timeout=10)
        actual_key = self.key if key == "default" else key
        request_headers = {"Content-Type": "application/json"}
        if actual_key:
            request_headers["Authorization"] = "Bearer " + actual_key
        request_headers.update(headers or {})
        connection.request(method, path, None if body is None else json.dumps(body), request_headers)
        response = connection.getresponse()
        code, data = response.status, response.read()
        connection.close()
        try:
            return code, json.loads(data)
        except ValueError:
            return code, data

    def action(self, action, **fields):
        return self.request("POST", "/admin/api/" + action, {"user_id": self.user_id, "reason": "Test reason", **fields})

    def test_version_blacklist_enforcement_and_recovery(self):
        body = {"version_id": 27, "enabled": True, "reason": "Broken level uploads"}
        self.assertEqual(self.request("POST", "/admin/api/version-block", body, key=None)[0], 401)
        self.assertEqual(self.request("POST", "/admin/api/version-block", body, public=True)[0], 404)
        self.assertEqual(self.request("POST", "/admin/api/version-block", body)[0], 200)
        headers = {"User-Agent": "DF-New/2.3.1 (build/27)"}
        for method, path, payload in [("GET", "/levels.json", None),
                ("GET", "/levels/test/data.json", None), ("GET", "/api.json", None),
                ("POST", "/api/auth/login", {}), ("POST", "/api/auth/lookup", {}),
                ("PUT", "/levels/test.json", self.payload), ("DELETE", "/levels/test.json", None),
                ("PUT", "/levels/test/difficulty", {"difficulty": 3})]:
            code, result = self.request(method, path, payload, key=self.token, headers=headers, public=True)
            self.assertEqual(code, 426, path)
            self.assertIn(body["reason"], result["error"]["message"])
        for ua in ["DF-New/2.3.2 (build/28)", "Mozilla/5.0", "DF-New/1.0"]:
            self.assertEqual(self.request("GET", "/levels.json", headers={"User-Agent": ua}, public=True)[0], 200)
        for path in ["/", "/health"]:
            self.assertEqual(self.request("GET", path, headers=headers, public=True)[0], 200)
        # Update distribution remains reachable rather than being blocked by client version policy.
        self.assertEqual(self.request("GET", "/update-manifest.json", headers=headers, public=True)[0], 302)
        self.assertEqual(self.request("GET", "/releases/game.apk", headers=headers, public=True)[0], 404)
        self.assertEqual(self.request("GET", "/admin/api/state", headers=headers)[0], 200)
        from game_server import Store
        with self.assertRaises(ApiError) as error:
            Store(self.path).check_client_version(headers["User-Agent"])
        self.assertEqual(error.exception.status, 426)
        state = self.request("GET", "/admin/api/state")[1]
        self.assertEqual(state["blocked_versions"][0]["version_id"], "27")
        self.assertEqual(state["events"][0]["action"], "block_version")
        body["enabled"] = False
        self.assertEqual(self.request("POST", "/admin/api/version-block", body)[0], 200)
        self.assertEqual(self.request("GET", "/levels.json", headers=headers, public=True)[0], 200)
        self.assertEqual(self.request("GET", "/admin/api/state")[1]["events"][0]["action"], "unblock_version")

    def test_version_blacklist_validation_and_legacy(self):
        body = {"version_id": 27, "enabled": True, "reason": "Update required"}
        for value in [None, True, 0, -1, 2100000001, 2.5, "27", [], {}]:
            self.assertEqual(self.request("POST", "/admin/api/version-block", {**body, "version_id": value})[0], 400)
        for change in [{"enabled": 1}, {"enabled": None}, {"reason": ""}]:
            self.assertEqual(self.request("POST", "/admin/api/version-block", {**body, **change})[0], 400)
        self.assertEqual(self.request("POST", "/admin/api/version-block", {**body, "version_id": "legacy"})[0], 200)
        for ua in ["DF-New/1.0", "DF-New/1.0-android", "DF-New/invalid"]:
            self.assertEqual(self.request("GET", "/levels.json", headers={"User-Agent": ua}, public=True)[0], 426)
        self.assertEqual(self.request("GET", "/levels.json", headers={"User-Agent": "DF-New/2.3.1 (build/27)"}, public=True)[0], 200)

    def test_dashboard_shell_and_authenticated_state(self):
        code, html = self.request("GET", "/", key=None)
        self.assertEqual(code, 200)
        self.assertIn(b"Community dashboard", html)
        self.assertEqual(self.request("GET", "/admin/api/state", key=None)[0], 401)
        self.assertEqual(self.request("GET", "/admin/api/state", key="incorrect")[0], 401)
        code, state = self.request("GET", "/admin/api/state")
        self.assertEqual(code, 200)
        self.assertEqual(state["stats"]["players"], 1)
        self.assertNotIn("password", json.dumps(state))
        self.assertNotIn(self.key, json.dumps(state))
        self.assertEqual(self.request("GET", "/admin/api/level?id=test")[1]["data"], "level data")

    def test_private_routes_are_not_on_public_server(self):
        for route in ("/admin/api/state", "/admin.js", "/admin/api/level?id=test"):
            self.assertEqual(self.request("GET", route, public=True)[0], 404)
        self.assertEqual(self.request("POST", "/admin/api/moderator",
            {"user_id": self.user_id, "enabled": True, "reason": "Unauthorized"}, public=True)[0], 404)
        self.assertFalse(self.game.store.auth("lookup", {}, self.token)["users"][0]["isModerator"])

    def test_rebinding_and_cross_origin_requests_blocked(self):
        self.assertEqual(self.request("GET", "/", headers={"Host": "evil.example"})[0], 403)
        self.assertEqual(self.request("GET", "/admin/api/state", headers={"Origin": "https://evil.example"})[0], 403)
        self.assertEqual(self.action("moderator", enabled=True)[0], 200)
        self.assertEqual(self.request("POST", "/admin/api/moderator",
            {"user_id": self.user_id, "enabled": False, "reason": "Wrong origin"},
            headers={"Sec-Fetch-Site": "cross-site"})[0], 403)
        self.assertTrue(self.game.store.auth("lookup", {}, self.token)["users"][0]["isModerator"])

    def test_no_non_loopback_bind(self):
        with self.assertRaises(ValueError):
            AdminServer(("0.0.0.0", 0), self.path, self.key)

    def test_grant_and_revoke_moderator(self):
        self.assertEqual(self.action("moderator", enabled=True)[0], 200)
        self.assertTrue(self.game.store.auth("lookup", {}, self.token)["users"][0]["isModerator"])
        self.assertEqual(self.action("moderator", enabled=False)[0], 200)
        self.assertFalse(self.game.store.auth("lookup", {}, self.token)["users"][0]["isModerator"])
        _, state = self.request("GET", "/admin/api/state")
        self.assertEqual([e["action"] for e in state["events"]], ["revoke_moderator", "grant_moderator"])

    def test_ratings_still_require_a_moderator_session(self):
        payload = {"level_id": "test", "difficulty": 8}
        self.assertEqual(self.request("POST", "/admin/api/rate", payload)[0], 401)
        self.assertEqual(self.request("POST", "/admin/api/rate", payload, headers={"X-Moderator-Token": self.token})[0], 403)
        self.action("moderator", enabled=True)
        for difficulty in (0, 10, True, "5"):
            self.assertEqual(self.request("POST", "/admin/api/rate", {**payload, "difficulty": difficulty},
                                         headers={"X-Moderator-Token": self.token})[0], 400)
        self.assertEqual(self.request("POST", "/admin/api/rate", payload, headers={"X-Moderator-Token": self.token})[0], 200)
        self.assertEqual(self.game.store.levels("GET", "test", False, False, {}, "")["difficulty"], 8)
        self.action("moderator", enabled=False)
        self.assertEqual(self.request("POST", "/admin/api/rate", payload, headers={"X-Moderator-Token": self.token})[0], 403)

    def test_moderator_login_and_logout(self):
        credentials = {"email": "player@example.com", "password": "strong-password"}
        self.assertEqual(self.request("POST", "/admin/api/moderator-login", credentials)[0], 403)
        self.action("moderator", enabled=True)
        code, session = self.request("POST", "/admin/api/moderator-login", credentials)
        self.assertEqual(code, 200)
        self.assertEqual(self.request("POST", "/admin/api/moderator-logout", {},
                                     headers={"X-Moderator-Token": session["idToken"]})[0], 200)
        with self.assertRaises(ApiError):
            self.game.store.auth("lookup", {}, session["idToken"])

    def test_suspension_blocks_new_uploads_and_edits_but_not_play(self):
        self.assertEqual(self.action("suspension", hours=24)[0], 200)
        for level_id in ("new", "test"):
            self.assertEqual(self.request("PUT", "/levels/" + level_id + ".json", self.payload,
                                         key=self.token, public=True)[0], 403)
        self.assertEqual(self.request("GET", "/levels/test/data.json", public=True)[0], 200)
        account = self.game.store.auth("lookup", {}, self.token)["users"][0]
        self.assertEqual(account["uploadRestriction"]["reason"], "Test reason")
        self.assertGreater(account["uploadRestriction"]["until"], time.time())
        self.assertEqual(self.action("suspension", hours=0)[0], 200)
        self.assertEqual(self.request("PUT", "/levels/new.json", self.payload, key=self.token, public=True)[0], 200)

    def test_permanent_suspension_persists_and_temporary_expires(self):
        self.action("suspension", hours=None)
        self.assertIsNone(self.game.store.auth("lookup", {}, self.token)["users"][0]["uploadRestriction"]["until"])
        with self.game.store.connect() as db:
            db.execute("UPDATE upload_blocks SET until=?", (int(time.time()) - 1,))
        self.assertIsNone(self.game.store.auth("lookup", {}, self.token)["users"][0]["uploadRestriction"])
        self.assertEqual(self.request("PUT", "/levels/new.json", self.payload, key=self.token, public=True)[0], 200)

    def test_warning_is_visible_without_suspension(self):
        self.assertEqual(self.action("warn")[0], 200)
        user = self.game.store.auth("lookup", {}, self.token)["users"][0]
        self.assertEqual(user["warning"]["reason"], "Test reason")
        self.assertIsNone(user["uploadRestriction"])
        self.assertEqual(self.request("PUT", "/levels/new.json", self.payload, key=self.token, public=True)[0], 200)

    def test_admin_deletes_any_level_with_audit_and_cascades(self):
        self.action("moderator", enabled=True)
        self.game.store.rate_difficulty("test", {"difficulty": 4}, self.token)
        self.assertEqual(self.action("delete-level", level_id="test")[0], 200)
        self.assertEqual(self.request("GET", "/levels/test.json", public=True)[0], 404)
        with self.game.store.connect() as db:
            self.assertEqual(db.execute("SELECT count(*) FROM level_difficulties").fetchone()[0], 0)
            self.assertEqual(db.execute("SELECT action FROM admin_events ORDER BY id DESC LIMIT 1").fetchone()[0], "delete-level")

    def test_invalid_actions_do_not_mutate_data(self):
        for body in ({"hours": -1}, {"hours": True}, {"hours": 9000}, {"reason": ""}):
            self.assertEqual(self.action("suspension", **body)[0], 400)
        self.assertEqual(self.action("moderator", enabled="true")[0], 400)
        self.assertEqual(self.action("delete-level", level_id="missing")[0], 404)
        self.assertEqual(self.action("moderator", user_id="missing", enabled=True)[0], 404)
        self.assertIsNone(self.game.store.auth("lookup", {}, self.token)["users"][0]["uploadRestriction"])

    def test_search_pagination_and_static_allowlist(self):
        state = dashboard_state(self.game.store, {"q": ["player"]})
        self.assertEqual(state["users_total"], 1)
        self.assertEqual(dashboard_state(self.game.store, {"q": ["%"]})["users_total"], 0)
        self.assertEqual(self.request("GET", "/admin/api/state?users_offset=-1")[0], 400)
        for url in ("/../game_server.py", "/admin-key", "/game.sqlite3"):
            self.assertEqual(self.request("GET", url)[0], 404)


class KeyTests(unittest.TestCase):
    def test_key_is_generated_once_and_env_override_validated(self):
        with tempfile.TemporaryDirectory() as directory, patch.dict(os.environ, {"ADMIN_KEY": ""}):
            path = Path(directory) / "admin-key"
            first = load_admin_key(path)
            self.assertGreaterEqual(len(first), 32)
            self.assertEqual(load_admin_key(path), first)
            with patch.dict(os.environ, {"ADMIN_KEY": "short"}):
                with self.assertRaises(ValueError):
                    load_admin_key(path)
