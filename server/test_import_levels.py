import io
import json
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
from urllib.error import HTTPError

sys.path.insert(0, str(Path(__file__).resolve().parent))
from game_server import Store
from import_levels import import_levels, parse_export, read_export, fetch_levels, NoRedirect


class ImportTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.store = Store(Path(self.temp.name) / "game.sqlite3")
        self.admin = self.store.auth("register", {
            "email": "admin@example.com", "displayName": "admin", "password": "strong-password"}, "")
        self.creator = self.store.auth("register", {
            "email": "creator@example.com", "displayName": "creator", "password": "strong-password"}, "")
        self.record = {"name": "Old level", "data": "1 2 3\n4 5 6\n", "owner": "old-author",
                       "api_version_id": 22, "uploaded_at": 1700000000}

    def tearDown(self):
        self.temp.cleanup()

    def test_import_preserves_level_and_provenance(self):
        report = import_levels(self.store, {"old-author-level": self.record}, "admin", source="old-server")
        self.assertEqual(report["imported"], 1)
        level = self.store.levels("GET", "old-author-level", False, False, {}, "")
        self.assertEqual(level["data"], self.record["data"])
        self.assertEqual(level["name"], self.record["name"])
        self.assertEqual(level["api_version_id"], "22")
        self.assertEqual(level["uploaded_at"], 1700000000)
        self.assertEqual(level["owner"], "admin")
        with self.store.connect() as db:
            row = db.execute("SELECT * FROM level_imports").fetchone()
            self.assertEqual(row["legacy_owner"], "old-author")
            self.assertEqual(row["source"], "old-server")
        self.assertEqual(self.store.levels("GET", "", False, True, {}, ""), {"old-author-level": True})

    def test_repeat_import_never_overwrites_existing_level(self):
        import_levels(self.store, {"same-id": self.record}, "admin")
        changed = {**self.record, "data": "different"}
        report = import_levels(self.store, {"same-id": changed}, "creator")
        self.assertEqual(report["skipped"], 1)
        self.assertEqual(report["imported"], 0)
        result = self.store.levels("GET", "same-id", False, False, {}, "")
        self.assertEqual(result["data"], self.record["data"])
        self.assertEqual(result["owner"], "admin")

    def test_dry_run_changes_neither_levels_nor_schema(self):
        with self.store.connect() as db:
            before = list(db.execute("SELECT name FROM sqlite_master ORDER BY name"))
        report = import_levels(self.store, {"old-level": self.record}, "admin", dry_run=True)
        self.assertEqual(report["would_import"], 1)
        self.assertEqual(report["imported"], 0)
        with self.store.connect() as db:
            self.assertEqual(db.execute("SELECT count(*) FROM levels").fetchone()[0], 0)
            after = list(db.execute("SELECT name FROM sqlite_master ORDER BY name"))
        self.assertEqual([tuple(r) for r in before], [tuple(r) for r in after])

    def test_explicit_owner_mapping(self):
        report = import_levels(self.store, {"old-level": self.record}, "admin",
                               owner_map={"old-author": "creator"})
        self.assertEqual(report["imported"], 1)
        self.assertEqual(self.store.levels("GET", "old-level", False, False, {}, "")["owner"], "creator")

    def test_unknown_owner_prevents_all_imports(self):
        with self.assertRaises(ValueError):
            import_levels(self.store, {"old-level": self.record}, "admin", owner_map={"other": "missing"})
        self.assertEqual(self.store.levels("GET", "", False, True, {}, ""), {})

    def test_invalid_rows_reported_while_valid_rows_import(self):
        levels = {
            "good": self.record,
            "../bad": self.record,
            "empty": {**self.record, "data": ""},
            "shallow": True,
            "bad-owner": {**self.record, "owner": []},
            "bad-time": {**self.record, "uploaded_at": float("nan")},
            "bad-version": {**self.record, "api_version_id": True},
            "bad-unicode": {**self.record, "data": "\ud800"},
        }
        report = import_levels(self.store, levels, "admin")
        self.assertEqual(report["failed"], 7)
        self.assertEqual(report["imported"], 1)

    def test_transaction_rolls_back_if_database_write_fails(self):
        with self.store.connect() as db:
            db.execute("""CREATE TRIGGER fail_second BEFORE INSERT ON levels
                       WHEN NEW.id='second' BEGIN SELECT RAISE(ABORT, 'simulated failure'); END""")
        with self.assertRaises(sqlite3.IntegrityError):
            import_levels(self.store, {"first": self.record, "second": self.record}, "admin")
        self.assertEqual(self.store.levels("GET", "", False, True, {}, ""), {})

    def test_cli_dry_run_import_and_duplicate_report(self):
        export = Path(self.temp.name) / "export.json"
        export.write_text(json.dumps({"levels": {"old-level": self.record}}))
        command = [sys.executable, str(Path(__file__).with_name("import_levels.py")),
                   "--database", self.store.path, "--file", str(export), "--owner", "admin"]
        preview = subprocess.run(command + ["--dry-run"], capture_output=True, text=True)
        self.assertEqual(preview.returncode, 0, preview.stderr)
        self.assertEqual(json.loads(preview.stdout)["would_import"], 1)
        imported = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(imported.returncode, 0, imported.stderr)
        self.assertEqual(json.loads(imported.stdout)["imported"], 1)
        repeated = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(repeated.returncode, 0, repeated.stderr)
        self.assertEqual(json.loads(repeated.stdout)["skipped"], 1)

    def test_provenance_removed_when_owner_deletes_level(self):
        import_levels(self.store, {"old-level": self.record}, "admin")
        self.store.levels("DELETE", "old-level", False, False, {}, self.admin["idToken"])
        with self.store.connect() as db:
            self.assertEqual(db.execute("SELECT count(*) FROM level_imports").fetchone()[0], 0)


