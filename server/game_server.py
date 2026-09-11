"""Self-hosted Dorfplatformer accounts and levels. Python 3.11+, no dependencies."""
import argparse
from contextlib import contextmanager
import hashlib
import hmac
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import logging
import os
from pathlib import Path
import re
import secrets
import sqlite3
import sys
import threading
import time
from urllib.parse import urlsplit, parse_qs

ROOT = Path(__file__).resolve().parent.parent
MAX_BODY = 2 * 1024 * 1024
SESSION_SECONDS = 30 * 24 * 3600
USERNAME = re.compile(r"[A-Za-z0-9_-]{3,48}")
LEVEL_ID = re.compile(r"[A-Za-z0-9_-]{1,160}")


class ApiError(Exception):
    def __init__(self, status, message):
        self.status, self.message = status, message


def password_hash(password, salt):
    return hashlib.scrypt(password.encode(), salt=bytes.fromhex(salt), n=16384, r=8, p=1).hex()


def require_password(value):
    if not isinstance(value, str) or not 10 <= len(value) <= 256:
        raise ApiError(400, "Password must contain 10 to 256 characters.")
    return value


class Store:
    def __init__(self, path):
        self.path = str(path)
        Path(path).parent.mkdir(parents=True, exist_ok=True)
        with self.connect() as db:
            db.executescript("""
                PRAGMA journal_mode=WAL;
                CREATE TABLE IF NOT EXISTS users(
                    id TEXT PRIMARY KEY, email TEXT NOT NULL UNIQUE COLLATE NOCASE,
                    username TEXT NOT NULL UNIQUE COLLATE NOCASE,
                    salt TEXT NOT NULL, password TEXT NOT NULL);
                CREATE TABLE IF NOT EXISTS sessions(
                    token_hash TEXT PRIMARY KEY, user_id TEXT NOT NULL REFERENCES users(id),
                    expires INTEGER NOT NULL);
                CREATE INDEX IF NOT EXISTS sessions_user ON sessions(user_id);
                CREATE TABLE IF NOT EXISTS levels(
                    id TEXT PRIMARY KEY, user_id TEXT NOT NULL REFERENCES users(id),
                    name TEXT NOT NULL, data TEXT NOT NULL, api_version TEXT NOT NULL,
                    uploaded_at INTEGER NOT NULL);
                CREATE TABLE IF NOT EXISTS moderators(
                    user_id TEXT PRIMARY KEY REFERENCES users(id) ON DELETE CASCADE);
                CREATE TABLE IF NOT EXISTS level_difficulties(
                    level_id TEXT PRIMARY KEY REFERENCES levels(id) ON DELETE CASCADE,
                    difficulty INTEGER NOT NULL CHECK(difficulty BETWEEN 1 AND 9),
                    rated_by TEXT NOT NULL REFERENCES users(id), rated_at INTEGER NOT NULL);
                CREATE TABLE IF NOT EXISTS upload_blocks(
                    user_id TEXT PRIMARY KEY REFERENCES users(id) ON DELETE CASCADE,
                    until INTEGER, reason TEXT NOT NULL, created_at INTEGER NOT NULL);
                CREATE TABLE IF NOT EXISTS admin_events(
                    id INTEGER PRIMARY KEY AUTOINCREMENT, action TEXT NOT NULL,
                    target_id TEXT NOT NULL, reason TEXT NOT NULL, actor TEXT NOT NULL,
                    created_at INTEGER NOT NULL);
                CREATE INDEX IF NOT EXISTS admin_events_target ON admin_events(target_id,id);
                CREATE TABLE IF NOT EXISTS blocked_versions(
                    version_id TEXT PRIMARY KEY, reason TEXT NOT NULL, created_at INTEGER NOT NULL);
                PRAGMA user_version=4;
            """)

    def check_client_version(self, user_agent):
        # Browsers remain usable for account management. This is compatibility
        # enforcement for official clients, not authentication or anti-cheat.
        if not user_agent.startswith("DF-New/"):
            return
        match = re.fullmatch(r"DF-New/[A-Za-z0-9.+-]+ \(build/([1-9][0-9]{0,9})\)", user_agent)
        version = match[1] if match else "legacy"
        with self.connect() as db:
            block = db.execute("SELECT reason FROM blocked_versions WHERE version_id=?", (version,)).fetchone()
        if block:
            raise ApiError(426, "This client version is blocked. Please update the game. Reason: " + block["reason"])

    @contextmanager
    def connect(self):
        db = sqlite3.connect(self.path, timeout=10)
        db.row_factory = sqlite3.Row
        db.execute("PRAGMA foreign_keys=ON")
        try:
            with db:
                yield db
        finally:
            db.close()

    @staticmethod
    def user_data(user, db):
        restriction = db.execute("SELECT until,reason FROM upload_blocks WHERE user_id=? AND (until IS NULL OR until>?)",
                                 (user["id"], int(time.time()))).fetchone()
        warning = db.execute("SELECT reason,created_at FROM admin_events WHERE target_id=? AND action='warn' ORDER BY id DESC LIMIT 1",
                             (user["id"],)).fetchone()
        return {"localId": user["id"], "email": user["email"], "displayName": user["username"],
                "isModerator": bool(db.execute("SELECT 1 FROM moderators WHERE user_id=?", (user["id"],)).fetchone()),
                "uploadRestriction": dict(restriction) if restriction else None,
                "warning": dict(warning) if warning else None}

    def session(self, db, user):
        token = secrets.token_urlsafe(32)
        db.execute("DELETE FROM sessions WHERE expires <= ?", (int(time.time()),))
        db.execute("INSERT INTO sessions VALUES(?,?,?)",
                   (hashlib.sha256(token.encode()).hexdigest(), user["id"], int(time.time()) + SESSION_SECONDS))
        return {**self.user_data(user, db), "idToken": token, "expiresIn": SESSION_SECONDS}

    def authenticate(self, db, token):
        if not isinstance(token, str) or not 20 <= len(token) <= 256:
            raise ApiError(401, "Sign in to continue.")
        user = db.execute("""SELECT users.* FROM users JOIN sessions ON users.id=sessions.user_id
            WHERE token_hash=? AND expires>?""",
            (hashlib.sha256(token.encode()).hexdigest(), int(time.time()))).fetchone()
        if not user:
            raise ApiError(401, "Session expired or revoked. Please sign in again.")
        return user

    def auth(self, action, body, token):
        with self.connect() as db:
            if action in ("register", "login"):
                email = body.get("email")
                if not isinstance(email, str) or len(email) > 254 or not re.fullmatch(r"[^\s@]+@[^\s@]+\.[^\s@]+", email):
                    raise ApiError(400, "A valid email address is required.")
                email = email.strip().lower()
                password = require_password(body.get("password"))
                if action == "register":
                    username = body.get("displayName", "")
                    if not isinstance(username, str) or not USERNAME.fullmatch(username):
                        raise ApiError(400, "Username must be 3 to 48 letters, numbers, underscores or hyphens.")
                    salt = secrets.token_hex(16)
                    digest = password_hash(password, salt)
                    try:
                        db.execute("INSERT INTO users VALUES(?,?,?,?,?)",
                                   (secrets.token_hex(16), email, username, salt, digest))
                    except sqlite3.IntegrityError:
                        raise ApiError(409, "Email or username is already registered.") from None
                user = db.execute("SELECT * FROM users WHERE email=?", (email,)).fetchone()
                # Do the same expensive operation for unknown accounts.
                digest = password_hash(password, user["salt"] if user else "00" * 16)
                if not user or not hmac.compare_digest(digest, user["password"]):
                    raise ApiError(401, "Invalid email or password.")
                return self.session(db, user)

            user = self.authenticate(db, token or body.get("idToken", ""))
            if action == "lookup":
                return {"users": [self.user_data(user, db)]}
            if action == "logout":
                db.execute("DELETE FROM sessions WHERE token_hash=?",
                           (hashlib.sha256((token or body.get("idToken", "")).encode()).hexdigest(),))
                return {"ok": True}
            if action == "update":
                current = body.get("currentPassword", "")
                if not isinstance(current, str) or len(current) > 256 or not hmac.compare_digest(
                        password_hash(current, user["salt"]), user["password"]):
                    raise ApiError(401, "Current password is required.")
                if "displayName" in body:
                    username = body["displayName"]
                    if not isinstance(username, str) or not USERNAME.fullmatch(username):
                        raise ApiError(400, "Invalid username.")
                    try:
                        db.execute("UPDATE users SET username=? WHERE id=?", (username, user["id"]))
                    except sqlite3.IntegrityError:
                        raise ApiError(409, "Username is already registered.") from None
                if "password" in body:
                    password = require_password(body["password"])
                    salt = secrets.token_hex(16)
                    db.execute("UPDATE users SET salt=?,password=? WHERE id=?",
                               (salt, password_hash(password, salt), user["id"]))
                db.execute("DELETE FROM sessions WHERE user_id=?", (user["id"],))
                return self.session(db, db.execute("SELECT * FROM users WHERE id=?", (user["id"],)).fetchone())
            raise ApiError(404, "Unknown account action.")

    def set_moderator(self, username, enabled):
        """Operator-only role management; deliberately not exposed over HTTP."""
        with self.connect() as db:
            db.execute("BEGIN IMMEDIATE")
            user = db.execute("SELECT id FROM users WHERE username=?", (username,)).fetchone()
            if not user:
                raise ValueError("Account does not exist.")
            if enabled:
                db.execute("INSERT OR IGNORE INTO moderators VALUES(?)", (user["id"],))
            else:
                db.execute("DELETE FROM moderators WHERE user_id=?", (user["id"],))
            db.execute("INSERT INTO admin_events(action,target_id,reason,actor,created_at) VALUES(?,?,?,?,?)",
                       ("grant_moderator" if enabled else "revoke_moderator", user["id"],
                        "Role updated from server console.", "console", int(time.time())))

    def rate_difficulty(self, level_id, body, token):
        with self.connect() as db:
            db.execute("BEGIN IMMEDIATE")
            user = self.authenticate(db, token)
            if not db.execute("SELECT 1 FROM moderators WHERE user_id=?", (user["id"],)).fetchone():
                raise ApiError(403, "Only moderators can set difficulty.")
            difficulty = body.get("difficulty")
            if type(difficulty) is not int or not 1 <= difficulty <= 9:
                raise ApiError(400, "Difficulty must be an integer from 1 to 9.")
            if not db.execute("SELECT 1 FROM levels WHERE id=?", (level_id,)).fetchone():
                raise ApiError(404, "Level not found.")
            db.execute("""INSERT INTO level_difficulties VALUES(?,?,?,?)
                ON CONFLICT(level_id) DO UPDATE SET difficulty=excluded.difficulty,
                rated_by=excluded.rated_by,rated_at=excluded.rated_at""",
                (level_id, difficulty, user["id"], int(time.time())))
            db.execute("INSERT INTO admin_events(action,target_id,reason,actor,created_at) VALUES(?,?,?,?,?)",
                       ("rate_level", level_id, "Difficulty set to " + str(difficulty), user["id"], int(time.time())))
            return {"level_id": level_id, "difficulty": difficulty}

    def levels(self, method, level_id, data_only, shallow, body, token, metadata=False):
        with self.connect() as db:
            if method == "GET":
                if not level_id:
                    if shallow:
                        return {row["id"]: True for row in db.execute("SELECT id FROM levels ORDER BY id")}
                    fields = "levels.id,levels.name,levels.api_version,levels.uploaded_at" if metadata else "levels.*"
                    rows = db.execute(f"""SELECT {fields},users.username,d.difficulty FROM levels
                        JOIN users ON users.id=levels.user_id
                        LEFT JOIN level_difficulties d ON d.level_id=levels.id ORDER BY levels.id""").fetchall()
                    return {r["id"]: self.level_data(r, include_data=not metadata) for r in rows}
                row = db.execute("""SELECT levels.*,users.username,d.difficulty FROM levels JOIN users
                    ON users.id=levels.user_id LEFT JOIN level_difficulties d ON d.level_id=levels.id
                    WHERE levels.id=?""", (level_id,)).fetchone()
                if not row:
                    raise ApiError(404, "Level not found.")
                return row["data"] if data_only else self.level_data(row)
            if not level_id or data_only:
                raise ApiError(405, "Write a complete level object.")
            user = self.authenticate(db, token)
            # Serialize ownership checks with writes so concurrent uploads cannot claim another user's ID.
            db.execute("BEGIN IMMEDIATE")
            existing = db.execute("SELECT user_id,data FROM levels WHERE id=?", (level_id,)).fetchone()
            if existing and existing["user_id"] != user["id"]:
                raise ApiError(403, "This level belongs to another account.")
            if method == "DELETE":
                if not existing:
                    raise ApiError(404, "Level not found.")
                db.execute("DELETE FROM levels WHERE id=?", (level_id,))
                return {"ok": True}
            if method != "PUT":
                raise ApiError(405, "Method not allowed.")
            restriction = db.execute("SELECT reason FROM upload_blocks WHERE user_id=? AND (until IS NULL OR until>?)",
                                     (user["id"], int(time.time()))).fetchone()
            if restriction:
                raise ApiError(403, "Uploads suspended: " + restriction["reason"])
            if "difficulty" in body:
                raise ApiError(403, "Use the moderator difficulty endpoint to set ratings.")
            name, data = body.get("name"), body.get("data")
            if not isinstance(name, str) or not 1 <= len(name.strip()) <= 160:
                raise ApiError(400, "Level name must contain 1 to 160 characters.")
            if not isinstance(data, str) or not data.strip() or len(data.encode()) > MAX_BODY - 4096:
                raise ApiError(400, "Level data must be nonempty text smaller than 2 MiB.")
            version = body.get("api_version_id", "1")
            if isinstance(version, bool) or not isinstance(version, (str, int)) or len(str(version)) > 32:
                raise ApiError(400, "Invalid API version.")
            if not existing and db.execute("SELECT count(*) FROM levels WHERE user_id=?", (user["id"],)).fetchone()[0] >= 100:
                raise ApiError(409, "Account level limit reached (100).")
            db.execute("""INSERT INTO levels VALUES(?,?,?,?,?,?)
                ON CONFLICT(id) DO UPDATE SET name=excluded.name,data=excluded.data,
                api_version=excluded.api_version,uploaded_at=excluded.uploaded_at""",
                (level_id, user["id"], name.strip(), data, str(version), int(time.time())))
            if existing and existing["data"] != data:
                db.execute("DELETE FROM level_difficulties WHERE level_id=?", (level_id,))
            return {"level_id": level_id, "owner": user["username"], "ok": True}

    @staticmethod
    def level_data(row, include_data=True):
        result = {"level_id": row["id"], "owner": row["username"], "name": row["name"],
                  "difficulty": row["difficulty"], "api_version_id": row["api_version"],
                  "uploaded_at": row["uploaded_at"]}
        if include_data:
            result["data"] = row["data"]
        return result


