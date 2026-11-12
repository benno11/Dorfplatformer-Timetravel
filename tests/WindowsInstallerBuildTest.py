"""Exercise the production installer target without needing Windows or Inno Setup."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


@unittest.skipUnless(shutil.which("cmake") and shutil.which("ninja"), "CMake and Ninja are required")
class WindowsInstallerBuildTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="installer regression ")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        source = (Path(__file__).resolve().parents[1] / "CMakeLists.txt").read_text()
        start = source.index("  if(INNO_SETUP_COMPILER)")
        end = source.index("\n  endif()", start) + len("\n  endif()")
        self.target = source[start:end]

    def configure(self, compiler, lite=False):
        output = self.root / "dist/windows-installer/output"
        output.mkdir(parents=True, exist_ok=True)
        self.installer = output / ("installer-lite.exe" if lite else "installer.exe")
        script = self.root / "installer/windows/df-platformer.iss"
        script.parent.mkdir(parents=True, exist_ok=True)
        script.write_text("; installer regression fixture")
        self.installer.write_text("old installer")
        # A future timestamp must not make an old installer count as a fresh build.
        os.utime(self.installer, (4102444800, 4102444800))
        (self.root / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.16)\nproject(InstallerRegression NONE)\n'
            'add_custom_target(windows-package)\n'
            f'set(INNO_SETUP_COMPILER "{compiler}")\n'
            f'set(PLATFORMER_LITE_EDITION {"ON" if lite else "OFF"})\n'
            'set(PLATFORMER_APP_VERSION_STRING "3.0.0 Alpha 3")\n'
            'set(PLATFORMER_APP_VERSION_ID 123)\n'
            f'set(PLATFORMER_WINDOWS_DIST_DIR "{(self.root / "dist/windows-installer").as_posix()}")\n'
            f'set(PLATFORMER_WINDOWS_INSTALLER_OUTPUT "{self.installer.as_posix()}")\n'
            + self.target + '\n'
        )
        subprocess.run(["cmake", "-G", "Ninja", "-S", str(self.root), "-B", str(self.root / "build")],
                       check=True, capture_output=True)

    def build(self):
        return subprocess.run(["cmake", "--build", str(self.root / "build"),
                               "--target", "windows-installer"], capture_output=True, text=True)

    @unittest.skipIf(os.name == "nt", "The fake compiler uses a POSIX shell")
    def test_full_and_lite_always_rebuild_existing_installers(self):
        for lite in (False, True):
            with self.subTest(lite=lite):
                compiler = self.root / "fake-iscc"
                # Behave like Inno Setup: only the explicit /O and /F arguments
                # determine where the executable is written.
                compiler.write_text('#!/bin/sh\noutput=\nname=\n'
                                    'for arg in "$@"; do\n'
                                    '  case "$arg" in /O*) output=${arg#/O};; /F*) name=${arg#/F};; esac\n'
                                    'done\n'
                                    'test -n "$output" && test -n "$name" || exit 9\n'
                                    'cat "$(dirname "$0")/payload" > "$output/$name.exe"\n')
                compiler.chmod(0o755)
                self.configure(compiler.as_posix(), lite)
                payload = self.root / "payload"
                payload.write_text("current build")
                result = self.build()
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual(self.installer.read_text(), "current build")
                payload.write_text("next build")
                result = self.build()
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual(self.installer.read_text(), "next build")

    @unittest.skipIf(os.name == "nt", "The fake compiler uses a POSIX shell")
    def test_compiler_failure_does_not_count_as_a_successful_build(self):
        compiler = self.root / "failed-iscc"
        compiler.write_text("#!/bin/sh\nexit 7\n")
        compiler.chmod(0o755)
        self.configure(compiler.as_posix())
        result = self.build()
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.installer.read_text(), "old installer")

    def test_missing_compiler_fails_even_with_an_old_installer(self):
        self.configure("")
        result = self.build()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Cannot build a fresh Windows installer", result.stdout + result.stderr)
        self.assertEqual(self.installer.read_text(), "old installer")


if __name__ == "__main__":
    unittest.main()
