"""CMake configuration checks, with synthetic ELF headers (no Android SDK needed).
These tests validate deployment configuration, not actual OpenSSL/TLS execution.
Run: python app/tests/test_android_openssl.py
Set CMAKE_COMMAND if cmake is not on PATH.
"""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

MODULE = Path(__file__).resolve().parents[1] / "cmake/AndroidOpenSSL.cmake"
CMAKE = os.environ.get("CMAKE_COMMAND") or shutil.which("cmake")


class AndroidOpenSSL(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.libs = self.root / "openssl"
        self.source = self.root / "source"
        self.source.mkdir()

    def library(self, abi, name, debug=False, machine=None):
        folder = self.libs / ("no-asm/ssl_3" if debug else "ssl_3") / abi
        folder.mkdir(parents=True, exist_ok=True)
        header = bytearray(20)
        header[:6] = b"\x7fELF\x02\x01"
        machine = machine or {"arm64-v8a": 183, "x86_64": 62}[abi]
        header[18:20] = machine.to_bytes(2, "little")
        path = folder / ("lib" + name + "_3.so")
        path.write_bytes(header)
        return path

    def configure(self, abis="arm64-v8a", android=True, extra="", build_type="Release",
                  automatic=False, environment=None):
        text = f"""cmake_minimum_required(VERSION 3.25)
project(Check NONE)
set(ANDROID {'TRUE' if android else 'FALSE'})
set(QT_ANDROID_ABIS "{abis}")
add_library(app INTERFACE)
{extra}
include("{MODULE.as_posix()}")
lem_deploy_android_openssl(app)
get_target_property(libs app QT_ANDROID_EXTRA_LIBS)
file(WRITE "${{CMAKE_BINARY_DIR}}/libs.txt" "${{libs}}")
"""
        (self.source / "CMakeLists.txt").write_text(text)
        command = [
                CMAKE,
                "-S",
                str(self.source),
                "-B",
                str(self.root / "build"),
                "-DCMAKE_BUILD_TYPE=" + build_type,
            ]
        if not automatic:
            command.append("-DLEM_ANDROID_OPENSSL_ROOT=" + str(self.libs))
        env = dict(os.environ)
        for name in ("LEM_ANDROID_OPENSSL_ROOT", "ANDROID_SDK_ROOT", "ANDROID_HOME", "LOCALAPPDATA"):
            env.pop(name, None)
        env.update(environment or {})
        result = subprocess.run(
            command,
            capture_output=True,
            text=True,
            env=env,
        )
        return result.returncode, result.stdout + result.stderr

    def test_arm64_deployed(self):
        paths = [self.library("arm64-v8a", name) for name in ("crypto", "ssl")]
        code, output = self.configure()
        self.assertEqual(code, 0, output)
        deployed = (self.root / "build/libs.txt").read_text().split(";")
        self.assertEqual(deployed, [p.as_posix() for p in paths])

    def test_missing_library_blocks_configuration(self):
        self.library("arm64-v8a", "crypto")
        code, output = self.configure()
        self.assertNotEqual(code, 0)
        self.assertIn("Missing Android OpenSSL library", output)

    def test_wrong_abi_rejected(self):
        self.library("arm64-v8a", "crypto", machine=62)
        self.library("arm64-v8a", "ssl")
        code, output = self.configure()
        self.assertNotEqual(code, 0)
        self.assertIn("not a little-endian ELF library", " ".join(output.split()))

    def test_all_abis_deployed(self):
        for abi in ("arm64-v8a", "x86_64"):
            for name in ("crypto", "ssl"):
                self.library(abi, name)
        code, output = self.configure("arm64-v8a;x86_64")
        self.assertEqual(code, 0, output)
        self.assertEqual(len((self.root / "build/libs.txt").read_text().split(";")), 4)

    def test_conflicting_old_library_rejected(self):
        for name in ("crypto", "ssl"):
            self.library("arm64-v8a", name)
        code, output = self.configure(
            extra="set_property(TARGET app PROPERTY QT_ANDROID_EXTRA_LIBS /old/libcrypto.so)"
        )
        self.assertNotEqual(code, 0)
        self.assertIn("Conflicting OpenSSL library", output)

    def test_debug_prefers_complete_no_asm_pair(self):
        for name in ("crypto", "ssl"):
            self.library("arm64-v8a", name)
            self.library("arm64-v8a", name, debug=True)
        code, output = self.configure(build_type="Debug")
        self.assertEqual(code, 0, output)
        self.assertIn("/no-asm/ssl_3/", (self.root / "build/libs.txt").read_text())

    def test_desktop_needs_no_android_openssl(self):
        code, output = self.configure(android=False)
        self.assertEqual(code, 0, output)

    def test_qt_creator_nested_windows_sdk_autodetected(self):
        local = self.root / "Local AppData"
        self.libs = local / "Android/Sdk/android_openssl/openssl-android-arm64-v8a"
        paths = [self.library("arm64-v8a", name) for name in ("crypto", "ssl")]
        code, output = self.configure(automatic=True, environment={"LOCALAPPDATA": str(local)})
        self.assertEqual(code, 0, output)
        self.assertEqual((self.root / "build/libs.txt").read_text().split(";"),
                         [p.as_posix() for p in paths])

    def test_cmake_sdk_root_autodetects_flat_repository(self):
        sdk = self.root / "custom SDK"
        self.libs = sdk / "android_openssl"
        for name in ("crypto", "ssl"):
            self.library("arm64-v8a", name)
        code, output = self.configure(automatic=True,
                                     extra=f'set(ANDROID_SDK_ROOT "{sdk.as_posix()}")')
        self.assertEqual(code, 0, output)

    def test_split_abi_archives_and_nested_debug_pair(self):
        sdk = self.root / "SDK"
        for abi in ("arm64-v8a", "x86_64"):
            self.libs = sdk / "android_openssl" / ("openssl-android-" + abi)
            for name in ("crypto", "ssl"):
                self.library(abi, name)
                self.library(abi, name, debug=True)
        code, output = self.configure("arm64-v8a;x86_64", automatic=True, build_type="Debug",
                                     environment={"ANDROID_SDK_ROOT": str(sdk)})
        self.assertEqual(code, 0, output)
        deployed = (self.root / "build/libs.txt").read_text().split(";")
        self.assertEqual(len(deployed), 4)
        self.assertTrue(all("/no-asm/ssl_3/" in p for p in deployed))

    def test_persistent_environment_override(self):
        for name in ("crypto", "ssl"):
            self.library("arm64-v8a", name)
        code, output = self.configure(automatic=True,
                                     environment={"LEM_ANDROID_OPENSSL_ROOT": str(self.libs)})
        self.assertEqual(code, 0, output)

    def test_invalid_explicit_override_does_not_fall_back(self):
        sdk = self.root / "SDK"
        for name in ("crypto", "ssl"):
            self.library("arm64-v8a", name)
        valid = self.libs
        self.libs = self.root / "invalid"
        code, output = self.configure(environment={"LEM_ANDROID_OPENSSL_ROOT": str(valid),
                                                  "ANDROID_SDK_ROOT": str(sdk)})
        self.assertNotEqual(code, 0)
        self.assertIn("Missing Android OpenSSL library", output)

    def test_environment_sdk_autodetection_still_rejects_wrong_abi(self):
        sdk = self.root / "SDK"
        self.libs = sdk / "android_openssl/openssl-android-arm64-v8a"
        for name in ("crypto", "ssl"):
            self.library("arm64-v8a", name, machine=62)
        code, output = self.configure(automatic=True, environment={"ANDROID_HOME": str(sdk)})
        self.assertNotEqual(code, 0)
        self.assertIn("not a little-endian ELF library", " ".join(output.split()))


if __name__ == "__main__":
    if not CMAKE:
        raise SystemExit("cmake not found; add it to PATH or set CMAKE_COMMAND.")
    unittest.main()
