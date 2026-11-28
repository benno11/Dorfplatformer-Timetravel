import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('linux_libc', Path(__file__).with_name('check-linux-libc.py'))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class LinuxLibcTests(unittest.TestCase):
    def test_version_numbers_are_compared_numerically(self):
        versions = module.required_versions('GLIBC_2.9 GLIBC_2.31 GLIBC_2.2.5 GLIBCXX_3.4.30 GLIBC_2.31')
        self.assertEqual(versions, {(2, 9), (2, 31), (2, 2, 5)})
        self.assertEqual(max(versions), (2, 31))

    def test_compatible_release(self):
        with patch.object(module.subprocess, 'check_output', side_effect=['GLIBC_2.2.5 GLIBC_2.31', 'NEEDED libc.so.6']):
            module.check('game', (2, 31))

    def test_newer_glibc_is_rejected(self):
        with patch.object(module.subprocess, 'check_output', return_value='GLIBC_2.34'):
            with self.assertRaisesRegex(ValueError, 'requires glibc 2.34'):
                module.check('game', (2, 31))

    def test_shared_cpp_runtime_is_rejected(self):
        for library in ['libstdc++.so.6', 'libgcc_s.so.1']:
            with patch.object(module.subprocess, 'check_output', side_effect=['GLIBC_2.31', f'NEEDED {library}']):
                with self.assertRaisesRegex(ValueError, 'statically'):
                    module.check('game', (2, 31))

    def test_missing_glibc_requirements_is_rejected(self):
        with patch.object(module.subprocess, 'check_output', return_value='not an ELF version table'):
            with self.assertRaisesRegex(ValueError, 'no glibc'):
                module.check('game', (2, 31))


if __name__ == '__main__':
    unittest.main()
