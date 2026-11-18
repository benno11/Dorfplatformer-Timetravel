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
