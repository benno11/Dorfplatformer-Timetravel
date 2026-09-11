"""Private operator dashboard. Served separately from the public game API."""
import hmac
import json
import os
from pathlib import Path
import secrets
import time
import sys
from urllib.parse import parse_qs, urlsplit

sys.path.insert(0, str(Path(__file__).resolve().parent))
from game_server import ApiError, GameServer, Handler

WEB = Path(__file__).resolve().parent / "admin"


def load_admin_key(path):
    key = os.environ.get("ADMIN_KEY", "")
    if not key:
        path = Path(path)
        path.parent.mkdir(parents=True, exist_ok=True)
        try:
            descriptor = os.open(path, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
        except FileExistsError:
            key = path.read_text(encoding="utf-8").strip()
        else:
            key = secrets.token_urlsafe(32)
            with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
                stream.write(key + "\n")
    if not key.isascii() or not 32 <= len(key) <= 256:
        raise ValueError("Admin key must contain 32 to 256 ASCII characters.")
    return key


def reason_from(body):
    reason = body.get("reason")
    if not isinstance(reason, str) or not 3 <= len(reason.strip()) <= 500:
        raise ApiError(400, "Enter a reason between 3 and 500 characters.")
    return reason.strip()


def audit(db, action, target, reason, actor="dashboard"):
    db.execute("INSERT INTO admin_events(action,target_id,reason,actor,created_at) VALUES(?,?,?,?,?)",
               (action, target, reason, actor, int(time.time())))


def admin_action(store, action, body):
    reason = reason_from(body)
    if action == "version-block":
        version = body.get("version_id")
        enabled = body.get("enabled")
        if not (version == "legacy" or (type(version) is int and 1 <= version <= 2100000000)):
            raise ApiError(400, "Build ID must be an integer from 1 to 2100000000, or legacy.")
        if type(enabled) is not bool:
            raise ApiError(400, "Blocked status must be true or false.")
        with store.connect() as db:
            db.execute("BEGIN IMMEDIATE")
            if enabled:
                db.execute("INSERT INTO blocked_versions VALUES(?,?,?) ON CONFLICT(version_id) DO UPDATE SET reason=excluded.reason,created_at=excluded.created_at",
                           (str(version), reason, int(time.time())))
            else:
                db.execute("DELETE FROM blocked_versions WHERE version_id=?", (str(version),))
            audit(db, "block_version" if enabled else "unblock_version", str(version), reason)
        return {"ok": True}
    target = body.get("user_id" if action in ("moderator", "suspension", "warn") else "level_id")
    if not isinstance(target, str) or not 1 <= len(target) <= 256:
        raise ApiError(400, "Select a valid player or level.")
    with store.connect() as db:
        db.execute("BEGIN IMMEDIATE")
        if action == "delete-level":
            level = db.execute("SELECT name,user_id FROM levels WHERE id=?", (target,)).fetchone()
            if not level:
                raise ApiError(404, "Level not found.")
            db.execute("DELETE FROM levels WHERE id=?", (target,))
            audit(db, action, target, reason + " | Level: " + level["name"] + " | Owner: " + level["user_id"])
            return {"ok": True}
        if not db.execute("SELECT 1 FROM users WHERE id=?", (target,)).fetchone():
            raise ApiError(404, "Player not found.")
        if action == "moderator":
            enabled = body.get("enabled")
            if type(enabled) is not bool:
                raise ApiError(400, "Moderator status must be true or false.")
            if enabled:
                db.execute("INSERT OR IGNORE INTO moderators VALUES(?)", (target,))
            else:
                db.execute("DELETE FROM moderators WHERE user_id=?", (target,))
            audit(db, "grant_moderator" if enabled else "revoke_moderator", target, reason)
        elif action == "warn":
            audit(db, "warn", target, reason)
        elif action == "suspension":
            duration = body.get("hours")
            if duration is not None and (type(duration) is not int or not 0 <= duration <= 8760):
                raise ApiError(400, "Hours must be 0-8760, or null for a permanent suspension.")
            if "hours" not in body:
                raise ApiError(400, "Select a suspension duration.")
            if duration == 0:
                db.execute("DELETE FROM upload_blocks WHERE user_id=?", (target,))
                audit(db, "lift_upload_suspension", target, reason)
            else:
                until = None if duration is None else int(time.time()) + duration * 3600
                db.execute("""INSERT INTO upload_blocks VALUES(?,?,?,?) ON CONFLICT(user_id)
                    DO UPDATE SET until=excluded.until,reason=excluded.reason,created_at=excluded.created_at""",
                    (target, until, reason, int(time.time())))
                audit(db, "suspend_uploads", target, reason + (" | Permanent" if until is None else " | Hours: " + str(duration)))
        else:
            raise ApiError(404, "Unknown admin action.")
    return {"ok": True}


def dashboard_state(store, query):
    term = query.get("q", [""])[0]
    if len(term) > 100:
        raise ApiError(400, "Search is too long.")
    try:
        user_offset = int(query.get("users_offset", ["0"])[0])
        level_offset = int(query.get("levels_offset", ["0"])[0])
        if min(user_offset, level_offset) < 0 or max(user_offset, level_offset) > 10000000:
            raise ValueError()
    except ValueError:
        raise ApiError(400, "Invalid page offset.") from None
    pattern = "%" + term.replace("!", "!!").replace("%", "!%").replace("_", "!_") + "%"
    now = int(time.time())
    with store.connect() as db:
        db.execute("BEGIN")
        users = db.execute("""SELECT u.id,u.username,u.email,(m.user_id IS NOT NULL) AS is_moderator,
            (SELECT count(*) FROM levels WHERE user_id=u.id) AS level_count,
            (b.user_id IS NOT NULL) AS upload_suspended,b.until,b.reason,
            (SELECT reason FROM admin_events WHERE target_id=u.id AND action='warn' ORDER BY id DESC LIMIT 1) AS warning
            FROM users u LEFT JOIN moderators m ON m.user_id=u.id
            LEFT JOIN upload_blocks b ON b.user_id=u.id AND (b.until IS NULL OR b.until>?)
            WHERE u.username LIKE ? ESCAPE '!' OR u.email LIKE ? ESCAPE '!'
            ORDER BY u.username LIMIT 25 OFFSET ?""", (now, pattern, pattern, user_offset)).fetchall()
        levels = db.execute("""SELECT l.id,l.name,u.username AS owner,l.user_id,d.difficulty,l.uploaded_at,
            length(l.data) AS size FROM levels l JOIN users u ON u.id=l.user_id
            LEFT JOIN level_difficulties d ON d.level_id=l.id
            WHERE l.id LIKE ? ESCAPE '!' OR l.name LIKE ? ESCAPE '!'
            ORDER BY l.uploaded_at DESC,l.id LIMIT 25 OFFSET ?""", (pattern, pattern, level_offset)).fetchall()
        events = db.execute("""SELECT e.*,COALESCE(u.username,e.actor) AS actor_name,
            COALESCE(t.username,e.target_id) AS target_name FROM admin_events e
            LEFT JOIN users u ON u.id=e.actor LEFT JOIN users t ON t.id=e.target_id ORDER BY e.id DESC LIMIT 50""").fetchall()
        scalar = lambda sql, args=(): db.execute(sql, args).fetchone()[0]
        return {
            "users": [dict(row) for row in users], "levels": [dict(row) for row in levels],
            "blocked_versions": [dict(row) for row in db.execute("SELECT * FROM blocked_versions ORDER BY created_at DESC,version_id")],
            "events": [dict(row) for row in events], "page_size": 25,
            "users_total": scalar("SELECT count(*) FROM users WHERE username LIKE ? ESCAPE '!' OR email LIKE ? ESCAPE '!'", (pattern, pattern)),
            "levels_total": scalar("SELECT count(*) FROM levels WHERE id LIKE ? ESCAPE '!' OR name LIKE ? ESCAPE '!'", (pattern, pattern)),
            "stats": {"players": scalar("SELECT count(*) FROM users"), "levels": scalar("SELECT count(*) FROM levels"),
                      "moderators": scalar("SELECT count(*) FROM moderators"),
                      "suspended": scalar("SELECT count(*) FROM upload_blocks WHERE until IS NULL OR until>?", (now,))}
        }


class AdminServer(GameServer):
    def __init__(self, address, database, key):
        if address[0] != "127.0.0.1":
            raise ValueError("The admin dashboard must bind to 127.0.0.1.")
        self.admin_key = key
        super().__init__(address, database, handler=AdminHandler)


class AdminHandler(Handler):
    def end_headers(self):
        self.send_header("Content-Security-Policy", "default-src 'self'; script-src 'self'; style-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'")
        super().end_headers()

    def handle_admin(self):
        try:
            # Protect the loopback service against DNS rebinding and cross-site requests.
            host = self.headers.get("Host", "")
            if host not in {f"127.0.0.1:{self.server.server_port}", f"localhost:{self.server.server_port}"}:
                raise ApiError(403, "Open the dashboard through its localhost address.")
            origin = self.headers.get("Origin")
            if origin is not None and origin != "http://" + host:
                raise ApiError(403, "Cross-origin requests are not allowed.")
            if self.headers.get("Sec-Fetch-Site", "none") not in ("same-origin", "none"):
                raise ApiError(403, "Cross-site requests are not allowed.")
            url = urlsplit(self.path)
            files = {"/": ("index.html", "text/html; charset=utf-8"),
                     "/admin.js": ("admin.js", "text/javascript; charset=utf-8"),
                     "/admin.css": ("admin.css", "text/css; charset=utf-8")}
            if self.command == "GET" and url.path in files:
                file, mime = files[url.path]
                self.send(200, (WEB / file).read_bytes(), mime)
                return
            token = self.headers.get("Authorization", "")
            if not hmac.compare_digest(token.encode(), ("Bearer " + self.server.admin_key).encode()):
                self.server.throttle(self.client_address[0])
                raise ApiError(401, "Enter the local admin key.")
            if self.command == "GET" and url.path == "/admin/api/state":
                self.send(200, dashboard_state(self.server.store, parse_qs(url.query)))
                return
            if self.command == "GET" and url.path == "/admin/api/level":
                level_id = parse_qs(url.query).get("id", [""])[0]
                if not level_id:
                    raise ApiError(400, "Select a level.")
                self.send(200, self.server.store.levels("GET", level_id, False, False, {}, ""))
                return
            if self.command == "POST" and url.path == "/admin/api/moderator-login":
                self.server.throttle(self.client_address[0])
                result = self.server.store.auth("login", self.body(), "")
                if not result["isModerator"]:
                    self.server.store.auth("logout", {}, result["idToken"])
                    raise ApiError(403, "Sign in with an account assigned as a moderator.")
                self.send(200, result)
                return
            if self.command == "POST" and url.path == "/admin/api/moderator-logout":
                self.body()
                try:
                    self.server.store.auth("logout", {}, self.headers.get("X-Moderator-Token", ""))
                except ApiError as error:
                    if error.status != 401:
                        raise
                self.send(200, {"ok": True})
                return
            if self.command == "POST" and url.path == "/admin/api/rate":
                body = self.body()
                level_id = body.get("level_id")
                if not isinstance(level_id, str):
                    raise ApiError(400, "Select a level.")
                self.send(200, self.server.store.rate_difficulty(level_id, body, self.headers.get("X-Moderator-Token", "")))
                return
            if self.command == "POST" and url.path in {"/admin/api/moderator", "/admin/api/suspension", "/admin/api/warn", "/admin/api/delete-level", "/admin/api/version-block"}:
                self.send(200, admin_action(self.server.store, url.path.rsplit("/", 1)[1], self.body()))
                return
            raise ApiError(404, "Not found.")
        except ApiError as error:
            self.send(error.status, {"error": {"message": error.message}})
        except (BrokenPipeError, ConnectionResetError, TimeoutError):
            return
        except Exception:
            import logging
            logging.exception("Dashboard request failed")
            self.send(500, {"error": {"message": "Dashboard request failed."}})

    do_GET = handle_admin
    do_POST = handle_admin
    do_PUT = handle_admin
    do_DELETE = handle_admin


def main():
    import argparse
    import logging
    from game_server import ROOT
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=int(os.environ.get("ADMIN_PORT", "8081")))
    parser.add_argument("--database", default=os.environ.get("DATABASE_PATH", str(ROOT / "server/data/game.sqlite3")))
    parser.add_argument("--key-file", default=os.environ.get("ADMIN_KEY_FILE", ""))
    args = parser.parse_args()
    key_file = Path(args.key_file) if args.key_file else Path(args.database).parent / "admin-key"
    server = AdminServer(("127.0.0.1", args.port), args.database, load_admin_key(key_file))
    logging.basicConfig(level=logging.INFO)
    logging.info("Admin dashboard: http://127.0.0.1:%s (key file: %s)", server.server_port, key_file)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
