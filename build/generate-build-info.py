"""Generate the native build identity from the same config/template as CMake."""
import argparse
from datetime import datetime
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parent.parent


def generate(config_path, template_path, output_path):
    config = json.loads(Path(config_path).read_text(encoding="utf-8-sig"))
    version = config.get("version")
    version_id = config.get("version_id")
    if not isinstance(version, str) or not re.fullmatch(r"[A-Za-z0-9.+-]+", version):
        raise ValueError("config version must be a nonempty version string")
    if type(version_id) is not int or not 1 <= version_id <= 2147483647:
        raise ValueError("config version_id must be a positive 32-bit integer")
    now = datetime.now().astimezone()
    values = {
        "PLATFORMER_APP_VERSION_STRING": version,
        "PLATFORMER_APP_VERSION_ID": str(version_id),
        "PLATFORMER_BUILD_TIMESTAMP": now.strftime("%Y-%m-%d %H:%M:%S"),
        "PLATFORMER_BUILD_TIMEZONE": now.strftime("%Z (%z)"),
    }
    text = Path(template_path).read_text(encoding="utf-8")
    for key, value in values.items():
        text = text.replace("@" + key + "@", value.replace("\\", "\\\\").replace('"', '\\"'))
    if re.search(r"@[A-Za-z0-9_]+@", text):
        raise ValueError("unresolved build-info template placeholder")
    output = Path(output_path)
    output.parent.mkdir(parents=True, exist_ok=True)
    if not output.exists() or output.read_text(encoding="utf-8") != text:
        output.write_text(text, encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, default=ROOT / "assets/config.json")
    parser.add_argument("--output", type=Path, default=ROOT / ".build/generated/BuildInfo.h")
    args = parser.parse_args()
    generate(args.config, ROOT / "cmake/BuildInfo.h.in", args.output)
