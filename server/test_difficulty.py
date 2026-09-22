import json
from contextlib import closing
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_server
from game_server import Store


class DifficultyTests(unittest.TestCase):
    setUp = test_server.ServerTests.setUp
    tearDown = test_server.ServerTests.tearDown
    request = test_server.ServerTests.request
    register = test_server.ServerTests.register

    def prepare(self):
        self.owner = self.register("author")
        self.moderator = self.register("moderator")
        self.server.store.set_moderator("moderator", True)
        self.payload = {"name": "Test level", "data": "original level"}
        self.request("PUT", "/levels/test.json", self.payload, self.owner["idToken"])
        return self.moderator["idToken"]

    def test_only_moderators_can_rate(self):
        token = self.prepare()
        self.assertEqual(self.request("PUT", "/levels/test/difficulty", {"difficulty": 5})[0], 401)
        self.assertEqual(self.request("PUT", "/levels/test/difficulty", {"difficulty": 5}, self.owner["idToken"])[0], 403)
        self.assertEqual(self.request("PUT", "/levels/test/difficulty", {"difficulty": 5}, token)[0], 200)
        self.assertEqual(self.request("GET", "/levels/test.json")[1]["difficulty"], 5)

    def test_all_ratings_one_through_nine_and_invalid_values(self):
        token = self.prepare()
        for number in range(1, 10):
            status, result = self.request("PUT", "/levels/test/difficulty", {"difficulty": number}, token)
            self.assertEqual(status, 200, result)
            self.assertEqual(result["difficulty"], number)
        for number in (0, 10, -1, True, False, 1.0, 1.5, "5", None, [], {}):
            self.assertEqual(self.request("PUT", "/levels/test/difficulty", {"difficulty": number}, token)[0], 400)
        self.assertEqual(self.request("GET", "/levels/test.json")[1]["difficulty"], 9)

    def test_upload_cannot_set_or_spoof_rating(self):
        token = self.prepare()
        status, _ = self.request("PUT", "/levels/new.json", {**self.payload, "difficulty": 9}, self.owner["idToken"])
        self.assertEqual(status, 403)
        for field in ("downloads", "likes", "dislikes"):
            self.assertEqual(self.request("PUT", "/levels/new.json", {**self.payload, field: 99}, self.owner["idToken"])[0], 403)
        self.assertEqual(self.request("GET", "/levels/new.json")[0], 404)
        self.request("PUT", "/levels/test/difficulty", {"difficulty": 7}, token)
        self.assertEqual(self.request("PUT", "/levels/test.json", {**self.payload, "difficulty": 1}, self.owner["idToken"])[0], 403)
        self.assertEqual(self.request("GET", "/levels/test.json")[1]["difficulty"], 7)

    def test_unrated_metadata_and_content_change_reset(self):
        token = self.prepare()
        self.assertIsNone(self.request("GET", "/levels/test.json")[1]["difficulty"])
        self.request("PUT", "/levels/test/difficulty", {"difficulty": 4}, token)
        _, metadata = self.request("GET", "/levels.json?metadata=true")
        self.assertEqual(metadata["test"]["difficulty"], 4)
        self.assertEqual(metadata["test"]["downloads"], 0)
        self.assertEqual(metadata["test"]["likes"], 0)
        self.assertEqual(metadata["test"]["dislikes"], 0)
        self.assertNotIn("data", metadata["test"])
        self.assertEqual(self.request("GET", "/levels.json?shallow=true")[1], {"test": True})
        # A metadata-only edit does not invalidate the reviewed content.
        self.request("PUT", "/levels/test.json", {**self.payload, "name": "Renamed"}, self.owner["idToken"])
        self.assertEqual(self.request("GET", "/levels/test.json")[1]["difficulty"], 4)
        self.request("PUT", "/levels/test.json", {**self.payload, "data": "harder layout"}, self.owner["idToken"])
        self.assertIsNone(self.request("GET", "/levels/test.json")[1]["difficulty"])

    def test_revocation_applies_to_existing_sessions(self):
        token = self.prepare()
        self.assertTrue(self.request("POST", "/api/auth/lookup", {}, token)[1]["users"][0]["isModerator"])
        self.server.store.set_moderator("moderator", False)
        self.assertFalse(self.request("POST", "/api/auth/lookup", {}, token)[1]["users"][0]["isModerator"])
        self.assertEqual(self.request("PUT", "/levels/test/difficulty", {"difficulty": 4}, token)[0], 403)

    def test_registration_and_account_updates_cannot_grant_moderator(self):
        status, account = self.request("POST", "/api/auth/register", {
            "email": "fake@example.com", "displayName": "fake", "password": "strong-password",
            "isModerator": True, "role": "moderator"})
        self.assertEqual(status, 200)
        self.assertFalse(account["isModerator"])
        _, updated = self.request("POST", "/api/auth/update",
            {"currentPassword": "strong-password", "isModerator": True}, account["idToken"])
        self.assertFalse(updated["isModerator"])

    def test_missing_level_and_delete_clear_rating(self):
        token = self.prepare()
        self.assertEqual(self.request("PUT", "/levels/missing/difficulty", {"difficulty": 3}, token)[0], 404)
        self.request("PUT", "/levels/test/difficulty", {"difficulty": 3}, token)
        self.request("DELETE", "/levels/test.json", token=self.owner["idToken"])
        self.request("PUT", "/levels/test.json", self.payload, self.owner["idToken"])
        self.assertIsNone(self.request("GET", "/levels/test.json")[1]["difficulty"])

    def test_imported_difficulty_is_not_trusted(self):
        from import_levels import import_levels
        self.register("admin")
        import_levels(self.server.store, {"old-level": {
            "name": "Old level", "data": "old data", "difficulty": 9}}, "admin")
        self.assertIsNone(self.request("GET", "/levels/old-level.json")[1]["difficulty"])

    def test_persistence_and_operator_cli(self):
        token = self.prepare()
        self.request("PUT", "/levels/test/difficulty", {"difficulty": 6}, token)
        reopened = Store(self.database)
        self.assertEqual(reopened.levels("GET", "test", False, False, {}, "")["difficulty"], 6)
        command = [sys.executable, str(Path(__file__).with_name("moderators.py")), "--database", str(self.database)]
        result = subprocess.run(command + ["--revoke", "moderator"], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.request("PUT", "/levels/test/difficulty", {"difficulty": 2}, token)[0], 403)
        result = subprocess.run(command + ["--grant", "moderator"], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.request("PUT", "/levels/test/difficulty", {"difficulty": 2}, token)[0], 200)


