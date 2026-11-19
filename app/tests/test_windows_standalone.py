# SPDX-License-Identifier: GPL-3.0-only
# Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

"""PE normal/delay import audit and CMake standalone guards (no Windows needed)."""
import importlib.util
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

APP = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("verify_pe", APP / "windows/verify-pe.py")
pe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pe)


def fixture(normal=("kernel32.dll",), delayed=()):
    data = bytearray(4608)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HH", data, 0x84, 0x8664, 1)
    struct.pack_into("<H", data, 0x94, 240)
    opt = 0x98
    struct.pack_into("<H", data, opt, 0x20B)
    struct.pack_into("<Q", data, opt+24, 0x140000000)
    struct.pack_into("<I", data, opt+108, 16)
    section = opt+240
    struct.pack_into("<IIII", data, section+8, 4096, 4096, 4096, 512)
    names = 512+1024
    for index, entries, pos, width in [(1, normal, 512, 20), (13, delayed, 768, 32)]:
        if not entries:
            continue
        struct.pack_into("<II", data, opt+112+index*8, pos-512+4096, (len(entries)+1)*width)
        for n in entries:
            name_rva = names-512+4096
            if index == 1:
                struct.pack_into("<IIIII", data, pos, 1, 0, 0, name_rva, 1)
            else:
                struct.pack_into("<IIIIIIII", data, pos, 1, name_rva, 0, 1, 1, 0, 0, 0)
            encoded = n.encode("ascii")+b"\0"
            data[names:names+len(encoded)] = encoded
            names += len(encoded)
            pos += width
    return bytes(data)


class PortableAudit(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.path = Path(self.temp.name)/"Lem.exe"

    def tearDown(self):
        self.temp.cleanup()

    def test_system_normal_and_delayed(self):
        self.path.write_bytes(fixture(("KERNEL32.dll", "ucrtbase.dll", "api-ms-win-core-file-l1-1-0.dll"), ("crypt32.dll", "winspool.drv")))
        result = pe.verify(self.path)
        self.assertIn("crypt32.dll", result["delay_imports"])

    def test_windows_authorization_and_color_management(self):
        # Reported by the real MinGW 15.1 static Qt build. Both are Windows APIs,
        # unlike Qt/OpenSSL/compiler runtime redistributables.
        for normal, delayed in [("AUTHZ.DLL", "MSCMS.DLL"), ("MSCMS.DLL", "AUTHZ.DLL")]:
            self.path.write_bytes(fixture(("kernel32.dll", normal), (delayed,)))
            result = pe.verify(self.path)
            self.assertIn(normal.lower(), result["imports"])
            self.assertIn(delayed.lower(), result["delay_imports"])

    def test_external_normal_or_delay_is_rejected(self):
        for dll in ["Qt6Core.dll", "libssl-3-x64.dll", "libgcc_s_seh-1.dll", "libstdc++-6.dll", "libwinpthread-1.dll", "vcruntime140.dll", "msvcp140.dll", "unknown.dll"]:
            for delayed in [False, True]:
                with self.subTest(dll=dll, delayed=delayed):
                    self.path.write_bytes(fixture(("kernel32.dll",) if delayed else (dll,), (dll,) if delayed else ()))
                    with self.assertRaisesRegex(ValueError, "External DLL"):
                        pe.verify(self.path)

    def test_malformed_or_empty_is_rejected(self):
        for data in [b"", b"MZ", fixture()[:512], fixture(normal=()), fixture(("folder/Qt6Core.dll",))]:
            self.path.write_bytes(data)
            with self.assertRaises((ValueError, struct.error)):
                pe.verify(self.path)

    def test_cli_report_and_failure_code(self):
        self.path.write_bytes(fixture())
        report = self.path.with_suffix(".json")
        import sys
        result = subprocess.run([sys.executable, str(APP/"windows/verify-pe.py"), str(self.path), "--report", str(report)], capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('kernel32.dll', report.read_text())
        self.path.write_bytes(fixture(("Qt6Gui.dll",)))
        result = subprocess.run([sys.executable, str(APP/"windows/verify-pe.py"), str(self.path)], capture_output=True)
        self.assertNotEqual(result.returncode, 0)


@unittest.skipUnless(shutil.which("cmake"), "CMake is required")
class CMakeGuard(unittest.TestCase):
    def configure(self, kind, windows=True, msvc=False, missing_plugin=None, enabled=True):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            module = (APP/"cmake/WindowsStandalone.cmake").as_posix()
            plugins = ["QWindowsIntegrationPlugin", "QOffscreenIntegrationPlugin", "QJpegPlugin", "QSchannelBackendPlugin", "QSvgPlugin"]
            targets = "\n".join(f"add_library(Qt6::{p} STATIC IMPORTED)" for p in plugins if p != missing_plugin)
            (root/"dummy.cpp").write_text("int main(){return 0;}")
            (root/"CMakeLists.txt").write_text(f'''cmake_minimum_required(VERSION 3.25)
project(Guard LANGUAGES CXX)
set(WIN32 {"TRUE" if windows else "FALSE"})
set(MSVC {"TRUE" if msvc else "FALSE"})
set(MINGW {"FALSE" if msvc else "TRUE"})
set(LEM_WINDOWS_STANDALONE {"ON" if enabled else "OFF"})
add_library(Qt6::Core {kind} IMPORTED)
{targets}
add_executable(Lem dummy.cpp)
function(qt_import_plugins)
endfunction()
function(qt_target_qml_sources)
endfunction()
include("{module}")
lem_configure_windows_standalone(Lem)
get_target_property(opts Lem LINK_OPTIONS)
get_target_property(crt Lem MSVC_RUNTIME_LIBRARY)
file(WRITE "${{CMAKE_BINARY_DIR}}/properties.txt" "${{opts}};${{crt}}")
''')
            result = subprocess.run(["cmake", "-S", str(root), "-B", str(root/"build"), f"-DPython3_EXECUTABLE={sys.executable}"], capture_output=True, text=True)
            props = root/"build/properties.txt"
            return result.returncode, result.stdout+result.stderr, props.read_text() if props.exists() else ""

    def test_dynamic_qt_rejected(self):
        rc, output, _ = self.configure("SHARED")
        self.assertNotEqual(rc, 0)
        self.assertIn("requires STATIC Qt", output)

    def test_missing_required_plugins_and_other_platform_rejected(self):
        for plugin in ("QSchannelBackendPlugin", "QSvgPlugin"):
            with self.subTest(plugin=plugin):
                rc, output, _ = self.configure("STATIC", missing_plugin=plugin)
                self.assertNotEqual(rc, 0)
                self.assertIn(plugin, output)
        rc, output, _ = self.configure("STATIC", windows=False)
        self.assertNotEqual(rc, 0)
        self.assertIn("only supported on Windows", output)

    def test_static_flags_and_opt_out(self):
        rc, output, props = self.configure("STATIC")
        self.assertEqual(rc, 0, output)
        self.assertIn("-static;-static-libgcc;-static-libstdc++", props)
        rc, output, props = self.configure("STATIC", msvc=True)
        self.assertEqual(rc, 0, output)
        self.assertIn("MultiThreaded", props)
        rc, output, props = self.configure("SHARED", windows=False, enabled=False)
        self.assertEqual(rc, 0, output)
        self.assertNotIn("-static", props)


if __name__ == "__main__":
    unittest.main()
