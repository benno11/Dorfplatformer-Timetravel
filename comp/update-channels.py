#!/usr/bin/env python3
"""Publish successful platform packages and promote immutable weekly snapshots."""
import argparse
import datetime as dt
import json
import os
from pathlib import Path
import subprocess
import tempfile

REPO = 'benno11/Dorfplatformer-Timetravel'
UTC = dt.timezone.utc
ANCHOR = dt.datetime(2026, 11, 10, tzinfo=UTC)
TARGETS = ['windows-x64', 'windows-x86', 'linux-x64', 'android-universal', 'ios-arm64']


def gh(*args):
    return subprocess.check_output(['gh', *args], text=True)


def boundary(now):
    if now < ANCHOR:
        return None
    return ANCHOR + dt.timedelta(weeks=(now - ANCHOR).days // 7)


def release(tag, commit):
    try:
        return json.loads(gh('api', f'repos/{REPO}/releases/tags/{tag}'))
    except subprocess.CalledProcessError:
        gh('release', 'create', tag, '--repo', REPO, '--target', commit,
           '--prerelease', '--title', tag, '--notes', 'Automated update channel build.', '--latest=false')
        return json.loads(gh('api', f'repos/{REPO}/releases/tags/{tag}'))


def upload_manifest(tag, manifest):
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / 'update-manifest.json'
        path.write_text(json.dumps(manifest, indent=2) + '\n')
        gh('release', 'upload', tag, str(path), '--repo', REPO, '--clobber')


def read_manifest(tag):
    data = json.loads(gh('api', f'repos/{REPO}/releases/tags/{tag}'))
    asset = next(a for a in data['assets'] if a['name'] == 'update-manifest.json')
    return json.loads(gh('api', asset['url'], '-H', 'Accept: application/octet-stream'))


def publish(target, edition, package):
    run = os.environ['GITHUB_RUN_NUMBER']
    commit = os.environ['GITHUB_SHA']
    key = f'{target}-{edition}'
    tag = f"build-{run}-{os.environ.get('GITHUB_RUN_ATTEMPT', '1')}-{key}"
    release(tag, commit)
    gh('release', 'upload', tag, str(package), '--repo', REPO, '--clobber')
    os_name, arch = target.split('-')
    config = json.loads(Path('assets/config.json').read_text())
    manifest = {'version': config['version'], 'version_id': int(run),
                'sandbox_version': Path('src/SandboxVersion.h').read_text().split('version[] = "')[1].split('"')[0], 'commit': commit,
                'built_at': dt.datetime.now(UTC).isoformat(), 'target': target, 'edition': edition,
                'installer_urls': {os_name: {arch: f'https://github.com/{REPO}/releases/download/{tag}/{package.name}'}},
                'notes': f'{key}, build {run}'}
    # Android APK contains all supported ABIs.
    if os_name == 'android':
        manifest['installer_urls'][os_name] = {'default': next(iter(manifest['installer_urls'][os_name].values()))}
    upload_manifest(tag, manifest)
    channel = f'nightly-{key}'
    release(channel, commit)
    try:
        previous = read_manifest(channel)
    except (subprocess.CalledProcessError, StopIteration):
        previous = {}
    if int(previous.get('version_id', -1)) <= int(run):
        upload_manifest(channel, manifest)


def promote(now):
    cutoff = boundary(now)
    if cutoff is None:
        return
    pages = json.loads(gh('api', '--paginate', '--slurp', f'repos/{REPO}/releases?per_page=100'))
    releases = [r for page in pages for r in page if not r['draft']]
    for target in TARGETS:
        for edition in ('full', 'lite'):
            if target == 'ios-arm64' and edition == 'lite':
                continue
            key = f'{target}-{edition}'
            snapshot = f'prerelease-{cutoff.date()}-{key}'
            existing = next((r for r in releases if r['tag_name'] == snapshot and
                             any(a['name'] == 'update-manifest.json' for a in r['assets'])), None)
            if existing:
                manifest = read_manifest(snapshot)
            else:
                candidates = [r for r in releases if r['tag_name'].startswith('build-') and
                              r['tag_name'].endswith('-' + key)]
                manifests = [read_manifest(r['tag_name']) for r in candidates
                             if any(a['name'] == 'update-manifest.json' for a in r['assets'])]
                manifests = [m for m in manifests if dt.datetime.fromisoformat(m['built_at']) <= cutoff]
                if not manifests:
                    print(f'No successful build at {cutoff.isoformat()} for {key}; keeping previous pin.')
                    continue
                manifest = max(manifests, key=lambda m: int(m['version_id']))
                manifest = {**manifest, 'snapshot_date': str(cutoff.date())}
                release(snapshot, manifest['commit'])
                upload_manifest(snapshot, manifest)
            channel = f'prerelease-{key}'
            release(channel, manifest['commit'])
            upload_manifest(channel, manifest)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=['publish', 'promote'])
    parser.add_argument('--target', choices=TARGETS)
    parser.add_argument('--edition', choices=['full', 'lite'], default='full')
    parser.add_argument('--package', type=Path)
    args = parser.parse_args()
    if args.mode == 'publish':
        if not args.target or not args.package or not args.package.is_file():
            parser.error('publish needs --target and an existing --package')
        publish(args.target, args.edition, args.package)
    else:
        promote(dt.datetime.now(UTC))
