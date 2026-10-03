"""Exercise the production CMake dependency resolver without compiling a game."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
MODULE = ROOT / "cmake" / "MinGWDependencies.cmake"


class MinGWDependenciesTests(unittest.TestCase):
    def setUp(self):
        self.workspace = tempfile.TemporaryDirectory(prefix="pd-mingw-path-")
        self.addCleanup(self.workspace.cleanup)
        self.base = Path(self.workspace.name)
        self.prefix = self.base / "runner temp" / "msys64" / "mingw64"
        (self.prefix / "bin").mkdir(parents=True)
        (self.prefix / "lib").mkdir()
        self.compiler = self.prefix / "bin" / "cc.exe"
        self.compiler.touch()

    def resolve(self, prefix=None):
        prefix = prefix or self.prefix
        # Poisoned environment hints must not redirect dependency ownership.
        decoy = self.base / "unselected" / "mingw64"
        (decoy / "lib").mkdir(parents=True, exist_ok=True)
        (decoy / "lib" / "libwinpthread.a").touch()
        env = dict(os.environ, MINGW_PREFIX=decoy.as_posix())
        script = self.base / "check.cmake"
        script.write_text(
            "cmake_minimum_required(VERSION 3.16)\n"
            f'set(CMAKE_C_COMPILER "{(prefix / "bin" / "cc.exe").as_posix()}")\n'
            f'include("{MODULE.as_posix()}")\n'
            'file(WRITE "${CMAKE_CURRENT_LIST_DIR}/paths.txt" '
            '"${_msys_lib}\n${PD_WINPTHREAD_STATIC_LIB}\n${PD_WINPTHREAD_RUNTIME_DLL}\n")\n',
            encoding="utf-8",
        )
        return subprocess.run(
            ["cmake", "-P", str(script)], capture_output=True, text=True,
            env=env, timeout=15,
        )

    def test_relocated_prefix_and_spaces_preserve_exact_static_and_runtime_paths(self):
        (self.prefix / "lib" / "libwinpthread.a").touch()
        (self.prefix / "lib" / "libwinpthread.dll.a").touch()
        (self.prefix / "bin" / "libwinpthread-1.dll").touch()
        result = self.resolve()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((self.base / "paths.txt").read_text().splitlines(), [
            (self.prefix / "lib").as_posix(),
            (self.prefix / "lib" / "libwinpthread.a").as_posix(),
            (self.prefix / "bin" / "libwinpthread-1.dll").as_posix(),
        ])

    def test_missing_static_archive_rejects_import_stub_and_other_prefix(self):
        (self.prefix / "lib" / "libwinpthread.dll.a").touch()
        (self.prefix / "bin" / "libwinpthread-1.dll").touch()
        result = self.resolve()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Missing static winpthread library:", result.stderr)
        self.assertIn("libwinpthread.a", result.stderr)

    def test_missing_runtime_dll_still_fails_configuration(self):
        (self.prefix / "lib" / "libwinpthread.a").touch()
        result = self.resolve()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Missing winpthread runtime DLL:", result.stderr)

    def test_second_compiler_replaces_previously_selected_prefix(self):
        (self.prefix / "lib" / "libwinpthread.a").touch()
        (self.prefix / "bin" / "libwinpthread-1.dll").touch()
        self.assertEqual(self.resolve().returncode, 0)
        other = self.base / "second toolchain" / "mingw64"
        (other / "bin").mkdir(parents=True)
        (other / "lib").mkdir()
        (other / "bin" / "cc.exe").touch()
        (other / "lib" / "libwinpthread.a").touch()
        (other / "bin" / "libwinpthread-1.dll").touch()
        result = self.resolve(other)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((self.base / "paths.txt").read_text().splitlines()[0],
                         (other / "lib").as_posix())


if __name__ == "__main__":
    unittest.main()
