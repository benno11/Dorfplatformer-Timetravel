#!/usr/bin/env python3
"""Publish an immutable SideStore IPA and generate its Pages source."""
import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile

REPO = 'benno11/Dorfplatformer-Timetravel'
def gh(*args):
    return subprocess.check_output(['gh', *args], text=True)
def api(path):
    return json.loads(gh('api', f'repos/{REPO}/{path}'))

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    runs = api('actions/workflows/android-build.yml/runs?branch=main&per_page=30')['workflow_runs']
    chosen = None
    for run in sorted(runs, key=lambda r: r['run_number'], reverse=True):
        if run['event'] not in ('push', 'workflow_dispatch') or run['head_repository']['full_name'].lower() != REPO.lower():
            continue
        # Pre-codec builds had present media files but no PNG/MP3 decoding.
        if run['run_number'] < 417:
            continue
        jobs = api(f"actions/runs/{run['id']}/jobs?per_page=100")['jobs']
        if any(j['name'] == 'iOS Package (iphoneos)' and j['conclusion'] == 'success' for j in jobs):
            chosen = run
            break
    if chosen is None:
        raise RuntimeError('No successful main-branch iOS device build is available')
    build = chosen['run_number']
    tag = f'sidestore-build-{build}'
    url = f'https://github.com/{REPO}/releases/download/{tag}/platformer.ipa'
    spec = importlib.util.spec_from_file_location('sidestore_source', Path(__file__).with_name('sidestore-source.py'))
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        gh('release', 'download', f'draft-main-{build}', '--repo', REPO,
           '--pattern', 'platformer-ios-device.ipa', '--pattern', 'platformer-ios-device-manifest.json', '--dir', directory)
        ipa = root / 'platformer-ios-device.ipa'
        manifest = json.loads((root / 'platformer-ios-device-manifest.json').read_text())
        source = module.generate(ipa, url, chosen['updated_at'], args.output)
        version = source['apps'][0]['versions'][0]
        if version['buildVersion'] != str(build) or version['sha256'] != manifest['ipa']['sha256'] or version['size'] != manifest['ipa']['size_bytes']:
            raise RuntimeError('Downloaded IPA does not match the build manifest')
        # Per-build URLs keep already-installed source versions reproducible.
        public = subprocess.run(['gh','release','view',tag,'--repo',REPO], capture_output=True)
        if public.returncode:
            gh('release', 'create', tag, '--repo', REPO, '--target', chosen['head_sha'], '--prerelease', '--draft',
               '--title', f"iOS SideStore build {build}", '--notes', 'iOS device IPA for the official SideStore source. Re-sign with SideStore to install.')
        asset = root / 'platformer.ipa'; ipa.rename(asset)
        existing = json.loads(gh('release', 'view', tag, '--repo', REPO, '--json', 'isDraft,assets'))
        assets = [a for a in existing['assets'] if a['name'] == 'platformer.ipa']
        if not assets:
            gh('release','upload',tag,str(asset),'--repo',REPO)
        else:
            downloaded = root / 'existing'; downloaded.mkdir()
            gh('release','download',tag,'--repo',REPO,'--pattern','platformer.ipa','--dir',str(downloaded))
            import hashlib
            if hashlib.sha256((downloaded / 'platformer.ipa').read_bytes()).hexdigest() != version['sha256']:
                raise RuntimeError('Existing immutable SideStore build differs from the source IPA')
        if existing['isDraft']:
            gh('release', 'edit', tag, '--repo', REPO, '--draft=false')
        print(f"SideStore source generated for verified iOS build {build}")

if __name__ == '__main__':
    main()
