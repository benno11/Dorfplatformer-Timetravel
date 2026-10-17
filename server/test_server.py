import concurrent.futures
import http.client
import json
from pathlib import Path
import sqlite3
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
from game_server import GameServer, Store, MAX_BODY, DEFAULT_UPDATE_MANIFEST_URL


class ServerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.database = Path(self.temp.name) / "game.sqlite3"
        self.releases = Path(self.temp.name) / "releases"
        self.releases.mkdir()
        self.server = GameServer(("127.0.0.1", 0), self.database, releases=self.releases)
        # Exercise per-level compatibility independently of the global floor.
        with self.server.store.connect() as db:
            db.execute("UPDATE client_version_policy SET minimum_build=0 WHERE id=1")
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
        self.assertEqual(level["downloads"], 0)
        self.assertEqual(level["likes"], 0)
        self.assertEqual(level["dislikes"], 0)
        self.assertEqual(self.request("GET", "/levels/player-demo/data.json")[1], data["data"])
        self.assertEqual(self.request("GET", "/levels/player-demo.json")[1]["downloads"], 1)
        self.assertEqual(self.request("GET", "/levels.json?shallow=true")[1], {"player-demo": True})
        listed = self.request("GET", "/levels.json")[1]["player-demo"]
        self.assertEqual(listed["name"], "My level")
        self.assertEqual(listed["downloads"], 1)
        self.assertEqual(self.request("DELETE", "/levels/player-demo.json", token=token)[0], 200)
        self.assertEqual(self.request("GET", "/levels/player-demo.json")[0], 404)

    def test_level_download_version_compatibility(self):
        token = self.register()["idToken"]
        for version in (1, 2, 10, 28, 29, 99999999999999999999999999999999):
            body = {"name": "Versioned", "data": "data-" + str(version), "api_version_id": version}
            self.assertEqual(self.request("PUT", f"/levels/v{version}.json", body, token)[0], 200)
        for build in (27, 28, 29):
            headers = {"User-Agent": f"DF-New/3.0.0 Alpha 1 (build/{build})"}
            expected = {"v" + str(v) for v in (1, 2, 10, 28, 29) if v <= build}
            for suffix in ("", "?shallow=true", "?metadata=true"):
                status, levels = self.request("GET", "/levels.json" + suffix, headers=headers)
                self.assertEqual(status, 200)
                self.assertEqual(set(levels), expected, (build, suffix))
                if suffix == "?metadata=true":
                    self.assertTrue(all("data" not in level for level in levels.values()))
            for version in (2, 10, 28, 29):
                for suffix in (".json", "/data.json"):
                    status, result = self.request("GET", f"/levels/v{version}" + suffix, headers=headers)
                    self.assertEqual(status, 200 if version <= build else 426)
                    if status == 426:
                        self.assertNotIn("data-", json.dumps(result))
        # Rejected downloads do not count as successful downloads.
        with self.server.store.connect() as db:
            counts = dict(db.execute("SELECT level_id,downloads FROM level_stats"))
        self.assertEqual(counts, {"v2": 3, "v10": 3, "v28": 2, "v29": 1})

    def test_upload_uses_compiled_build_and_reupload_updates_compatibility(self):
        token = self.register()["idToken"]
        body = {"name": "Versioned", "data": "level", "api_version_id": 1}
        old_headers = {"User-Agent": "DF-New/3.0.0 Alpha 1 (build/28)"}
        new_headers = {"User-Agent": "DF-New/3.0.0 Alpha 2 (build/29)"}
        path = "/levels/versioned.json"
        self.assertEqual(self.request("PUT", path, body, token, headers=old_headers)[0], 200)
        self.assertEqual(self.request("GET", path, headers=old_headers)[1]["api_version_id"], "28")
        self.assertEqual(self.request("PUT", path, body, token, headers=new_headers)[0], 200)
        self.assertEqual(self.request("GET", path, headers=old_headers)[0], 426)
        self.assertEqual(self.request("GET", path, headers=new_headers)[1]["api_version_id"], "29")
        # Version policy must not prevent an owner from deleting a newer level.
        self.assertEqual(self.request("DELETE", path, token=token, headers=old_headers)[0], 200)

    def test_legacy_downloads_and_unknown_stored_versions(self):
        token = self.register()["idToken"]
        for version in (1, 28):
            self.request("PUT", f"/levels/v{version}.json",
                         {"name": "Versioned", "data": "level", "api_version_id": version}, token)
        self.request("PUT", "/levels/unknown.json", {"name": "Unknown", "data": "level"}, token)
        with self.server.store.connect() as db:
            db.execute("UPDATE levels SET api_version='unknown' WHERE id='unknown'")
        for user_agent in ("", "Mozilla/5.0", "DF-New slim-menu"):
            headers = {"User-Agent": user_agent}
            self.assertEqual(set(self.request("GET", "/levels.json", headers=headers)[1]), {"v1"})
            for level in ("v28", "unknown"):
                self.assertEqual(self.request("GET", f"/levels/{level}/data.json", headers=headers)[0], 426)
        for user_agent in ("DF-New/1.0", "DF-New/invalid"):
            self.assertEqual(self.request("GET", "/levels.json", headers={"User-Agent": user_agent})[0], 426)
        headers = {"User-Agent": "DF-New/3.0.0 Alpha 1 (build/28)"}
        self.assertEqual(self.request("GET", "/levels/unknown.json", headers=headers)[0], 426)
        self.assertEqual(set(self.request("GET", "/levels.json?shallow=true", headers=headers)[1]), {"v1", "v28"})

    def test_upload_version_id_validation(self):
        token = self.register()["idToken"]
        body = {"name": "Versioned", "data": "level"}
        for version in (True, False, None, 0, -1, 1.5, "", "dev", "1.0", "-1", " 28", "9" * 33, [], {}):
            self.assertEqual(self.request("PUT", "/levels/versioned.json",
                {**body, "api_version_id": version}, token)[0], 400, version)
        for field in ("api_version_id", "level_api_version_id"):
            self.assertEqual(self.request("PUT", "/levels/versioned.json", {**body, field: "0028"}, token)[0], 200)
            with self.server.store.connect() as db:
                self.assertEqual(db.execute("SELECT api_version FROM levels WHERE id='versioned'").fetchone()[0], "28")

    def test_level_list_pagination_filters_versions_before_slicing(self):
        owner = self.register()
        with self.server.store.connect() as db:
            db.executemany("INSERT INTO levels VALUES(?,?,?,?,?,?)", [
                (f"level-{i:02}", owner["localId"], f"Level {i}", "large level payload " * 100,
                 "29" if i % 5 == 0 else "28", 1) for i in range(28)])
        headers = {"User-Agent": "DF-New/3.0.0 Alpha 1 (build/28)"}
        expected = [f"level-{i:02}" for i in range(28) if i % 5 != 0]
        received = []
        for page in range(3):
            status, result = self.request("GET", f"/levels.json?metadata=true&page={page}&page_size=10", headers=headers)
            self.assertEqual(status, 200)
            self.assertEqual(result["page"], page)
            self.assertEqual(result["page_count"], 3)
            self.assertEqual(result["total"], len(expected))
            self.assertEqual(result["page_size"], 10)
            self.assertEqual(list(result["levels"]), expected[page * 10:(page + 1) * 10])
            self.assertTrue(all("data" not in level for level in result["levels"].values()))
            received.extend(result["levels"])
        self.assertEqual(received, expected)
        last = self.request("GET", "/levels.json?metadata=true&page=999", headers=headers)[1]
        self.assertEqual(last["page"], 2)
        self.assertEqual(list(last["levels"]), expected[20:])
        shallow = self.request("GET", "/levels.json?shallow=true&page=1", headers=headers)[1]
        self.assertEqual(shallow["levels"], {level: True for level in expected[10:20]})
        with self.server.store.connect() as db:
            self.assertEqual(db.execute("SELECT COUNT(*) FROM level_stats").fetchone()[0], 0)

    def test_level_list_pagination_applies_difficulty_and_handles_empty_pages(self):
        owner = self.register()
        with self.server.store.connect() as db:
            db.executemany("INSERT INTO levels VALUES(?,?,?,?,?,?)", [
                (f"level-{i}", owner["localId"], f"Level {i}", "level", "1", 1) for i in range(10)])
            db.executemany("INSERT INTO level_difficulties VALUES(?,?,?,?)", [
                (f"level-{i}", i, owner["localId"], 1) for i in range(1, 10)])
        for filter_name, expected in (("all", list(range(10))), ("easy", [1, 2, 3]),
                                      ("medium", [4, 5, 6]), ("hard", [7, 8, 9]), ("unrated", [0])):
            status, result = self.request("GET", f"/levels.json?metadata=true&page=0&page_size=2&filter={filter_name}")
            self.assertEqual(status, 200)
            self.assertEqual(result["total"], len(expected))
            self.assertEqual(list(result["levels"]), [f"level-{i}" for i in expected[:2]])
            self.assertEqual(result["page_count"], (len(expected) + 1) // 2)
        with self.server.store.connect() as db:
            db.execute("DELETE FROM levels")
        result = self.request("GET", "/levels.json?metadata=true&page=12")[1]
        self.assertEqual(result, {"levels": {}, "page": 0, "page_size": 10, "total": 0, "page_count": 1})

    def test_level_list_pagination_validation(self):
        for query in ("page=-1", "page=abc", "page=1.5", "page=", "page=2147483648",
                      "page=99999999999999999999", "page=0&page=1", "page_size=0", "page_size=101",
                      "page_size=-1", "page_size=2&page_size=3", "filter=invalid", "filter=all&filter=hard"):
            self.assertEqual(self.request("GET", "/levels.json?" + query)[0], 400, query)
        self.assertEqual(self.request("GET", "/levels/missing.json?page=0")[0], 400)
        status, result = self.request("GET", "/levels.json?page_size=100")
        self.assertEqual(status, 200)
        self.assertEqual(result["page_size"], 100)
        # Existing non-paginated API clients retain their response shape.
        self.assertEqual(self.request("GET", "/levels.json?metadata=true")[1], {})

    def test_level_votes_and_download_counts(self):
        owner = self.register("owner")["idToken"]
        liker = self.register("liker")["idToken"]
        disliker = self.register("disliker")["idToken"]
        self.request("PUT", "/levels/voted.json", {"name": "Voted", "data": "level"}, owner)
        self.assertEqual(self.request("GET", "/levels/voted/data.json")[0], 200)
        self.assertEqual(self.request("GET", "/levels/voted/data.json")[0], 200)
        self.assertEqual(self.request("PUT", "/levels/voted/vote", {"vote": "like"})[0], 401)
        self.assertEqual(self.request("PUT", "/levels/voted/vote", {"vote": "like"}, liker)[1],
                         {"level_id": "voted", "downloads": 2, "likes": 1, "dislikes": 0})
        self.assertEqual(self.request("PUT", "/levels/voted/vote", {"vote": "dislike"}, disliker)[1]["dislikes"], 1)
        self.assertEqual(self.request("PUT", "/levels/voted/vote", {"vote": "dislike"}, liker)[1],
                         {"level_id": "voted", "downloads": 2, "likes": 0, "dislikes": 2})
        self.assertEqual(self.request("PUT", "/levels/voted/vote", {"vote": "clear"}, liker)[1],
                         {"level_id": "voted", "downloads": 2, "likes": 0, "dislikes": 1})
        metadata = self.request("GET", "/levels.json?metadata=true")[1]["voted"]
        self.assertEqual(metadata["downloads"], 2)
        self.assertEqual(metadata["likes"], 0)
        self.assertEqual(metadata["dislikes"], 1)
        self.assertEqual(self.request("PUT", "/levels/voted/vote", {"vote": "bad"}, liker)[0], 400)
        self.assertEqual(self.request("PUT", "/levels/missing/vote", {"vote": "like"}, liker)[0], 404)

    def test_moderator_can_trigger_configured_server_update(self):
        user = self.register("player")["idToken"]
        moderator = self.register("servermod")
        token = moderator["idToken"]
        self.server.store.set_moderator("servermod", True)
        self.assertEqual(self.request("PUT", "/api/server/update", {})[0], 401)
        self.assertEqual(self.request("PUT", "/api/server/update", {}, user)[0], 403)
        self.assertEqual(self.request("PUT", "/api/server/update", {}, token)[0], 503)

        marker = Path(self.temp.name) / "updated.txt"
        self.server.server_update_command = [sys.executable, "-c", "from pathlib import Path; Path(r'" + str(marker) + "').write_text('ok')"]
        self.server.server_update_status["configured"] = True
        self.server.server_update_status["state"] = "idle"
        self.server.server_update_status["detail"] = "Ready."
        status, result = self.request("PUT", "/api/server/update", {}, token)
        self.assertEqual(status, 202, result)
        self.assertTrue(result["running"])
        for _ in range(50):
            state = self.request("GET", "/api/server/update", token=token)[1]
            if not state["running"]:
                break
            time.sleep(0.05)
        self.assertEqual(state["state"], "succeeded")
        self.assertEqual(marker.read_text(), "ok")

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
        connection = http.client.HTTPConnection("127.0.0.1", self.server.server_port, timeout=10)
        connection.request("GET", "/update-manifest.json")
        response = connection.getresponse()
        self.assertEqual(response.status, 302)
        self.assertEqual(response.getheader("Location"), DEFAULT_UPDATE_MANIFEST_URL)
        response.read()
        connection.close()

        self.server.update_manifest_url = ""
        self.assertEqual(self.request("GET", "/update-manifest.json")[0], 404)
        manifest = {"version": "2.3.1", "version_id": 27, "installer_url": "https://game.example.com/releases/setup.exe"}
        (self.releases / "update-manifest.json").write_text(json.dumps(manifest))
        (self.releases / "setup.exe").write_bytes(b"test-installer")
        (self.releases / "private.txt").write_text("private")
        self.assertEqual(self.request("GET", "/update-manifest.json")[1], manifest)
        self.assertEqual(self.request("GET", "/releases/setup.exe"), (200, b"test-installer"))
        self.assertEqual(self.request("GET", "/releases/private.txt")[0], 404)
        self.assertEqual(self.request("GET", "/releases/../setup.exe")[0], 404)

    def test_updates_remain_available_to_blocked_and_legacy_clients(self):
        with self.server.store.connect() as db:
            db.execute("UPDATE client_version_policy SET minimum_build=239 WHERE id=1")
            db.execute("INSERT INTO blocked_versions VALUES('240','Blocked current build',1)")
        manifest = {"version": "4.0.2", "version_id": 241,
                    "installer_url": "/releases/setup.exe"}
        (self.releases / "update-manifest.json").write_text(json.dumps(manifest))
        (self.releases / "setup.exe").write_bytes(b"windows-installer")
        (self.releases / "game.apk").write_bytes(b"android-installer")
        for user_agent in ("DF-New/1.0", "DF-New/1.0-android", "DF-New/3.0.0 Alpha 1 (build/28)",
                           "DF-New/4.0.0 (build/238)", "DF-New/4.0.1 (build/240)"):
            with self.subTest(user_agent=user_agent):
                headers = {"User-Agent": user_agent, "Authorization": "Bearer expired-session"}
                self.assertEqual(self.request("GET", "/api.json", headers=headers)[0], 426)
                self.assertEqual(self.request("GET", "/levels.json", headers=headers)[0], 426)
                self.server.update_manifest_url = DEFAULT_UPDATE_MANIFEST_URL
                connection = http.client.HTTPConnection("127.0.0.1", self.server.server_port, timeout=10)
                connection.request("GET", "/update-manifest.json?current_version=1", headers=headers)
                response = connection.getresponse()
                self.assertEqual(response.status, 302)
                self.assertEqual(response.getheader("Location"), DEFAULT_UPDATE_MANIFEST_URL)
                response.read()
                connection.close()
                self.server.update_manifest_url = ""
                self.assertEqual(self.request("GET", "/update-manifest.json", headers=headers), (200, manifest))
                self.assertEqual(self.request("GET", "/releases/setup.exe", headers=headers), (200, b"windows-installer"))
                self.assertEqual(self.request("GET", "/releases/game.apk", headers=headers), (200, b"android-installer"))

    def test_web_and_health(self):
        self.assertEqual(self.request("GET", "/health")[1]["status"], "ok")
        self.assertIn(b"currentPassword", self.request("GET", "/")[1])
        self.assertIn(b"/api/auth/", self.request("GET", "/account-manager.js")[1])
        self.assertNotIn("firebase", json.dumps(self.request("GET", "/api.json")[1]).lower())

    def test_account_manager_without_repository_pages(self):
        # A deployment containing only server/ must serve all browser assets.
        with patch("game_server.ROOT", Path(self.temp.name)):
            for route, expected in (("/", b"currentPassword"),
                                    ("/account-manager.html", b"currentPassword"),
                                    ("/account-manager.js", b"/api/auth/"),
                                    ("/theme.css", b"--")):
                with self.subTest(route=route):
                    status, body = self.request("GET", route)
                    self.assertEqual(status, 200)
                    self.assertIn(expected, body)

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
