#!/usr/bin/env python3
"""Generate a SideStore/AltStore source from the actual device IPA metadata."""
import argparse
import datetime as dt
import hashlib
import html
import json
from pathlib import Path
import plistlib
import shutil
import urllib.parse
import zipfile

REPO = 'benno11/Dorfplatformer-Timetravel'
SITE = 'https://benno11.github.io/Dorfplatformer-Timetravel'

def generate(ipa, download_url, date, output, channel="nightly"):
    if channel not in ("nightly", "prerelease"):
        raise ValueError("Unknown SideStore channel")
    label = channel.title()
    ipa, output = Path(ipa), Path(output)
    if not download_url.startswith('https://github.com/' + REPO + '/releases/download/'):
        raise ValueError('The IPA must have a public GitHub release download URL')
    with zipfile.ZipFile(ipa) as archive:
        names = [n for n in archive.namelist() if n.startswith('Payload/') and n.count('/') == 2 and n.endswith('.app/Info.plist')]
        if len(names) != 1:
            raise ValueError('Expected exactly one iOS app bundle')
        info = plistlib.loads(archive.read(names[0]))
    if info.get('CFBundleIdentifier') != 'com.benno111.dorfplatformertimetravel':
        raise ValueError('Unexpected device app bundle identifier')
    build = str(info['CFBundleVersion'])
    if not build.isdigit():
        raise ValueError('Expected a numeric build version')
    version = dict(version=info['CFBundleShortVersionString'], buildVersion=build, date=date,
                   downloadURL=download_url, size=ipa.stat().st_size,
                   sha256=hashlib.sha256(ipa.read_bytes()).hexdigest(), minOSVersion=info['MinimumOSVersion'])
    source = dict(name='Dorfplatformer Timetravel · ' + label, identifier='com.benno111.dorfplatformertimetravel.source.' + channel,
                  sourceURL=SITE + '/' + channel + '/source.json', subtitle='Official iOS ' + channel + ' builds',
                  website='https://github.com/' + REPO, iconURL=SITE + '/icon.png', tintColor='76E1FF',
                  apps=[dict(name=info.get('CFBundleDisplayName', 'Dorfplatformer Timetravel'),
                             bundleIdentifier=info['CFBundleIdentifier'], developerName='Benno111',
                             localizedDescription='Dorfplatformer Timetravel for iOS. ' + ('Latest successful device build.' if channel == 'nightly' else 'Weekly pinned successful device build.'),
                             iconURL=SITE + '/icon.png', tintColor='76E1FF', versions=[version],
                             version=version['version'], versionDate=date, downloadURL=download_url, size=version['size'])], news=[])
    output.mkdir(parents=True, exist_ok=True)
    (output / '.nojekyll').touch()
    (output / 'source.json').write_text(json.dumps(source, indent=2) + '\n')
    root = Path(__file__).resolve().parent.parent
    shutil.copyfile(root / 'Android/app/src/main/res/drawable/android_icon_192.png', output / 'icon.png')
    add_url = 'sidestore://source?url=' + urllib.parse.quote(source['sourceURL'], safe='')
    (output / 'index.html').write_text(f'''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Dorfplatformer · SideStore</title><style>body{{font:18px system-ui;background:#141a22;color:#edf6ff;max-width:640px;margin:10vh auto;padding:24px;line-height:1.6}}a{{color:#76e1ff}}img{{width:96px;border-radius:20px}}code{{overflow-wrap:anywhere}}</style>
<img src="icon.png" alt="Game icon"><h1>Dorfplatformer Timetravel · {label}</h1><p>Install and update the iOS game through SideStore.</p>
<p><a href="{html.escape(add_url, quote=True)}">Add source to SideStore</a></p><p>Or add this source URL manually:</p><p><a href="source.json"><code>{SITE}/{channel}/source.json</code></a></p>
<p>{html.escape(str(version['version']))} · Build {build} · iOS {html.escape(str(version['minOSVersion']))}+</p>
<p><a href="{html.escape(download_url, quote=True)}">Download IPA</a> · <a href="https://github.com/{REPO}">GitHub</a></p></html>''')
    return source

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ipa', required=True)
    parser.add_argument('--download-url', required=True)
    parser.add_argument('--date', default=dt.datetime.now(dt.timezone.utc).isoformat())
    parser.add_argument('--output', required=True)
    parser.add_argument('--channel', choices=['nightly', 'prerelease'], default='nightly')
    args = parser.parse_args()
    generate(args.ipa, args.download_url, args.date, args.output, args.channel)


def landing(output, channels):
    """The original source.json remains an alias for nightly subscriptions."""
    output = Path(output)
    shutil.copyfile(output / 'nightly/source.json', output / 'source.json')
    shutil.copyfile(output / 'nightly/icon.png', output / 'icon.png')
    (output / '.nojekyll').touch()
    links = []
    for channel in channels:
        url = SITE + '/' + channel + '/source.json'
        add = 'sidestore://source?url=' + urllib.parse.quote(url, safe='')
        links.append(f'<li><strong>{channel.title()}</strong>: <a href="{html.escape(add, quote=True)}">Add to SideStore</a> · <a href="{url}">Source JSON</a></li>')
    (output / 'index.html').write_text('<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Dorfplatformer SideStore channels</title><body><h1>Dorfplatformer Timetravel</h1><p>Choose an update channel. Nightly follows successful iOS builds; prerelease stays pinned for the week.</p><ul>' + ''.join(links) + '</ul><p>Both channels update the same app. Subscribe to your preferred channel to avoid competing updates.</p></body></html>')
