import datetime as dt
import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('channels', Path(__file__).with_name('update-channels.py'))
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)

class ChannelsTest(unittest.TestCase):
    def test_weekly_anchor(self):
        self.assertIsNone(c.boundary(c.ANCHOR - dt.timedelta(seconds=1)))
        for days, expected in [(0, 0), (6, 0), (7, 7), (13, 7), (14, 14)]:
            self.assertEqual(c.boundary(c.ANCHOR + dt.timedelta(days=days)), c.ANCHOR + dt.timedelta(days=expected))

    def test_pin_excludes_future_and_incomplete_builds(self):
        releases = [dict(tag_name=f'build-{n}-1-linux-x64-full', draft=False,
                         assets=[{'name': 'update-manifest.json'}] if n != 3 else []) for n in (1, 2, 3)]
        manifests = {
            releases[0]['tag_name']: dict(version_id=1, built_at='2026-11-09T23:59:00+00:00', commit='old'),
            releases[1]['tag_name']: dict(version_id=2, built_at='2026-11-10T00:01:00+00:00', commit='new')}
        import json
        with patch.object(c, 'gh', return_value=json.dumps([releases])), \
             patch.object(c, 'read_manifest', side_effect=lambda tag: manifests[tag]), \
             patch.object(c, 'release') as create, patch.object(c, 'upload_manifest') as upload:
            c.promote(dt.datetime(2026, 11, 14, tzinfo=c.UTC))
        self.assertEqual(upload.call_count, 2)
        self.assertEqual(upload.call_args.args[1]['version_id'], 1)
        self.assertEqual(upload.call_args.args[1]['snapshot_date'], '2026-11-10')
        self.assertEqual(create.call_args.args, ('prerelease-linux-x64-full', 'old'))

    def test_older_build_cannot_replace_newer_nightly(self):
        import tempfile
        with tempfile.TemporaryDirectory() as directory:
            package = Path(directory) / 'installer.exe'
            package.write_bytes(b'package')
            with patch.dict(c.os.environ, GITHUB_RUN_NUMBER='12', GITHUB_RUN_ATTEMPT='2', GITHUB_SHA='commit'), \
                 patch.object(c, 'release'), patch.object(c, 'gh'), \
                 patch.object(c, 'read_manifest', return_value={'version_id': 13}), \
                 patch.object(c, 'upload_manifest') as upload:
                c.publish('windows-x86', 'lite', package)
            upload.assert_called_once()
            tag, manifest = upload.call_args.args
            self.assertEqual(tag, 'build-12-2-windows-x86-lite')
            self.assertEqual(manifest['edition'], 'lite')
            self.assertEqual(set(manifest['installer_urls']), {'windows'})
            self.assertEqual(set(manifest['installer_urls']['windows']), {'x86'})
            self.assertIn('/build-12-2-windows-x86-lite/', manifest['installer_urls']['windows']['x86'])

    def test_failed_package_upload_does_not_advance_channel(self):
        import tempfile
        with tempfile.TemporaryDirectory() as directory:
            package = Path(directory) / 'game.apk'
            package.write_bytes(b'package')
            with patch.dict(c.os.environ, GITHUB_RUN_NUMBER='12', GITHUB_SHA='commit'), \
                 patch.object(c, 'release'), \
                 patch.object(c, 'gh', side_effect=c.subprocess.CalledProcessError(1, 'gh')), \
                 patch.object(c, 'upload_manifest') as upload:
                with self.assertRaises(c.subprocess.CalledProcessError):
                    c.publish('android-universal', 'full', package)
            upload.assert_not_called()

    def test_existing_weekly_pin_is_reused(self):
        import json
        release = dict(tag_name='prerelease-2026-11-10-linux-x64-full', draft=False,
                       assets=[{'name': 'update-manifest.json'}])
        manifest = dict(version_id=10, commit='pinned')
        with patch.object(c, 'gh', return_value=json.dumps([[release]])), \
             patch.object(c, 'read_manifest', return_value=manifest), \
             patch.object(c, 'release'), patch.object(c, 'upload_manifest') as upload:
            c.promote(c.ANCHOR)
        upload.assert_called_once_with('prerelease-linux-x64-full', manifest)

if __name__ == '__main__':
    unittest.main()