class ExportTests(unittest.TestCase):
    def test_database_export_and_collection_export(self):
        levels = {"level": {"data": "raw level", "name": "name"}}
        self.assertEqual(parse_export(json.dumps(levels)), levels)
        self.assertEqual(parse_export(json.dumps({"levels": levels, "users": {"ignored": {}}})), levels)
        self.assertEqual(parse_export(json.dumps({"levels": {"data": {"data": "text"}}})),
                         {"data": {"data": "text"}})
        self.assertEqual(parse_export("null"), {})
        self.assertEqual(parse_export('{"levels":null}'), {})
        # A valid level may itself be named "levels".
        collision = {"levels": {"data": "raw level", "name": "name"}}
        self.assertEqual(parse_export(json.dumps(collision)), collision)

    def test_bad_exports_and_size_limits(self):
        for raw in ("[1]", "{", '{"error":"Permission denied"}', '{"level":NaN}'):
            with self.assertRaises(ValueError):
                parse_export(raw)
        with self.assertRaises(ValueError):
            read_export(io.BytesIO(b'{"too":"large"}'), max_bytes=3)
        with self.assertRaises(ValueError):
            read_export(io.BytesIO(b"{}"), max_bytes=0)

    def test_remote_source_uses_get_and_bounded_read(self):
        response = io.BytesIO(b'{"old":{"data":"hello"}}')
        with patch("import_levels.build_opener") as opener:
            opener.return_value.open.return_value = response
            self.assertEqual(fetch_levels("https://example.com/", token="private token"), {"old": {"data": "hello"}})
            request = opener.return_value.open.call_args.args[0]
            self.assertEqual(request.get_method(), "GET")
            self.assertEqual(request.full_url, "https://example.com/levels.json?auth=private+token")

    def test_remote_failure_does_not_expose_credentials(self):
        with patch("import_levels.build_opener") as opener:
            opener.return_value.open.side_effect = HTTPError("https://host?auth=secret", 403, "secret", {}, None)
            with self.assertRaises(ValueError) as error:
                fetch_levels("https://example.com", token="secret")
            self.assertIn("403", str(error.exception))
            self.assertNotIn("secret", str(error.exception))
        self.assertIsNone(NoRedirect().redirect_request(None, None, 302, "", {}, "https://elsewhere"))

    def test_rejects_unsafe_sources(self):
        for url in ("file:///tmp/levels.json", "https://user:pass@host", "https://host?auth=secret",
                    "https://host/other", "https://host/#frag"):
            with self.assertRaises(ValueError):
                fetch_levels(url)
        with self.assertRaises(ValueError):
            fetch_levels("http://example.com", token="secret")