class GameServer(ThreadingHTTPServer):
    daemon_threads = True
    def __init__(self, address, database, public_url="", releases=None, handler=None):
        self.releases = Path(releases or ROOT / "server/releases").resolve()
        self.store = Store(database)
        self.public_url = public_url.rstrip("/")
        self.slots = threading.BoundedSemaphore(32)
        self.rate_lock = threading.Lock()
        self.attempts = {}
        super().__init__(address, handler or Handler)

    def process_request(self, request, client_address):
        if not self.slots.acquire(blocking=False):
            request.close()
            return
        try:
            super().process_request(request, client_address)
        except Exception:
            self.slots.release()
            raise

    def process_request_thread(self, request, client_address):
        try:
            super().process_request_thread(request, client_address)
        finally:
            self.slots.release()

    def throttle(self, ip):
        now = time.monotonic()
        with self.rate_lock:
            self.attempts = {key: value for key, value in self.attempts.items() if value[1] > now}
            count, expiry = self.attempts.get(ip, (0, now + 60))
            if count >= 20 or (ip not in self.attempts and len(self.attempts) >= 10000):
                raise ApiError(429, "Too many account requests. Try again in a minute.")
            self.attempts[ip] = (count + 1, expiry)


class Handler(BaseHTTPRequestHandler):
    server_version = "DorfServer/1"
    def setup(self):
        super().setup()
        self.connection.settimeout(15)

    def log_message(self, fmt, *args):
        # Never log request URLs, credentials or session tokens.
        logging.info("request from %s", self.client_address[0])

    def send(self, status, value, content_type="application/json; charset=utf-8"):
        payload = json.dumps(value, ensure_ascii=False).encode() if content_type.startswith("application/json") else value
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Referrer-Policy", "no-referrer")
        self.send_header("X-Frame-Options", "DENY")
        self.end_headers()
        self.wfile.write(payload)

    def body(self):
        if self.headers.get("Transfer-Encoding"):
            raise ApiError(400, "Chunked request bodies are not supported.")
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            raise ApiError(400, "Invalid Content-Length.") from None
        if length < 0 or length > MAX_BODY:
            raise ApiError(413, "Request body exceeds 2 MiB.")
        if self.headers.get_content_type() != "application/json":
            raise ApiError(415, "Use application/json.")
        try:
            raw = self.rfile.read(length)
            if len(raw) != length:
                raise ValueError()
            data = json.loads(raw, parse_constant=lambda _: (_ for _ in ()).throw(ValueError()))
        except (ValueError, UnicodeDecodeError, RecursionError):
            raise ApiError(400, "Invalid JSON body.") from None
        if not isinstance(data, dict):
            raise ApiError(400, "JSON body must be an object.")
        return data

    def handle_api(self):
        try:
            url = urlsplit(self.path)
            path = url.path
            if path == "/api.json" or path.startswith("/api/auth/") or path.startswith("/levels"):
                self.server.store.check_client_version(self.headers.get("User-Agent", ""))
            token = self.headers.get("Authorization", "")
            token = token[7:] if token.startswith("Bearer ") else ""
            # Legacy level URLs are public reads; credentials are only accepted in headers/body.
            if self.command == "GET" and path == "/health":
                self.send(200, {"status": "ok", "api_version": 1})
                return
            if self.command == "GET" and path == "/api.json":
                self.send(200, {"name": "Dorfplatformer custom server", "api_version_id": 1,
                                "level_server_url": self.server.public_url,
                                "account_manager_url": "/", "auth": "opaque bearer sessions"})
                return
            if self.command == "POST" and path.startswith("/api/auth/"):
                self.server.throttle(self.client_address[0])
                self.send(200, self.server.store.auth(path.rsplit("/", 1)[1], self.body(), token))
                return
            rating = re.fullmatch(r"/levels/([A-Za-z0-9_-]{1,160})/difficulty", path)
            if rating:
                if self.command != "PUT":
                    raise ApiError(405, "Use PUT to set difficulty.")
                self.send(200, self.server.store.rate_difficulty(rating[1], self.body(), token))
                return
            match = re.fullmatch(r"/levels(?:/([A-Za-z0-9_-]{1,160})(/data)?)?\.json", path)
            if match:
                body = self.body() if self.command == "PUT" else {}
                result = self.server.store.levels(self.command, match[1], bool(match[2]),
                          parse_qs(url.query).get("shallow") == ["true"], body, token,
                          metadata=parse_qs(url.query).get("metadata") == ["true"])
                self.send(200, result)
                return
            release_name = None
            if self.command == "GET" and path == "/update-manifest.json":
                release_name = "update-manifest.json"
            elif self.command == "GET" and re.fullmatch(r"/releases/[A-Za-z0-9_-][A-Za-z0-9_.-]*\.(exe|msi|zip|apk|aab)", path):
                release_name = path.rsplit("/", 1)[1]
            if release_name:
                file = (self.server.releases / release_name).resolve()
                if file.parent != self.server.releases or not file.is_file():
                    raise ApiError(404, "Release file not published.")
                with file.open("rb") as stream:
                    self.send_response(200)
                    self.send_header("Content-Type", "application/json" if release_name.endswith(".json") else "application/octet-stream")
                    self.send_header("Content-Length", str(os.fstat(stream.fileno()).st_size))
                    self.send_header("X-Content-Type-Options", "nosniff")
                    self.end_headers()
                    while chunk := stream.read(65536):
                        self.wfile.write(chunk)
                return
            static = {"/": ("index.html", "text/html; charset=utf-8"),
                      "/account-manager.html": ("index.html", "text/html; charset=utf-8"),
                      "/account-manager.js": ("account-manager.js", "text/javascript; charset=utf-8"),
                      "/theme.css": ("theme.css", "text/css; charset=utf-8")}
            if self.command == "GET" and path in static:
                file, mime = static[path]
                self.send(200, (ROOT / "pages" / file).read_bytes(), mime)
                return
            raise ApiError(404, "Not found.")
        except ApiError as error:
            self.send(error.status, {"error": {"message": error.message}})
        except (BrokenPipeError, ConnectionResetError, TimeoutError):
            return
        except Exception:
            logging.exception("Request failed")
            self.send(500, {"error": {"message": "Internal server error."}})

    do_GET = handle_api
    do_POST = handle_api
    do_PUT = handle_api
    do_DELETE = handle_api


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default=os.environ.get("HOST", "127.0.0.1"))
    parser.add_argument("--port", type=int, default=int(os.environ.get("PORT", "8080")))
    parser.add_argument("--database", default=os.environ.get("DATABASE_PATH", str(ROOT / "server/data/game.sqlite3")))
    parser.add_argument("--public-url", default=os.environ.get("PUBLIC_URL", ""))
    parser.add_argument("--releases", default=os.environ.get("RELEASES_PATH", str(ROOT / "server/releases")))
    parser.add_argument("--admin-port", type=int, default=int(os.environ.get("ADMIN_PORT", "8081")))
    parser.add_argument("--no-dashboard", action="store_true")
    parser.add_argument("--admin-key-file", default=os.environ.get("ADMIN_KEY_FILE", ""))
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    server = GameServer((args.host, args.port), args.database, args.public_url, args.releases)
    logging.info("Game server listening on %s:%s", args.host, server.server_port)
    dashboard = None
    dashboard_thread = None
    try:
        if not args.no_dashboard:
            # The embedded Windows Python runtime does not add the script directory.
            sys.path.insert(0, str(Path(__file__).resolve().parent))
            from admin_dashboard import AdminServer, load_admin_key
            key_file = Path(args.admin_key_file) if args.admin_key_file else Path(args.database).parent / "admin-key"
            key = load_admin_key(key_file)
            dashboard = AdminServer(("127.0.0.1", args.admin_port), args.database, key)
            dashboard_thread = threading.Thread(target=dashboard.serve_forever, daemon=True)
            dashboard_thread.start()
            logging.info("Admin dashboard: http://127.0.0.1:%s (key file: %s)", dashboard.server_port, key_file)
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        if dashboard:
            dashboard.shutdown()
            dashboard.server_close()
        if dashboard_thread:
            dashboard_thread.join()
        server.server_close()


if __name__ == "__main__":
    main()
