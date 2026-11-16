#!/usr/bin/env python3
"""Manually pin a published main build without republishing its packages."""
import base64
import datetime as dt
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

REPO = 'benno11/Dorfplatformer-Timetravel'


def gh(*args):
    return subprocess.check_output(['gh', *args], text=True)


def main():
    source = os.environ['SOURCE_RELEASE']
    match = re.fullmatch(r'draft-main-(\d+)', source)
    if not match:
        raise ValueError('Select a draft-main-N build release.')
    release = json.loads(gh('api', f'repos/{REPO}/releases/tags/{source}'))
    if release['draft']:
        raise ValueError('Publish the source release before selecting it.')
    commit = json.loads(gh('api', f'repos/{REPO}/commits/{source}'))['sha']
    config = json.loads(base64.b64decode(json.loads(gh('api', f'repos/{REPO}/contents/assets/config.json?ref={commit}'))['content']))
    sandbox = json.loads(gh('api', f'repos/{REPO}/contents/src/SandboxVersion.h?ref={commit}'))
    sandbox_version = re.search(r'version\[\] = "([^"]+)"', base64.b64decode(sandbox['content']).decode()).group(1)
    assets = {a['name']: a['browser_download_url'] for a in release['assets']}
    packages = {}
    for edition in ('full', 'lite'):
        suffix = '-lite' if edition == 'lite' else ''
        for target, filename in [('windows-x64', f'platformer-x64-windows{suffix}-installer.exe'),
                                 ('windows-x86', f'platformer-x86-windows{suffix}-installer.exe'),
                                 ('linux-x64', f'platformer-x64-linux{suffix}.zip')]:
            if filename in assets:
                platform, arch = target.split('-')
                packages[(target, edition)] = {platform: {arch: assets[filename]}}
        prefix = 'platformer-android-lite-' if edition == 'lite' else ''
        android = {arch: assets[prefix + f'app-{abi}-release.apk'] for arch, abi in
                   [('arm64', 'arm64-v8a'), ('arm', 'armeabi-v7a'), ('x64', 'x86_64'), ('x86', 'x86')]
                   if prefix + f'app-{abi}-release.apk' in assets}
        if android:
            packages[('android-universal', edition)] = {'android': android}
    if 'platformer-ios-device.ipa' in assets:
        packages[('ios-arm64', 'full')] = {'ios': {'arm64': assets['platformer-ios-device.ipa']}}
    if not packages:
        raise ValueError('No supported packages in the source release.')
    now = dt.datetime.now(dt.timezone.utc)
    anchor = dt.datetime(2026, 11, 10, tzinfo=dt.timezone.utc)
    if now < anchor:
        raise ValueError('The prerelease schedule has not started.')
    snapshot = str((anchor + dt.timedelta(weeks=(now-anchor).days//7)).date())
    existing = set(gh('api', '--paginate', f'repos/{REPO}/releases?per_page=100', '--jq', '.[].tag_name').splitlines())
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / 'update-manifest.json'
        for (target, edition), urls in packages.items():
            manifest = dict(version=config['version'], sandbox_version=sandbox_version, version_id=int(match[1]), channel='prerelease',
                            target=target, edition=edition, commit=commit, source_release=source,
                            source_release_url=release['html_url'], built_at=release['published_at'],
                            snapshot_date=snapshot, pinned_at=now.isoformat(), pin_mode='manual',
                            installer_urls=urls, notes=f'Manually selected prerelease: {source}.')
            path.write_text(json.dumps(manifest, indent=2) + '\n')
            for tag in (f'prerelease-{snapshot}-{target}-{edition}', f'prerelease-{target}-{edition}'):
                if tag not in existing:
                    gh('release', 'create', tag, '--repo', REPO, '--target', commit, '--prerelease',
                       '--title', tag, '--notes', f'Manually pinned to {source}.', '--latest=false')
                gh('release', 'upload', tag, str(path), '--repo', REPO, '--clobber')
                saved = json.loads(gh('api', f'repos/{REPO}/releases/tags/{tag}'))
                asset = next(a for a in saved['assets'] if a['name'] == path.name)
                actual = json.loads(gh('api', asset['url'], '-H', 'Accept: application/octet-stream'))
                if actual != manifest:
                    raise RuntimeError(f'Manifest verification failed: {tag}')
                print(f'Pinned and verified {tag} -> {source}')


if __name__ == '__main__':
    main()
