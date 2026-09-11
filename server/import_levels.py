"""Import old Firebase levels into the custom server without overwriting levels."""
import argparse
import json
import math
import os
from pathlib import Path
import sqlite3
import sys
import time
from urllib.error import HTTPError, URLError
from urllib.parse import urlencode, urlsplit, urlunsplit
from urllib.request import HTTPRedirectHandler, Request, build_opener

sys.path.insert(0, str(Path(__file__).resolve().parent))
from game_server import LEVEL_ID, MAX_BODY, ROOT, Store

DEFAULT_SOURCE = "https://timetravel-server-default-rtdb.europe-west1.firebasedatabase.app"
MAX_EXPORT_BYTES = 128 * 1024 * 1024


class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        # An optional Firebase credential must never be forwarded to another host.
        return None


def parse_export(raw):
    """Accept either /levels.json or a Firebase database export with a levels node."""
    def invalid_constant(value):
        raise ValueError("Non-finite JSON number in legacy export.")

    try:
        value = json.loads(raw, parse_constant=invalid_constant)
    except (UnicodeError, json.JSONDecodeError, RecursionError) as error:
        raise ValueError("Legacy export is not valid UTF-8 JSON.") from error
    if isinstance(value, dict) and "levels" in value and (
            value["levels"] is None or isinstance(value["levels"], dict) and not isinstance(value["levels"].get("data"), str)):
        value = value["levels"]
    if value is None:
        return {}
    if not isinstance(value, dict) or isinstance(value.get("error"), str):
        raise ValueError("Expected an object containing legacy level IDs.")
    return value


def read_export(stream, max_bytes=MAX_EXPORT_BYTES):
    if max_bytes < 1:
        raise ValueError("Export size limit must be positive.")
    raw = stream.read(max_bytes + 1)
    if len(raw) > max_bytes:
        raise ValueError("Legacy export exceeds the size limit. Increase --max-bytes if needed.")
    return parse_export(raw)


def fetch_levels(source=DEFAULT_SOURCE, token="", timeout=30, max_bytes=MAX_EXPORT_BYTES):
    """Read the old server; no writes or deletes are sent to the source."""
    parts = urlsplit(source)
    if (parts.scheme not in ("http", "https") or not parts.hostname or parts.username or
            parts.password or parts.query or parts.fragment or
            parts.path not in ("", "/", "/levels.json") or any(ord(c) <= 32 for c in source)):
        raise ValueError("Source must be an HTTP(S) server origin or its /levels.json URL.")
    if token and parts.scheme != "https":
        raise ValueError("Authenticated legacy downloads require HTTPS.")
    if not math.isfinite(timeout) or timeout <= 0:
        raise ValueError("Timeout must be a positive finite number.")
    if max_bytes < 1:
        raise ValueError("Export size limit must be positive.")
    url = urlunsplit((parts.scheme, parts.netloc, "/levels.json",
                     urlencode({"auth": token}) if token else "", ""))
    request = Request(url, headers={"Accept": "application/json", "User-Agent": "DorfLevelImporter/1"})
    try:
        with build_opener(NoRedirect()).open(request, timeout=timeout) as response:
            return read_export(response, max_bytes)
    except HTTPError as error:
        # HTTPError contains the request URL, which may contain the old token.
        raise ValueError(f"Legacy server returned HTTP {error.code}; check the URL and read permissions.") from None
    except (URLError, TimeoutError, OSError):
        raise ValueError("Could not read the legacy server. Check connectivity and TLS settings.") from None


def normalize_level(level_id, record):
    if not isinstance(level_id, str) or not LEVEL_ID.fullmatch(level_id):
        raise ValueError("ID must contain 1 to 160 letters, numbers, underscores or hyphens.")
    if not isinstance(record, dict):
        raise ValueError("Expected a level object (a shallow ID listing cannot be imported).")
    data = record.get("data")
    if not isinstance(data, str) or not data.strip():
        raise ValueError("Level data must be nonempty text.")
    try:
        size = len(data.encode("utf-8"))
    except UnicodeError:
        raise ValueError("Level data contains invalid Unicode.") from None
    if size > MAX_BODY - 4096:
        raise ValueError("Level data exceeds the custom server's size limit.")
    name = record.get("name", level_id)
    if not isinstance(name, str) or not 1 <= len(name.strip()) <= 160:
        raise ValueError("Level name must contain 1 to 160 characters.")
    version = record.get("api_version_id", record.get("level_api_version_id", "1"))
    if isinstance(version, bool) or not isinstance(version, (str, int)) or len(str(version)) > 32:
        raise ValueError("Invalid API version.")
    timestamp = record.get("uploaded_at", 0)
    if (isinstance(timestamp, bool) or not isinstance(timestamp, (int, float)) or
            not math.isfinite(timestamp) or timestamp < 0 or timestamp > 253402300799):
        raise ValueError("Upload timestamp must be Unix seconds between 0 and 253402300799.")
    # Reject surrogate strings before SQLite tries to encode them inside the transaction.
    owner = record.get("owner", "")
    if not isinstance(owner, str) or len(owner) > 256:
        raise ValueError("Invalid legacy owner.")
    try:
        for value in (name, str(version), owner):
            value.encode("utf-8")
    except UnicodeError:
        raise ValueError("Metadata contains invalid Unicode.") from None
    return (level_id, name.strip(), data, str(version), int(timestamp), owner)


