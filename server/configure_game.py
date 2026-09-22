"""Point packaged game configs at a custom server before building."""
import argparse
import json
from pathlib import Path
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_UPDATE_MANIFEST_URL = "https://benno111.github.io/Dorfplatformer-API/update-manifest.json"


def configure(base, update_manifest_url=DEFAULT_UPDATE_MANIFEST_URL):
    parsed = urlsplit(base)
    if parsed.scheme not in ("http", "https") or not parsed.hostname or parsed.username or parsed.password:
        raise ValueError("Use an http(s) server URL without embedded credentials.")
    if parsed.query or parsed.fragment or parsed.path not in ("", "/"):
        raise ValueError("Use the server origin without a path, query or fragment.")
    manifest = urlsplit(update_manifest_url)
    if manifest.scheme not in ("http", "https") or not manifest.hostname or manifest.username or manifest.password:
        raise ValueError("Use an http(s) update manifest URL without embedded credentials.")
    base = base.rstrip("/")
    paths = [ROOT / "assets/config.json", ROOT / "assets/client_settings.json"]
    configs = [json.loads(path.read_text(encoding="utf-8-sig")) for path in paths]
    configs[0].update(level_server_url=base, level_api_url=base, account_manager_url=base + "/",
                      windows_update_manifest_url=update_manifest_url)
    configs[1]["settings"]["network"].update(level_server_url=base,
        level_server_auth_token="", level_server_account_username="")
    for path, config in zip(paths, configs):
        path.write_text(json.dumps(config, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("url")
    parser.add_argument("--update-manifest-url", default=DEFAULT_UPDATE_MANIFEST_URL)
    args = parser.parse_args()
    configure(args.url, args.update_manifest_url)
    print("Game server URLs updated. Rebuild the game to package the new address.")
