"""Grant or revoke moderator access from the server host."""
import argparse
import os
from pathlib import Path
import sqlite3
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from game_server import ROOT, Store


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--grant", metavar="USERNAME")
    action.add_argument("--revoke", metavar="USERNAME")
    parser.add_argument("--database", default=os.environ.get("DATABASE_PATH", str(ROOT / "server/data/game.sqlite3")))
    args = parser.parse_args()
    try:
        if not Path(args.database).is_file():
            raise ValueError("Database does not exist. Start the server and create the account first.")
        username = args.grant or args.revoke
        Store(args.database).set_moderator(username, bool(args.grant))
    except (ValueError, OSError, sqlite3.Error) as error:
        print(str(error), file=sys.stderr)
        return 1
    print(f"Moderator access {'granted to' if args.grant else 'revoked from'} {username}.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
