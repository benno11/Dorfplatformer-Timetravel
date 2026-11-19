#!/usr/bin/env python3
"""Publish immutable SideStore IPAs and nightly/weekly prerelease Pages sources."""
import argparse
import datetime as dt
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile

REPO = 'benno11/Dorfplatformer-Timetravel'
ANCHOR = dt.datetime(2026, 11, 10, tzinfo=dt.timezone.utc)

def gh(*args):
    return subprocess.check_output(['gh', *args], text=True)

def api(path):
    return json.loads(gh('api', f'repos/{REPO}/{path}'))

def boundary(now):
    return ANCHOR + dt.timedelta(weeks=max(0, (now - ANCHOR).days // 7))

def select_runs(runs, jobs, cutoff):
    successful = []
    for run in sorted(runs, key=lambda r: r['run_number'], reverse=True):
        if (run['event'] not in ('push', 'workflow_dispatch') or
            run['head_repository']['full_name'].lower() != REPO.lower() or run['run_number'] < 417):
            continue
        device = [j for j in jobs(run) if j['name'] == 'iOS Package (iphoneos)' and j['conclusion'] == 'success']
        if device:
            successful.append({**run, 'ios_completed_at': device[-1]['completed_at']})
    if not successful:
        raise ValueError('No successful supported iOS device build is available')
    eligible = [r for r in successful if dt.datetime.fromisoformat(r['ios_completed_at'].replace('Z', '+00:00')) <= cutoff]
    # Bootstrap the first supported week without selecting a pre-codec IPA.
    return successful[0], max(eligible, key=lambda r: r['run_number']) if eligible else successful[-1]

def release_info(tag):
    result = subprocess.run(['gh', 'release', 'view', tag, '--repo', REPO, '--json', 'isDraft,assets'], capture_output=True, text=True)
    if result.returncode:
        if 'release not found' in result.stderr.lower() or 'not found' in result.stderr.lower():
            return None
        raise RuntimeError(result.stderr)
    return json.loads(result.stdout)

def publish_ipa(run, module, output, channel):
    build = run['run_number']
    tag = f'sidestore-build-{build}'
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        gh('release', 'download', f'draft-main-{build}', '--repo', REPO,
           '--pattern', 'platformer-ios-device.ipa', '--pattern', 'platformer-ios-device-manifest.json', '--dir', directory)
        ipa = root / 'platformer-ios-device.ipa'
        source = module.generate(ipa, f'https://github.com/{REPO}/releases/download/{tag}/platformer.ipa',
                                 run.get('ios_completed_at', run['updated_at']), output, channel)
        version = source['apps'][0]['versions'][0]
        manifest = json.loads((root / 'platformer-ios-device-manifest.json').read_text())
        if manifest['ipa']['sha256'] != version['sha256'] or manifest['ipa']['size_bytes'] != version['size']:
            raise ValueError('IPA does not match its build manifest')
        if version['buildVersion'] != str(build):
            raise ValueError('IPA build does not match the selected workflow run')
        existing = release_info(tag)
        if existing is None:
            gh('release', 'create', tag, '--repo', REPO, '--target', run['head_sha'], '--prerelease', '--draft',
               '--title', f'iOS SideStore build {build}', '--notes', 'Verified iOS device IPA. Re-sign with SideStore to install.')
            existing = {'isDraft': True, 'assets': []}
        asset = root / 'platformer.ipa'; ipa.rename(asset)
        if not any(a['name'] == 'platformer.ipa' for a in existing['assets']):
            gh('release', 'upload', tag, str(asset), '--repo', REPO)
        else:
            downloaded = root / 'existing'; downloaded.mkdir()
            gh('release', 'download', tag, '--repo', REPO, '--pattern', 'platformer.ipa', '--dir', str(downloaded))
            if hashlib.sha256((downloaded / 'platformer.ipa').read_bytes()).hexdigest() != version['sha256']:
                raise ValueError('Existing immutable SideStore IPA differs from the source IPA')
        if existing['isDraft']:
            gh('release', 'edit', tag, '--repo', REPO, '--draft=false')
        print(f'{channel}: verified iOS build {build}')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    cutoff = boundary(dt.datetime.now(dt.timezone.utc))
    # Paginate so weeks with many builds cannot hide the weekly candidate.
    pages = json.loads(gh('api', '--paginate', '--slurp', f'repos/{REPO}/actions/workflows/android-build.yml/runs?branch=main&per_page=100'))
    runs = [run for page in pages for run in page['workflow_runs'] if run['run_number'] >= 417]
    nightly, candidate = select_runs(runs, lambda r: api(f"actions/runs/{r['id']}/jobs?per_page=100")['jobs'], cutoff)
    spec = importlib.util.spec_from_file_location('sidestore', Path(__file__).with_name('sidestore-source.py'))
    module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    output = Path(args.output)
    publish_ipa(nightly, module, output / 'nightly', 'nightly')
    # Persist the weekly choice publicly. Later successful builds cannot move it.
    tag = f'sidestore-prerelease-{cutoff.date()}'
    info = release_info(tag)
    with tempfile.TemporaryDirectory() as directory:
        pin = Path(directory) / 'selection.json'
        if info and any(a['name'] == pin.name for a in info['assets']):
            gh('release', 'download', tag, '--repo', REPO, '--pattern', pin.name, '--dir', directory)
            candidate = json.loads(pin.read_text())
        elif info is None:
            gh('release', 'create', tag, '--repo', REPO, '--target', candidate['head_sha'], '--prerelease', '--draft',
               '--title', f'SideStore prerelease {cutoff.date()}', '--notes', 'Immutable weekly iOS prerelease selection.')
        publish_ipa(candidate, module, output / 'prerelease', 'prerelease')
        if not pin.exists():
            pin.write_text(json.dumps(candidate) + '\n')
            gh('release', 'upload', tag, str(pin), '--repo', REPO)
        if info is None or info['isDraft']:
            gh('release', 'edit', tag, '--repo', REPO, '--draft=false')
    module.landing(output, ['prerelease', 'nightly'])

if __name__ == '__main__':
    main()
