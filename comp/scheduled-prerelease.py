"""Publish the fixed build only after its scheduled time, then verify all pins."""
import argparse
import datetime as dt
import json
import os
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent
REPO = 'benno11/Dorfplatformer-Timetravel'


def gh(*args):
    return subprocess.check_output(['gh', *args], text=True)


def run(dry=False, now=None):
    plan = json.loads((ROOT / 'prerelease-plan.json').read_text())
    now = now or dt.datetime.now(dt.timezone.utc)
    due = dt.datetime.fromisoformat(plan['publish_at'])
    if not dry and not due <= now < dt.datetime.fromisoformat(plan['expires_at']):
        print('Outside scheduled window.'); return
    source = json.loads(gh('release', 'view', plan['source_release'], '--repo', REPO,
                           '--json', 'targetCommitish,isDraft,isPrerelease,assets'))
    if source['targetCommitish'] != plan['commit']:
        raise ValueError('Source commit changed.')
    names = {a['name'] for a in source['assets']}
    required = set()
    for edition in ('', '-lite'):
        for arch in ('x64', 'x86'):
            required.add(f'platformer-{arch}-windows{edition}-installer.exe')
        required.add(f'platformer-x64-linux{edition}.zip')
        prefix = 'platformer-android-lite-' if edition else ''
        for abi in ('arm64-v8a', 'armeabi-v7a', 'x86_64'):
            required.add(prefix + f'app-{abi}-release.apk')
        required.add(prefix + 'app-x86-release.apk')
    if required - names:
        raise ValueError(f'Missing packages: {required - names}')
    if dry:
        print('Validated packages and commit; no changes.'); return
    if source['isDraft'] or not source['isPrerelease']:
        gh('release', 'edit', plan['source_release'], '--repo', REPO, '--draft=false', '--prerelease', '--latest=false')
    subprocess.run(['python3', str(ROOT / 'pin-prerelease.py')], check=True,
                   env={**os.environ, 'SOURCE_RELEASE': plan['source_release']})


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--dry-run', action='store_true')
    args = parser.parse_args()
    run(args.dry_run)
