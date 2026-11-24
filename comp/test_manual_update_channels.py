import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('manual_channels', Path(__file__).with_name('pin-prerelease.py'))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class ManualChannelTests(unittest.TestCase):
    def test_stable_preserves_other_channels_and_maps_editions(self):
        document = {'channels': {'prerelease': {'version_id': 429}, 'nightly': {'status': 'available'}},
                    'installer_url': 'old', 'installer_urls': {'ios': {'arm64': 'old'}}}
        packages = {('windows-x64', 'full'): {'windows': {'x64': 'full.exe'}},
                    ('windows-x64', 'lite'): {'windows': {'x64': 'lite.exe'}},
                    ('android-universal', 'full'): {'android': {'arm64': 'game.apk'}}}
        result = module.stable_document(document, {'version': '3.0'}, '2.0', 500, packages, 'draft-main-500')
        self.assertEqual(result['installer_url'], 'full.exe')
        self.assertEqual(result['installer_urls'], {'windows': {'x64': 'full.exe'}, 'android': {'arm64': 'game.apk'}})
        self.assertEqual(result['channels']['prerelease'], {'version_id': 429})
        self.assertEqual(result['channels']['nightly'], {'status': 'available'})
        self.assertEqual(result['channels']['stable']['manifest_urls']['lite']['windows']['x64'],
                         f'https://github.com/{module.REPO}/releases/download/stable-windows-x64-lite/update-manifest.json')
        self.assertEqual(result['version_id'], 500)

    def test_no_stale_legacy_installer_for_mobile_only_release(self):
        result = module.stable_document({'installer_url': 'old'}, {'version': '3'}, '2', 500,
                    {('ios-arm64', 'full'): {'ios': {'arm64': 'game.ipa'}}}, 'draft-main-500')
        self.assertNotIn('installer_url', result)

    def test_lite_only_cannot_replace_full_stable_manifest(self):
        with self.assertRaises(ValueError):
            module.stable_document({}, {'version': '3'}, '2', 500,
                {('windows-x64', 'lite'): {'windows': {'x64': 'lite.exe'}}}, 'draft-main-500')


if __name__ == '__main__':
    unittest.main()