class MigrationTests(unittest.TestCase):
    def test_existing_database_keeps_users_and_levels(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "old.sqlite3"
            with closing(sqlite3.connect(path)) as db:
                db.executescript("""
                    CREATE TABLE users(id TEXT PRIMARY KEY, email TEXT UNIQUE, username TEXT UNIQUE, salt TEXT, password TEXT);
                    CREATE TABLE levels(id TEXT PRIMARY KEY,user_id TEXT,name TEXT,data TEXT,api_version TEXT,uploaded_at INTEGER);
                    INSERT INTO users VALUES('u','test@example.com','player','salt','hash');
                    INSERT INTO levels VALUES('old','u','Old','unchanged','22',123);
                    PRAGMA user_version=1;
                """)
            store = Store(path)
            level = store.levels("GET", "old", False, False, {}, "")
            self.assertIsNone(level["difficulty"])
            self.assertEqual(level["downloads"], 0)
            self.assertEqual(level["likes"], 0)
            self.assertEqual(level["dislikes"], 0)
            self.assertEqual(level["data"], "unchanged")
            self.assertEqual(level["owner"], "player")
            store.set_moderator("player", True)
            Store(path)
            with store.connect() as db:
                self.assertEqual(db.execute("SELECT count(*) FROM moderators").fetchone()[0], 1)
