import importlib.util
import json
from pathlib import Path
import plistlib
import tempfile
import unittest
import zipfile

spec = importlib.util.spec_from_file_location('source', Path(__file__).with_name('sidestore-source.py'))
source = importlib.util.module_from_spec(spec); spec.loader.exec_module(source)

class SourceTests(unittest.TestCase):
    def test_real_plist_formats_and_public_download(self):
        for fmt in (plistlib.FMT_XML, plistlib.FMT_BINARY):
            with self.subTest(fmt=fmt), tempfile.TemporaryDirectory() as directory:
                root = Path(directory); ipa = root / 'game.ipa'
                info = dict(CFBundleIdentifier='com.benno111.dorfplatformertimetravel',
                            CFBundleDisplayName='Dorfplatformer Timetravel', CFBundleVersion='420',
                            CFBundleShortVersionString='3.0.0 Alpha 4 Bugfix 1', MinimumOSVersion='15.0')
                with zipfile.ZipFile(ipa, 'w') as archive:
                    archive.writestr('Payload/platformer.app/Info.plist', plistlib.dumps(info, fmt=fmt))
                result = source.generate(ipa, f'https://github.com/{source.REPO}/releases/download/sidestore-build-420/platformer.ipa', '2026-11-18T12:00:00Z', root / 'site')
                version = result['apps'][0]['versions'][0]
                self.assertEqual(version['buildVersion'], '420')
                self.assertEqual(version['size'], ipa.stat().st_size)
                self.assertEqual(len(version['sha256']), 64)
                self.assertEqual(json.loads((root/'site/source.json').read_text()), result)
                self.assertIn('sidestore://source?', (root/'site/index.html').read_text())
                with self.assertRaises(ValueError):
                    source.generate(ipa, 'https://github.com/private/draft', '', root/'invalid')

if __name__ == '__main__': unittest.main()

publisher_spec = importlib.util.spec_from_file_location('publisher', Path(__file__).with_name('publish-sidestore.py'))
publisher = importlib.util.module_from_spec(publisher_spec); publisher_spec.loader.exec_module(publisher)

class Channels(unittest.TestCase):
    def test_sources_are_distinct_but_update_same_app(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); ipa = root / 'game.ipa'
            with zipfile.ZipFile(ipa, 'w') as archive:
                archive.writestr('Payload/platformer.app/Info.plist', plistlib.dumps(dict(
                    CFBundleIdentifier='com.benno111.dorfplatformertimetravel', CFBundleVersion='450',
                    CFBundleShortVersionString='3.0.0', MinimumOSVersion='15.0')))
            sources = [source.generate(ipa, 'https://github.com/' + source.REPO + '/releases/download/sidestore-build-450/platformer.ipa',
                                       '2026-11-19T00:00:00Z', root / channel, channel)
                       for channel in ('nightly', 'prerelease')]
            self.assertNotEqual(sources[0]['identifier'], sources[1]['identifier'])
            self.assertEqual(sources[0]['apps'][0]['bundleIdentifier'], sources[1]['apps'][0]['bundleIdentifier'])
            self.assertTrue(sources[1]['sourceURL'].endswith('/prerelease/source.json'))
            source.landing(root, ['nightly', 'prerelease'])
            self.assertEqual(json.loads((root / 'source.json').read_text()), sources[0])
            self.assertIn('/prerelease/source.json', (root / 'index.html').read_text())

    def test_platform_success_and_weekly_cutoff(self):
        import datetime as dt
        cutoff = dt.datetime(2026, 11, 17, tzinfo=dt.timezone.utc)
        def run(build):
            return dict(id=build, run_number=build, event='push', head_repository={'full_name': publisher.REPO})
        runs = [run(n) for n in (416, 417, 418, 419, 420)]
        def jobs(run):
            n = run['run_number']
            return [dict(name='iOS Package (iphoneos)', conclusion='failure' if n == 420 else 'success',
                         completed_at='2026-11-16T23:00:00Z' if n <= 418 else '2026-11-18T00:00:00Z')]
        nightly, prerelease = publisher.select_runs(runs, jobs, cutoff)
        self.assertEqual(nightly['run_number'], 419)
        self.assertEqual(prerelease['run_number'], 418)
        nightly, bootstrap = publisher.select_runs(runs, jobs, cutoff - dt.timedelta(days=7))
        self.assertEqual(bootstrap['run_number'], 417)
        self.assertEqual(publisher.boundary(cutoff + dt.timedelta(days=6)), cutoff)


class ManualPrerelease(unittest.TestCase):
    def test_current_week_pin_overrides_weekly_choice(self):
        import datetime as dt
        cutoff = dt.datetime(2026, 11, 17, tzinfo=dt.timezone.utc)
        run = dict(run_number=429, head_sha='chosen')
        pin = dict(pin_mode='manual', snapshot_date='2026-11-17', version_id=429,
                   source_release='draft-main-429', commit='chosen')
        self.assertEqual(publisher.manual_selection(pin, [run], cutoff), run)
        self.assertIsNone(publisher.manual_selection({**pin, 'snapshot_date':'2026-11-10'}, [run], cutoff))
        with self.assertRaises(ValueError):
            publisher.manual_selection(pin, [{**run, 'head_sha':'other'}], cutoff)
