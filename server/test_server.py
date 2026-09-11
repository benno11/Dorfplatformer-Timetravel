import concurrent.futures
import http.client
import json
from pathlib import Path
import sqlite3
import sys
import tempfile
import threading
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from game_server import GameServer, Store, MAX_BODY


class ServerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.database = Path(self.temp.name) / "game.sqlite3"
        self.releases = Path(self.temp.name) / "releases"
        self.releases.mkdir()
        self.server = GameServer(("127.0.0.1", 0), self.database, releases=self.releases)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()
        self.temp.cleanup()

    def request(self, method, path, data=None, token=None, raw=None, headers=None):
        connection = http.client.HTTPConnection("127.0.0.1", self.server.server_port, timeout=10)
        request_headers = {"Content-Type": "application/json"}
        if token:
            request_headers["Authorization"] = "Bearer " + token
        request_headers.update(headers or {})
        body = raw if raw is not None else json.dumps(data) if data is not None else None
        connection.request(method, path, body, request_headers)
        response = connection.getresponse()
        status, payload = response.status, response.read()
        connection.close()
        try:
            payload = json.loads(payload)
        except ValueError:
            pass
        return status, payload

    def register(self, name="player"):
        status, result = self.request("POST", "/api/auth/register", {
            "email": name + "@example.com", "displayName": name, "password": "strong-password-123"})
        self.assertEqual(status, 200, result)
        return result

    def test_register_login_lookup_logout(self):
        account = self.register()
        token = account["idToken"]
        self.assertEqual(self.request("POST", "/api/auth/lookup", {}, token)[1]["users"][0]["displayName"], "player")
        self.assertEqual(self.request("POST", "/api/auth/login",
            {"email": "player@example.com", "password": "wrong-password"})[0], 401)
        self.assertEqual(self.request("POST", "/api/auth/login",
            {"email": "player@example.com", "password": "strong-password-123"})[0], 200)
        self.assertEqual(self.request("POST", "/api/auth/logout", {}, token)[0], 200)
        self.assertEqual(self.request("POST", "/api/auth/lookup", {}, token)[0], 401)

    def test_unique_accounts_and_password_validation(self):
        self.register()
        for body, expected in [
            ({"email": "PLAYER@example.com", "displayName": "other", "password": "strong-password-123"}, 409),
            ({"email": "other@example.com", "displayName": "PLAYER", "password": "strong-password-123"}, 409),
            ({"email": "other@example.com", "displayName": "other", "password": "short"}, 400),
            ({"email": "other@example.com", "displayName": "../bad", "password": "strong-password-123"}, 400),
            ({"email": "bad", "displayName": "other", "password": "strong-password-123"}, 400)]:
            self.assertEqual(self.request("POST", "/api/auth/register", body)[0], expected)

    def test_level_lifecycle_and_server_owned_fields(self):
        token = self.register()["idToken"]
        data = {"name": "My level", "data": "level text\n1 2 3", "owner": "forged", "uploaded_at": 0}
        self.assertEqual(self.request("PUT", "/levels/player-demo.json", data)[0], 401)
        self.assertEqual(self.request("PUT", "/levels/player-demo.json", data, token)[0], 200)
        status, level = self.request("GET", "/levels/player-demo.json")
        self.assertEqual(status, 200)
        self.assertEqual(level["owner"], "player")
        self.assertGreater(level["uploaded_at"], 0)
        self.assertEqual(self.request("GET", "/levels/player-demo/data.json")[1], data["data"])
        self.assertEqual(self.request("GET", "/levels.json?shallow=true")[1], {"player-demo": True})
        self.assertEqual(self.request("GET", "/levels.json")[1]["player-demo"]["name"], "My level")
        self.assertEqual(self.request("DELETE", "/levels/player-demo.json", token=token)[0], 200)
        self.assertEqual(self.request("GET", "/levels/player-demo.json")[0], 404)

    def test_cross_account_overwrite_and_delete_rejected(self):
        a, b = self.register("first")["idToken"], self.register("second")["idToken"]
        data = {"name": "test", "data": "hello"}
        self.assertEqual(self.request("PUT", "/levels/shared.json", data, a)[0], 200)
        self.assertEqual(self.request("PUT", "/levels/shared.json", data, b)[0], 403)
        self.assertEqual(self.request("DELETE", "/levels/shared.json", token=b)[0], 403)

    def test_concurrent_claim(self):
        tokens = [self.register("first")["idToken"], self.register("second")["idToken"]]
        with concurrent.futures.ThreadPoolExecutor() as executor:
            statuses = list(executor.map(lambda token: self.request("PUT", "/levels/race.json",
                            {"name": "race", "data": "hello"}, token)[0], tokens))
        self.assertEqual(sorted(statuses), [200, 403])

    def test_update_requires_password_and_revokes_sessions(self):
        old = self.register()["idToken"]
        self.assertEqual(self.request("POST", "/api/auth/update", {"displayName": "renamed"}, old)[0], 401)
        status, updated = self.request("POST", "/api/auth/update", {
            "displayName": "renamed", "password": "another-password", "currentPassword": "strong-password-123"}, old)
        self.assertEqual(status, 200)
        self.assertEqual(updated["displayName"], "renamed")
        self.assertEqual(self.request("POST", "/api/auth/lookup", {}, old)[0], 401)
        self.assertEqual(self.request("POST", "/api/auth/lookup", {}, updated["idToken"])[0], 200)

    def test_rename_preserves_level_ownership(self):
        old = self.register()["idToken"]
        self.request("PUT", "/levels/original.json", {"name": "original", "data": "hello"}, old)
        _, new = self.request("POST", "/api/auth/update",
            {"displayName": "renamed", "currentPassword": "strong-password-123"}, old)
        self.assertEqual(self.request("GET", "/levels/original.json")[1]["owner"], "renamed")
        self.assertEqual(self.request("DELETE", "/levels/original.json", token=new["idToken"])[0], 200)

    def test_expiry_and_persistence(self):
        account = self.register()
        token = account["idToken"]
        self.request("PUT", "/levels/persistent.json", {"name": "test", "data": "saved"}, token)
        other_store = Store(self.database)
        self.assertEqual(other_store.levels("GET", "persistent", True, False, {}, "") , "saved")
        with other_store.connect() as db:
            user = other_store.authenticate(db, token)
            self.assertNotEqual(user["password"], "strong-password-123")
            self.assertNotEqual(db.execute("SELECT token_hash FROM sessions").fetchone()[0], token)
            db.execute("UPDATE sessions SET expires=0")
        self.assertEqual(self.request("POST", "/api/auth/lookup", {}, token)[0], 401)

    def test_invalid_requests_and_private_files(self):
        for raw in ("{", "[]", '{"email":NaN}'):
            self.assertEqual(self.request("POST", "/api/auth/register", raw=raw)[0], 400)
        self.assertEqual(self.request("POST", "/api/auth/login", raw="{}",
                                      headers={"Content-Type": "text/plain"})[0], 415)
        for path in ("/../server/data/game.sqlite3", "/server/data/game.sqlite3", "/.env", "/levels/../oops.json"):
            self.assertEqual(self.request("GET", path)[0], 404)
        self.assertEqual(self.request("POST", "/api/auth/login", raw="", headers={"Content-Length": str(MAX_BODY + 1)})[0], 413)

    def test_release_files(self):
        self.assertEqual(self.request("GET", "/update-manifest.json")[0], 404)
        manifest = {"version": "2.3.1", "version_id": 27, "installer_url": "https://game.example.com/releases/setup.exe"}
        (self.releases / "update-manifest.json").write_text(json.dumps(manifest))
        (self.releases / "setup.exe").write_bytes(b"test-installer")
        (self.releases / "private.txt").write_text("private")
        self.assertEqual(self.request("GET", "/update-manifest.json")[1], manifest)
        self.assertEqual(self.request("GET", "/releases/setup.exe"), (200, b"test-installer"))
        self.assertEqual(self.request("GET", "/releases/private.txt")[0], 404)
        self.assertEqual(self.request("GET", "/releases/../setup.exe")[0], 404)

    def test_web_and_health(self):
        self.assertEqual(self.request("GET", "/health")[1]["status"], "ok")
        self.assertIn(b"currentPassword", self.request("GET", "/")[1])
        self.assertIn(b"/api/auth/", self.request("GET", "/account-manager.js")[1])
        self.assertNotIn("firebase", json.dumps(self.request("GET", "/api.json")[1]).lower())

    def test_throttling(self):
        for _ in range(20):
            self.assertEqual(self.request("POST", "/api/auth/login", {})[0], 400)
        self.assertEqual(self.request("POST", "/api/auth/login", {})[0], 429)

    def test_query_tokens_are_not_accepted(self):
        token = self.register()["idToken"]
        self.assertEqual(self.request("PUT", "/levels/test.json?auth=" + token,
                                     {"name": "test", "data": "hello"})[0], 401)


if __name__ == "__main__":
    unittest.main()
