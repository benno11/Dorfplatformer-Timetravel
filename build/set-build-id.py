"""Stamp packaged configs with the GitHub Actions workflow run number."""
import argparse
import json
from pathlib import Path


def stamp(root, build_id):
    if not 1 <= build_id <= 2100000000:
        raise ValueError("build ID must be between 1 and 2100000000")
    paths = [root / "assets/config.json", root / "Android/app/src/main/assets/config.json"]
    configs = [json.loads(path.read_text(encoding="utf-8-sig")) for path in paths]
    for path, config in zip(paths, configs):
        config["version_id"] = build_id
        path.write_text(json.dumps(config, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build_id", type=int)
    args = parser.parse_args()
    stamp(Path(__file__).resolve().parent.parent, args.build_id)
