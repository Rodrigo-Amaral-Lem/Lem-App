# SPDX-License-Identifier: GPL-3.0-only
"""Build/deploy safety and bootstrap regression checks; no SDK/network required.

Run: python -m unittest discover -s tools/tests -v
"""
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import zipfile

spec = importlib.util.spec_from_file_location("lem_android", Path(__file__).resolve().parents[1] / "android.py")
android = importlib.util.module_from_spec(spec)
spec.loader.exec_module(android)


class AndroidTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="lem android test ")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        args = android.parser().parse_args(["build", "--qt-root", str(self.root / "Qt"),
                                            "--sdk", str(self.root / "Sdk"),
                                            "--build-dir", str(self.root / "build"),
                                            "--cache-dir", str(self.root / "cache")])
        self.task = android.Android(args)

    def file(self, name, contents=""):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(contents, encoding="utf-8")
        return path

    def receipt(self):
        apk = self.file("build/android-build-Lem/build/outputs/apk/debug/Lem-debug.apk", "APK fixture")
        android.write_json(self.task.receipt, {"apk": str(apk), "sha256": android.digest(apk), "abi": "arm64-v8a"})
        return apk

    def test_device_unique(self):
        self.assertEqual(android.select_device("List of devices attached\nphone123 device usb:1-1 product:test\n", None), "phone123")

    def test_device_selection_never_ignores_unready_devices(self):
        for devices in ("", "a unauthorized", "a offline", "a device\nb device", "a device\nb unauthorized",
                        "a no permissions (udev rules)", "a device\nb no permissions (udev rules)"):
            with self.subTest(devices=devices), self.assertRaises(android.ToolError):
                android.select_device(devices, None)

    def test_explicit_serial(self):
        self.assertEqual(android.select_device("a device\nb device", "b"), "b")
        with self.assertRaises(android.ToolError):
            android.select_device("a unauthorized\nb device", "a")

    def test_deploy_replaces_without_wiping_and_launches_only_when_requested(self):
        apk = self.receipt()
        self.task.adb = self.root / "adb"
        calls = []
        def run(args, **kwargs):
            calls.append([str(x) for x in args])
            return subprocess.CompletedProcess(args, 0, "List of devices attached\nphone device\n")
        with patch.object(self.task, "android_sdk") as sdk, patch.object(self.task, "run", side_effect=run):
            self.task.deploy()
            sdk.assert_called_once_with(deploy_only=True)
        self.assertEqual(calls[-1], [str(self.task.adb), "-s", "phone", "install", "-r", str(apk)])
        self.assertEqual(len(calls), 2)

    def test_install_failure_never_launches_or_uninstalls(self):
        self.receipt()
        self.task.adb = self.root / "adb"
        self.task.args.launch = True
        result = subprocess.CompletedProcess([], 0, "phone device")
        with patch.object(self.task, "android_sdk"), patch.object(self.task, "run", side_effect=[result, android.ToolError("signing mismatch")]) as run:
            with self.assertRaises(android.ToolError):
                self.task.deploy()
            self.assertEqual(run.call_count, 2)

    def test_deploy_rejects_modified_apk_before_sdk_or_device_access(self):
        self.receipt().write_text("changed")
        with patch.object(self.task, "android_sdk") as sdk, self.assertRaises(android.ToolError):
            self.task.deploy()
        sdk.assert_not_called()

    def test_deploy_requires_successful_build(self):
        with self.assertRaisesRegex(android.ToolError, "No successful build"):
            self.task.deploy()

    def test_deploy_checks_selected_abi(self):
        self.receipt()
        self.task.args.abi = "x86_64"
        with self.assertRaisesRegex(android.ToolError, "matching --abi"):
            self.task.deploy()

    def test_failed_build_invalidates_previous_receipt(self):
        self.receipt()
        with patch.object(self.task, "cmake_tools", side_effect=android.ToolError("missing compiler")):
            with self.assertRaises(android.ToolError):
                self.task.build_apk()
        self.assertFalse(self.task.receipt.exists())

    def test_unmanaged_build_is_not_reconfigured(self):
        self.file("build/CMakeCache.txt")
        with self.assertRaisesRegex(android.ToolError, "unmanaged"):
            self.task.guard_build(self.task.build, {"abi": "arm64-v8a"})

    def test_changed_toolchain_requires_fresh_build(self):
        self.task.guard_build(self.task.build, {"abi": "arm64-v8a"})
        self.task.guard_build(self.task.build, {"abi": "arm64-v8a"})
        with self.assertRaisesRegex(android.ToolError, "configuration changed"):
            self.task.guard_build(self.task.build, {"abi": "x86_64"})

    def test_batch_paths_are_quoted_and_expansions_rejected(self):
        args = android.batch_command(["C:/SDK & tools/bin/sdkmanager.bat", "--sdk_root=C:/SDK & tools", "ndk;27.2.12479018"])
        self.assertTrue(args.endswith('""C:/SDK & tools/bin/sdkmanager.bat" "--sdk_root=C:/SDK & tools" "ndk;27.2.12479018""'))
        for path in ("C:/SDK%PATH%", "a!b", 'a"b', "a\nb"):
            with self.subTest(path=path), self.assertRaises(android.ToolError):
                android.batch_command([path])

    @unittest.skipUnless(os.name == "nt", "Windows cmd quoting")
    def test_real_batch_execution_with_spaces_and_metacharacters(self):
        script = self.file('batch & spaces/tool.cmd', '@echo off\necho "%~1"\necho "%~2"\nexit /b 7\n')
        result = subprocess.run(android.batch_command([script, 'path & spaces', 'ndk;27.2']), capture_output=True, text=True)
        self.assertEqual(result.returncode, 7, result.stderr)
        self.assertIn('"path & spaces"', result.stdout)
        self.assertIn('"ndk;27.2"', result.stdout)

    def test_no_bootstrap_blocks_installs(self):
        self.task.args.no_bootstrap = True
        with patch.object(self.task, "java") as java, self.assertRaisesRegex(android.ToolError, "Missing Android"):
            self.task.android_sdk()
        java.assert_not_called()

    def test_sdk_installs_exact_missing_packages(self):
        self.task.args.accept_licenses = True
        manager = self.file("sdkmanager")
        calls = []
        def run(args, **kwargs):
            calls.append(([str(x) for x in args], kwargs))
            if "platform-tools" in args:
                self.file(f"Sdk/platform-tools/adb{self.task.exe}")
                self.file(f"Sdk/platforms/{android.SDK_PLATFORM}/android.jar")
                self.file(f"Sdk/build-tools/{android.BUILD_TOOLS}/aapt2{self.task.exe}")
                self.file(f"Sdk/ndk/{android.NDK_VERSION}/build/cmake/android.toolchain.cmake")
        with patch.object(self.task, "java"), patch.object(self.task, "sdk_manager", return_value=manager), patch.object(self.task, "run", side_effect=run):
            self.task.android_sdk()
        self.assertIn(f"ndk;{android.NDK_VERSION}", calls[-1][0])
        self.assertIn(f"platforms;{android.SDK_PLATFORM}", calls[-1][0])
        self.assertEqual(calls[0][1]["input_text"], "y\n" * 100)

    def test_deploy_sdk_does_not_install_java_ndk_or_qt_when_adb_exists(self):
        self.file(f"Sdk/platform-tools/adb{self.task.exe}")
        with patch.object(self.task, "java") as java, patch.object(self.task, "sdk_manager") as manager:
            self.task.android_sdk(deploy_only=True)
        java.assert_not_called()
        manager.assert_not_called()

    def test_sdk_failure_is_not_ignored(self):
        with patch.object(self.task, "java"), patch.object(self.task, "sdk_manager", return_value="sdkmanager"), patch.object(self.task, "run"):
            with self.assertRaisesRegex(android.ToolError, "incomplete"):
                self.task.android_sdk()

    def test_download_checksum_failure_removes_partial_file(self):
        destination = self.root / "file.zip"
        with patch.object(android.urllib.request, "urlopen", return_value=io.BytesIO(b"incorrect")):
            with self.assertRaisesRegex(android.ToolError, "Checksum mismatch"):
                android.download("https://example.invalid/file", destination, "0" * 64)
        self.assertFalse(destination.exists())
        self.assertFalse(destination.with_suffix(".zip.part").exists())

    def test_valid_cached_download_does_not_access_network(self):
        destination = self.file("file.zip", "fixture")
        with patch.object(android.urllib.request, "urlopen") as request:
            self.assertEqual(android.download("unused", destination, android.digest(destination)), destination)
        request.assert_not_called()

    def test_archive_cannot_escape_destination(self):
        archive = self.root / "archive.zip"
        with zipfile.ZipFile(archive, "w") as z:
            z.writestr("../escaped", "fixture")
        with self.assertRaisesRegex(android.ToolError, "Unsafe archive"):
            android.extract_zip(archive, self.root / "extract")
        self.assertFalse((self.root / "escaped").exists())

    def test_qt_version_mismatch_is_reported_before_build(self):
        self.file("Qt/6.12.0/" + ("mingw_64" if self.task.windows else "gcc_64") + "/mkspecs/qconfig.pri", "QT_VERSION = 6.11.3\n")
        with self.assertRaisesRegex(android.ToolError, "must match"):
            self.task.qt()

    def test_existing_openssl_is_never_overwritten(self):
        self.file("Sdk/android_openssl/my-file", "user content")
        with self.assertRaisesRegex(android.ToolError, "will not be overwritten"):
            self.task.openssl()
        self.assertEqual((self.root / "Sdk/android_openssl/my-file").read_text(), "user content")

    def test_lock_is_released_after_exception(self):
        with self.assertRaises(RuntimeError):
            with android.lock_directory(self.root / "lock"):
                raise RuntimeError("interrupted")
        with android.lock_directory(self.root / "lock"):
            pass

    def test_source_bootstrap_builds_host_before_android_with_openssl_headers(self):
        # Exercise the actual bootstrap orchestration, substituting only external
        # tools and network. Assert host tools are visible when cross-configuring.
        self.task.windows = False
        self.task.cmake, self.task.ninja = Path("cmake"), Path("ninja")
        self.task.openssl_root = self.root / "ssl"
        self.file("ssl/ssl_3/include/openssl/ssl.h")
        self.task.args.accept_licenses = True
        self.task.cache.mkdir(parents=True)
        commands = []
        def run(args, **kwargs):
            args = [str(x) for x in args]
            commands.append(args)
            if "-android-abis" in args:
                self.assertIn(str(self.task.host / "bin"), self.task.env["PATH"])
            return subprocess.CompletedProcess(args, 0, "")
        with patch.object(self.task, "git", return_value="git"), patch.object(android.shutil, "which", return_value="tool"), patch.object(android, "download", return_value=self.root / "patch.diff"), patch.object(self.task, "run", side_effect=run):
            self.task.build_qt([self.task.host, self.task.target])
        configured = [c for c in commands if "-opensource" in c]
        self.assertEqual(len(configured), 2)
        self.assertNotIn("-android-abis", configured[0])
        self.assertIn("-android-abis", configured[1])
        self.assertIn(f"-DOPENSSL_INCLUDE_DIR={self.task.openssl_root / 'ssl_3'}/include", configured[1])
        self.assertEqual(android.read_json(self.task.target / ".lem-source-install.json")["status"], "complete")

    def test_windows_qt_configure_propagates_cmake_failure(self):
        self.task.windows = True
        self.task.cmake = Path("cmake.exe")
        options = [self.root / "source/configure.bat", "-prefix", self.root / "Qt with spaces", "-opensource"]
        with patch.object(self.task, "run", side_effect=android.ToolError("configure failed")) as run:
            with self.assertRaisesRegex(android.ToolError, "configure failed"):
                self.task.configure_qt(options, self.root)
        self.assertEqual(run.call_args.args[0][0], self.task.cmake)
        self.assertIn(str(self.root / "Qt with spaces") + "\n", (self.root / "lem-configure.opt").read_text())

    def test_ubuntu_installed_packages_do_not_require_sudo_or_update(self):
        installed = subprocess.CompletedProcess([], 0, "cmake\tinstalled\nlibssl-dev:amd64\tinstalled\n")
        with patch.object(android.Path, "is_file", return_value=True), patch.object(android.Path, "read_text", return_value="ID=ubuntu\n"), patch.object(self.task, "run", return_value=installed) as run:
            self.task.apt(["cmake", "libssl-dev"])
        self.assertEqual(run.call_count, 1)

    def test_ubuntu_installs_only_missing_packages(self):
        installed = subprocess.CompletedProcess([], 1, "cmake\tinstalled\ndpkg-query: no packages found matching ninja-build\n")
        with patch.object(android.Path, "is_file", return_value=True), patch.object(android.Path, "read_text", return_value="ID=ubuntu\n"), patch.object(android.os, "geteuid", return_value=1000, create=True), patch.object(self.task, "run", return_value=installed) as run:
            self.task.apt(["cmake", "ninja-build"])
        self.assertEqual(run.call_args.args[0], ["sudo", "apt-get", "install", "-y", "ninja-build"])

    def test_wsl_build_path_excludes_windows_mounts_without_changing_parent(self):
        env = {"PATH": "/usr/bin:/mnt/c/Windows/System32:/home/rod/bin:/mnt/d/Tools", "WSL_DISTRO_NAME": "Ubuntu"}
        result = android.build_environment(env, windows=False)
        self.assertEqual(result["PATH"], "/usr/bin:/home/rod/bin")
        self.assertIn("/mnt/c/Windows/System32", env["PATH"])

    def test_wsl_custom_paths_can_be_retained(self):
        env = {"PATH": "/usr/bin:/mnt/c/custom-linux-tools", "WSL_DISTRO_NAME": "Ubuntu"}
        self.assertEqual(android.build_environment(env, windows=False, inherit_windows_path=True), env)

    def test_native_windows_and_linux_paths_are_unchanged(self):
        env = {"PATH": "/usr/bin:/mnt/c/custom-linux-tools"}
        self.assertEqual(android.build_environment(env, windows=False, kernel="6.8.0-generic"), env)
        windows_env = {"PATH": "C:\\Qt\\bin;C:\\Windows\\System32", "WSL_DISTRO_NAME": "Ubuntu"}
        self.assertEqual(android.build_environment(windows_env, windows=True), windows_env)
        self.assertEqual(android.build_environment(env, windows=False, kernel="6.6.87.2-microsoft-standard-WSL2")["PATH"], "/usr/bin")


if __name__ == "__main__":
    unittest.main()
