"""Persistent operator settings for the fixed moderator-triggered updater."""
import json
from pathlib import Path
import shutil
import time
from urllib.parse import urlsplit


def validate_config(body):
    fields = {"enabled", "command", "cwd", "timeout_seconds", "manifest_url"}
    if not isinstance(body, dict) or set(body) != fields:
        raise ValueError("Provide enabled, command, cwd, timeout_seconds and manifest_url.")
    if type(body["enabled"]) is not bool:
        raise ValueError("Enabled must be true or false.")
    command = body["command"]
    if (not isinstance(command, list) or len(command) > 128 or
            any(not isinstance(arg, str) or len(arg) > 4096 or "\0" in arg for arg in command)):
        raise ValueError("Command must be an array of up to 128 text arguments.")
    if command and not command[0].strip():
        raise ValueError("Enter an executable or script interpreter.")
    if body["enabled"] and not command:
        raise ValueError("An enabled updater needs an executable.")
    cwd = body["cwd"]
    if not isinstance(cwd, str) or not cwd.strip() or len(cwd) > 4096 or "\0" in cwd:
        raise ValueError("Enter a working directory.")
    try:
        directory = Path(cwd).expanduser().resolve()
        if not directory.is_dir():
            raise ValueError("Working directory does not exist.")
    except (OSError, RuntimeError) as error:
        raise ValueError("Working directory is invalid.") from error
    timeout = body["timeout_seconds"]
    if type(timeout) is not int or not 1 <= timeout <= 86400:
        raise ValueError("Timeout must be an integer from 1 to 86400 seconds.")
    manifest = body["manifest_url"]
    if not isinstance(manifest, str) or len(manifest) > 4096 or any(ord(c) < 33 for c in manifest):
        raise ValueError("Manifest URL must be empty for local hosting, or an HTTP(S) URL.")
    try:
        url = urlsplit(manifest)
        if manifest and (url.scheme not in ("http", "https") or not url.hostname or
                         url.username or url.password or url.fragment):
            raise ValueError("Use an HTTP(S) manifest URL without credentials or a fragment.")
        url.port  # Validate malformed or out-of-range ports too.
    except ValueError as error:
        raise ValueError("Manifest URL is invalid.") from error
    resolved_command = list(command)
    if body["enabled"]:
        executable = command[0]
        if Path(executable).is_absolute() or "/" in executable or "\\" in executable:
            executable = str((directory / executable).resolve())
        executable = shutil.which(executable)
        if not executable:
            raise ValueError("Executable was not found. Use an absolute path or a command on PATH.")
        resolved_command[0] = executable
    return {"enabled": body["enabled"], "command": resolved_command, "cwd": str(directory),
            "timeout_seconds": timeout, "manifest_url": manifest}


def read_config(store, defaults):
    with store.connect() as db:
        row = db.execute("SELECT config FROM updater_configuration WHERE id=1").fetchone()
    return json.loads(row["config"]) if row else dict(defaults)


def save_config(store, config):
    with store.connect() as db:
        db.execute("INSERT INTO updater_configuration VALUES(1,?) ON CONFLICT(id) DO UPDATE SET config=excluded.config",
                   (json.dumps(config),))
        db.execute("INSERT INTO admin_events(action,target_id,reason,actor,created_at) VALUES(?,?,?,?,?)",
                   ("configure_server_updater", "server", "Updater settings saved; " +
                    ("enabled." if config["enabled"] else "disabled."), "dashboard", int(time.time())))


def last_run_event(store):
    with store.connect() as db:
        row = db.execute("SELECT action,reason,created_at FROM admin_events WHERE action IN "
                         "('server_update_requested','server_update_succeeded','server_update_failed') "
                         "ORDER BY id DESC LIMIT 1").fetchone()
    return dict(row) if row else None
