#!/usr/bin/env python3
"""Update the app build id in assets/config.json.

Usage:
  python3 set-build-id.py <build_number>
"""

from __future__ import annotations

import json
import sys
from pathlib import Path


def resolve_config_path() -> Path:
    script_dir = Path(__file__).resolve().parent
    candidate = script_dir / "assets" / "config.json"
    if candidate.exists():
        return candidate
    raise FileNotFoundError("Could not find assets/config.json from the project root.")


def main() -> int:
    if len(sys.argv) < 2:
        print("Usage: python3 set-build-id.py <build_number>", file=sys.stderr)
        return 2

    try:
        build_id = int(sys.argv[1])
    except ValueError:
        print(f"Invalid build id: {sys.argv[1]!r}", file=sys.stderr)
        return 2

    config_path = resolve_config_path()
    with config_path.open("r", encoding="utf-8") as fh:
        data = json.load(fh)

    data["version_id"] = build_id
    with config_path.open("w", encoding="utf-8") as fh:
        json.dump(data, fh, indent=2)
        fh.write("\n")

    print(f"Updated {config_path} with version_id={build_id}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