def import_levels(store, levels, owner, *, dry_run=False, owner_map=None, source="legacy-export"):
    """Import validated records atomically, assigning only explicitly chosen owners.

    owner is an existing custom-server username used as a fallback. owner_map can
    map original author names to other existing usernames. Matching a legacy
    name alone never grants ownership of an imported level to a new registrant.
    """
    if not isinstance(levels, dict):
        raise ValueError("Levels must be an object keyed by ID.")
    if not isinstance(owner, str) or not owner:
        raise ValueError("Specify an existing custom-server account with --owner.")
    owner_map = {} if owner_map is None else owner_map
    if not isinstance(owner_map, dict) or any(
            not isinstance(key, str) or not isinstance(value, str) for key, value in owner_map.items()):
        raise ValueError("Owner map must map legacy owner strings to existing usernames.")
    report = {"dry_run": dry_run, "imported": 0, "would_import": 0, "skipped": 0, "failed": 0, "details": []}
    # Validate outside the write transaction. Invalid records are reported individually.
    valid = []
    for level_id, record in levels.items():
        try:
            valid.append(normalize_level(level_id, record))
        except (ValueError, OverflowError) as error:
            report["failed"] += 1
            report["details"].append({"id": str(level_id), "status": "failed", "reason": str(error)})
    with store.connect() as db:
        # A transaction covers duplicate checks and inserts, including provenance.
        db.execute("BEGIN" if dry_run else "BEGIN IMMEDIATE")
        destinations = {}
        for username in {owner, *owner_map.values()}:
            if not isinstance(username, str) or not username:
                raise ValueError("Specify an existing custom-server account with --owner.")
            account = db.execute("SELECT id FROM users WHERE username=?", (username,)).fetchone()
            if not account:
                raise ValueError(f"Destination account '{username}' does not exist. Create it before importing.")
            destinations[username] = account["id"]
        if not dry_run:
            db.execute("""CREATE TABLE IF NOT EXISTS level_imports(
                level_id TEXT PRIMARY KEY REFERENCES levels(id) ON DELETE CASCADE,
                source TEXT NOT NULL, legacy_owner TEXT NOT NULL, imported_at INTEGER NOT NULL)""")
        for level_id, name, data, version, uploaded_at, legacy_owner in valid:
            if db.execute("SELECT 1 FROM levels WHERE id=?", (level_id,)).fetchone():
                report["skipped"] += 1
                report["details"].append({"id": level_id, "status": "skipped", "reason": "ID already exists; left unchanged."})
                continue
            target = owner_map.get(legacy_owner, owner)
            if dry_run:
                report["would_import"] += 1
            else:
                db.execute("INSERT INTO levels VALUES(?,?,?,?,?,?)",
                           (level_id, destinations[target], name, data, version, uploaded_at))
                db.execute("INSERT INTO level_imports VALUES(?,?,?,?)",
                           (level_id, source, legacy_owner, int(time.time())))
                report["imported"] += 1
            report["details"].append({"id": level_id, "status": "would_import" if dry_run else "imported", "owner": target})
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group()
    source.add_argument("--source", help=f"Old server origin (default: {DEFAULT_SOURCE})")
    source.add_argument("--file", type=Path, help="Local Firebase JSON export or /levels.json response")
    parser.add_argument("--database", default=os.environ.get("DATABASE_PATH", str(ROOT / "server/data/game.sqlite3")))
    parser.add_argument("--owner", required=True, help="Existing custom-server account to own imported levels")
    parser.add_argument("--owner-map", type=Path, help="JSON map of old owner names to existing new usernames")
    parser.add_argument("--dry-run", action="store_true", help="Preview without inserting levels or provenance")
    parser.add_argument("--token-env", default="LEGACY_SERVER_TOKEN", help="Environment variable holding an optional old Firebase token")
    parser.add_argument("--timeout", type=float, default=30)
    parser.add_argument("--max-bytes", type=int, default=MAX_EXPORT_BYTES)
    args = parser.parse_args()
    try:
        # Require an existing destination; do not silently create a new DB.
        if not Path(args.database).is_file():
            raise ValueError("Destination database does not exist. Start the server and create an account first.")
        owner_map = json.loads(args.owner_map.read_text(encoding="utf-8-sig")) if args.owner_map else None
        if args.file:
            with args.file.open("rb") as stream:
                levels = read_export(stream, args.max_bytes)
            label = args.file.name
        else:
            url = args.source or DEFAULT_SOURCE
            levels = fetch_levels(url, os.environ.get(args.token_env, ""), args.timeout, args.max_bytes)
            label = url  # fetch_levels rejects credentials and query parameters.
        report = import_levels(Store(args.database), levels, args.owner, dry_run=args.dry_run,
                               owner_map=owner_map, source=label)
    except (ValueError, OSError, sqlite3.Error) as error:
        print(json.dumps({"error": str(error)}))
        return 1
    print(json.dumps(report, indent=2))
    # Partial success is deliberately nonzero so unattended migrations notice invalid records.
    return 2 if report["failed"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
