# SPDX-License-Identifier: GPL-3.0-only
"""Static build discovery, installed-SDK protection and resumable cache tests."""
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

SCRIPT = Path(__file__).resolve().parents[1] / 'windows/build_standalone.py'
spec = importlib.util.spec_from_file_location('windows_builder', SCRIPT)
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


class InstalledBuild(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=os.environ.get('LEM_TEST_TEMP'))
        self.root = Path(self.temp.name)
        self.sdk = self.root / 'Qt'
        self.output = self.root / 'app/build/windows-static'
        self.source = self.sdk / '6.12.0/Src'
        self.shared = self.sdk / '6.12.0/mingw_64'
        self.kit(self.shared, static=False)
        for name in ('Tools/CMake_64/bin/cmake.exe', 'Tools/Ninja/ninja.exe', 'Tools/mingw1510_64/bin/g++.exe',
                     'Tools/mingw1510_64/bin/gcc.exe', 'Tools/mingw1510_64/bin/windres.exe'):
            self.file(self.sdk / name)
        for name in ('qtbase', 'qtshadertools', 'qtdeclarative'):
            self.file(self.source / name / 'CMakeLists.txt')
        self.file(self.source / 'configure.bat')
        self.file(self.source / 'qtbase/.cmake.conf', 'set(QT_REPO_MODULE_VERSION "6.12.0")')
        self.file(self.root / 'Programs/Git/usr/bin/perl.exe')
        self.env = {'PATH': '', 'ProgramFiles': str(self.root / 'Programs')}
        self.capture = patch.object(builder.Builder, 'capture', side_effect=self.probe)
        self.capture.start()

    def tearDown(self):
        self.capture.stop()
        self.temp.cleanup()

    def file(self, path, content=''):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)

    def kit(self, path, static=True, gcc='15.1.0', arch='x86_64'):
        self.file(path / 'mkspecs/qconfig.pri', f'QT_VERSION = 6.12.0\nQT_ARCH = {arch}\nQT_CONFIG += {"static" if static else "shared"} release\n' +
                  ''.join(f'QT_GCC_{part}_VERSION = {number}\n' for part, number in zip(('MAJOR', 'MINOR', 'PATCH'), gcc.split('.'))))
        for module in builder.MODULES:
            self.file(path / f'lib/cmake/Qt6{module}/Qt6{module}Config.cmake')

    def probe(self, args):
        if '--version' in args:
            return 'cmake version 3.30.5'
        if '-dumpmachine' in args:
            return 'x86_64-w64-mingw32'
        return '15.1.0'

    def make(self, *args):
        parsed = builder.parser().parse_args(['--qt-root', str(self.sdk), '--build-directory', str(self.output), *args])
        return builder.Builder(parsed, self.env)

    def test_default_build_path_is_under_repository_build(self):
        parsed = builder.parser().parse_args(['--qt-root', str(self.sdk)])
        build = builder.Builder(parsed, self.env)
        self.assertEqual(build.build, SCRIPT.parents[2] / 'build/windows-static')

    def test_configure_preserves_shortened_ninja_paths_during_build(self):
        def check_first_configure(expected_executable):
            build = self.make().discover()
            with patch.object(build, 'say'), patch.object(
                build, 'run', side_effect=builder.BuildError('configuration captured')
            ) as run, self.assertRaisesRegex(builder.BuildError, 'configuration captured'):
                build.compile()
            command = run.call_args.args[0]
            self.assertEqual(Path(command[0]).name, expected_executable)
            self.assertIn('-DCMAKE_SUPPRESS_REGENERATION=ON', command)

        check_first_configure('configure.bat')
        self.file(self.output / 'qt-build/build.ninja')
        check_first_configure('cmake.exe')
        (self.output / 'qt-build/lem-static-install-pending').unlink()
        self.kit(self.output / 'qt')
        check_first_configure('cmake.exe')

    @unittest.skipUnless(os.name == 'nt', 'Windows build drive')
    def test_build_drive_is_saved_and_released_after_interruption(self):
        self.output.mkdir(parents=True)
        drive = Path('Z:/')
        mounted = False
        original_exists = Path.exists

        def exists(path):
            return mounted if path == drive else original_exists(path)

        def subst(command, **kwargs):
            nonlocal mounted
            self.assertEqual(command[1], 'Z:')
            mounted = command[2] != '/D'
            if mounted:
                self.assertEqual(Path(command[2]), self.output.parent)
            return subprocess.CompletedProcess(command, 0)

        with patch.object(builder, 'SHORT_BUILD_PATH_THRESHOLD', 0), patch.object(
            Path, 'exists', exists
        ), patch.object(Path, 'samefile', lambda path, target: path == drive and target == self.output.parent), patch.object(
            builder.subprocess, 'run', side_effect=subst
        ) as run:
            with self.assertRaisesRegex(RuntimeError, 'interrupted'):
                with builder.short_build_directory(self.output, self.output / 'qt-build') as path:
                    self.assertEqual(path, drive / self.output.name)
                    raise RuntimeError('interrupted')
            self.assertFalse(mounted)
            self.assertEqual((self.output / 'lem-build-drive.txt').read_text().strip(), 'Z:')
            with builder.short_build_directory(self.output, self.output / 'qt-build') as path:
                self.assertEqual(path, drive / self.output.name)
            self.assertFalse(mounted)
        self.assertEqual(run.call_count, 4)

    def test_short_build_paths_preserve_the_reported_output(self):
        build = self.make().discover()
        short_path = self.root / 'short'
        build.use_build_directory(short_path)
        self.assertEqual(build.qt_build, short_path / 'qt-build')
        self.assertEqual(build.prefix, short_path / 'qt')
        self.assertEqual(build.plan()['executable'], str(self.output / 'portable/Lem.exe'))

    def test_first_build_uses_installed_source_and_outputs_only_under_app(self):
        build = self.make().discover()
        self.assertTrue(build.build_qt)
        self.assertEqual(build.source, self.source)
        self.assertEqual(build.prefix, self.output / 'qt')
        self.assertTrue(builder.inside(build.qt_build, self.output))
        self.assertIn('mingw1510_64', str(build.compiler))
        self.assertFalse(self.output.exists(), 'Discovery/Plan must not write build files')

    def test_versioned_and_flat_kits_resolve_same_installer_root(self):
        self.assertEqual(builder.installed_root(self.shared), self.sdk)
        self.assertEqual(builder.installed_root(self.sdk / '6.12.0-static'), self.sdk)

    def test_long_dependency_paths_are_unique_short_and_idempotent(self):
        first = '  DEP_FILE = nested/' + 'x' * 180 + '.obj.d'
        second = first.replace('.obj.d', '-second.obj.d')
        original = 'rule compiler\n  depfile = $DEP_FILE\n  command = gcc -MF $DEP_FILE\n' + first + '\n' + second + '\n  DEP_FILE = short.obj.d\n'
        manifest = self.output / 'build.ninja'
        self.file(manifest, original)
        self.assertEqual(builder.shorten_ninja_paths(self.output), 2)
        text = manifest.read_text()
        paths = [line.split(' = ', 1)[1] for line in text.splitlines() if line.startswith('  DEP_FILE = ')]
        self.assertNotEqual(paths[0], paths[1])
        self.assertTrue(all(len(str(self.output / path)) < 240 for path in paths))
        self.assertIn('  DEP_FILE = short.obj.d', text)
        self.assertIn('command = gcc -MF $DEP_FILE', text)
        self.assertEqual(builder.shorten_ninja_paths(self.output), 0)
        self.assertEqual(manifest.read_text(), text)

    @unittest.skipUnless(os.name == 'nt' and shutil.which('g++.exe') and shutil.which('ninja.exe'), 'Windows MinGW/Ninja required')
    def test_real_long_depfile_build_and_header_rebuild(self):
        directory = self.root / 'long-depfile-build'
        directory.mkdir()
        relative = Path('nested')
        while len(str(directory / relative)) < 200:
            relative = relative / ('nested-' + 'x' * 35)
        object_name = 'probe-' + 'x' * 80 + '.obj'
        dependency_name = object_name + '.d'
        object_path = directory / relative / object_name
        extended_object = Path('\\\\?\\' + str(object_path))
        extended_object.parent.mkdir(parents=True, exist_ok=True)
        self.file(directory / 'probe.cpp', '#include "settings.h"\nint probe() { return VALUE; }\n')
        self.file(directory / 'settings.h', '#define VALUE 1\n')
        self.file(directory / 'build.ninja',
                  f'rule CXX_COMPILER_test\n  command = "{shutil.which("g++.exe")}" -MD -MF $DEP_FILE -o $out -c $in\n  depfile = $DEP_FILE\n  deps = gcc\n' +
                  f'rule archive\n  command = "{Path(shutil.which("g++.exe")).with_name("ar.exe")}" rcs $out $in\n' +
                  f'build {(relative / object_name).as_posix()}: CXX_COMPILER_test probe.cpp\n  DEP_FILE = {relative / dependency_name}\n' +
                  f'build probe.a: archive {(relative / object_name).as_posix()}\n')
        self.assertEqual(builder.shorten_ninja_paths(directory), 1)
        short_object = directory / next(iter(json.loads((directory / 'lem-short-objects.json').read_text()).values()))

        def compile():
            result = subprocess.run([shutil.which('ninja.exe')], cwd=directory, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            return result.stdout

        try:
            compile()
            previous = short_object.read_bytes()
            self.assertTrue((directory / 'probe.a').is_file())
            self.assertIn('no work to do', compile())
            self.file(directory / 'settings.h', '#define VALUE 2\n')
            self.assertNotIn('no work to do', compile())
            self.assertNotEqual(short_object.read_bytes(), previous)
            builder.restore_install_objects(directory)
            self.assertEqual(extended_object.read_bytes(), short_object.read_bytes())
        finally:
            extended_object.unlink(missing_ok=True)

    @unittest.skipUnless(shutil.which('cmake'), 'CMake required')
    def test_cmake_shortens_plugin_autogen_before_qt_metadata_is_read(self):
        source = self.root / 'cmake-source'
        output = self.root / 'cmake-output'
        hook = SCRIPT.with_name('ShortAutogenPaths.cmake').as_posix()
        name = 'qtquickcontrols2fluentwinui3styleimplplugin'
        self.file(source / 'dummy.cpp', 'int dummy() { return 0; }\n')
        self.file(source / 'CMakeLists.txt', f'''cmake_minimum_required(VERSION 3.25)
project(ShortPaths LANGUAGES CXX)
include("{hook}")
add_library(Core STATIC dummy.cpp)
add_library({name} STATIC dummy.cpp)
add_library(alias ALIAS {name})
add_library(external STATIC IMPORTED)
get_target_property(short_dir {name} AUTOGEN_BUILD_DIR)
get_target_property(core_dir Core AUTOGEN_BUILD_DIR)
file(WRITE "${{CMAKE_BINARY_DIR}}/paths.txt" "${{short_dir}}\\n${{core_dir}}")
''')
        arguments = [shutil.which('cmake'), '-S', str(source), '-B', str(output), '-G', 'Ninja']
        if os.name == 'nt' and shutil.which('windres.exe'):
            arguments.append(builder.cmake_path('CMAKE_RC_COMPILER', Path(shutil.which('windres.exe'))))
        result = subprocess.run(arguments, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        lines = (output / 'paths.txt').read_text().splitlines()
        self.assertTrue(Path(lines[0]).is_relative_to(output / '.ag'))
        self.assertEqual(lines[1], 'core_dir-NOTFOUND')

    def test_equivalent_windows_path_separators_can_resume_cache(self):
        build = self.make()
        signature = {'arguments': ['-DCMAKE_RC_COMPILER=C:\\Qt\\bin\\windres.exe']}
        build.guard(self.output, 'marker.json', signature)
        self.file(self.output / 'CMakeCache.txt', 'incomplete configuration')
        build.guard(self.output, 'marker.json', {'arguments': ['-DCMAKE_RC_COMPILER=C:/Qt/bin/windres.exe']})

    def test_existing_static_binary_used_without_sources_or_perl(self):
        static = self.sdk / '6.12.0-static-mingw'
        self.kit(static)
        (self.source / 'configure.bat').unlink()
        (self.root / 'Programs/Git/usr/bin/perl.exe').unlink()
        build = self.make().discover()
        self.assertFalse(build.build_qt)
        self.assertEqual(build.prefix, static)
        self.assertIsNone(build.source)
        self.assertIsNone(build.perl)

    def test_local_static_install_wins_and_pending_install_resumes(self):
        self.kit(self.sdk / '6.12.0-static-mingw')
        self.kit(self.output / 'qt')
        build = self.make().discover()
        self.assertEqual(build.prefix, self.output / 'qt')
        self.assertFalse(build.build_qt)
        self.file(self.output / 'qt-build/lem-static-install-pending')
        self.assertTrue(self.make().discover().build_qt)

    def test_force_rebuild_uses_local_prefix_even_with_external_static_kit(self):
        self.kit(self.sdk / '6.12.0-static-mingw')
        build = self.make('--build-qt').discover()
        self.assertTrue(build.build_qt)
        self.assertEqual(build.prefix, self.output / 'qt')

    def test_reject_shared_explicit_prefix_and_wrong_compiler(self):
        with self.assertRaisesRegex(builder.BuildError, 'Shared installer'):
            self.make('--qt-prefix', str(self.shared)).discover()
        static = self.sdk / '6.12.0-static-mingw'
        self.kit(static, gcc='13.1.0')
        with self.assertRaisesRegex(builder.BuildError, '13.1.0'):
            self.make('--qt-prefix', str(static)).discover()

    def test_no_writes_to_installed_sdk_even_with_explicit_arguments(self):
        with self.assertRaisesRegex(builder.BuildError, 'outside the installed Qt'):
            self.make('--build-directory', str(self.sdk / 'build-Lem')).discover()
        with self.assertRaisesRegex(builder.BuildError, 'inside BuildDirectory'):
            self.make('--qt-build', str(self.sdk / 'build-qt')).discover()
        self.kit(self.sdk / 'static')
        with self.assertRaisesRegex(builder.BuildError, 'read-only'):
            self.make('--qt-prefix', str(self.sdk / 'static'), '--build-qt').discover()
        external = self.root / 'custom-static-kit'
        self.kit(external)
        with self.assertRaisesRegex(builder.BuildError, 'inside the installed static Qt'):
            self.make('--qt-prefix', str(external), '--build-directory', str(external / 'Lem')).discover()

    def test_missing_source_and_missing_ninja_explain_required_input(self):
        (self.source / 'configure.bat').unlink()
        with self.assertRaisesRegex(builder.BuildError, 'No installed Qt'):
            self.make().discover()
        self.file(self.source / 'configure.bat')
        (self.sdk / 'Tools/Ninja/ninja.exe').unlink()
        with self.assertRaisesRegex(builder.BuildError, 'ninja.exe'):
            self.make().discover()

    def test_source_and_foreign_app_build_rejected_before_compiling_qt(self):
        with self.assertRaisesRegex(builder.BuildError, 'app source'):
            self.make('--build-directory', str(builder.APP)).discover()
        self.file(self.output / 'CMakeCache.txt', 'shared kit cache')
        with self.assertRaisesRegex(builder.BuildError, 'Unowned app CMake cache'):
            self.make().discover()
        self.assertFalse((self.output / 'qt-build').exists())

    def test_matching_failed_configure_resumes_foreign_or_changed_cache_is_preserved(self):
        build = self.make()
        directory = self.output / 'qt-build'
        signature = {'compiler': 'matching', 'source': 'installed'}
        build.guard(directory, 'marker.json', signature)
        self.file(directory / 'CMakeCache.txt', 'incomplete configuration')
        build.guard(directory, 'marker.json', signature)
        with self.assertRaisesRegex(builder.BuildError, 'Incompatible'):
            build.guard(directory, 'marker.json', {'compiler': 'different'})
        self.assertEqual(json.loads((directory / 'marker.json').read_text()), signature)
        (directory / 'marker.json').unlink()
        with self.assertRaisesRegex(builder.BuildError, 'unowned'):
            build.guard(directory, 'marker.json', signature)
        self.assertEqual((directory / 'CMakeCache.txt').read_text(), 'incomplete configuration')

    def test_cache_cannot_mix_source_and_prefix(self):
        with self.assertRaisesRegex(builder.BuildError, 'separate subdirectories'):
            self.make('--qt-prefix', str(self.output / 'qt-build/install')).discover()

    def test_batch_expansion_rejected(self):
        for value in ('%PATH%', '!SECRET!', 'x\ny', 'x"y'):
            with self.assertRaises(builder.BuildError):
                builder.batch_command(['configure.bat', value])

    @unittest.skipUnless(os.name == 'nt', 'Windows build lock')
    def test_concurrent_build_rejected_and_lock_released_after_failure(self):
        with self.assertRaisesRegex(RuntimeError, 'interrupted'):
            with builder.build_lock(self.output):
                with self.assertRaisesRegex(builder.BuildError, 'Another build'):
                    with builder.build_lock(self.output):
                        self.fail('A concurrent build acquired the same directory')
                raise RuntimeError('interrupted')
        with builder.build_lock(self.output):
            pass

    @unittest.skipUnless(os.name == 'nt', 'Windows subprocess flags')
    def test_runner_logs_output_and_propagates_failure_and_timeout(self):
        build = self.make()
        build.build.mkdir(parents=True)
        build.env = dict(os.environ)
        logfile = build.build / 'build.log'
        with logfile.open('w', encoding='utf-8') as build.log, patch('sys.stdout', new_callable=io.StringIO) as console:
            build.run([sys.executable, '-c', "print('compiler output')"])
            with self.assertRaisesRegex(builder.BuildError, 'exit 9'):
                build.run([sys.executable, '-c', "print('compile failed'); raise SystemExit(9)"])
            with self.assertRaisesRegex(builder.BuildError, 'Timed out'):
                build.run([sys.executable, '-c', 'import time; time.sleep(30)'], timeout=0.2)
            self.assertIn('compiler output', console.getvalue())
        self.assertIn('compiler output', logfile.read_text())
        self.assertIn('compile failed', logfile.read_text())

    @unittest.skipUnless(os.name == 'nt', 'Windows process cancellation')
    def test_cancel_stops_child_before_releasing_build_lock(self):
        build = self.make()
        build.build.mkdir(parents=True)
        build.env = dict(os.environ)
        real_wait = subprocess.Popen.wait
        interrupted = False

        def wait(process, *args, **kwargs):
            nonlocal interrupted
            if not interrupted:
                interrupted = True
                raise KeyboardInterrupt
            return real_wait(process, *args, **kwargs)

        started = time.monotonic()
        with (build.build / 'build.log').open('w', encoding='utf-8') as build.log, patch('sys.stdout', new_callable=io.StringIO):
            with builder.build_lock(build.build), patch.object(subprocess.Popen, 'wait', wait):
                with self.assertRaises(KeyboardInterrupt):
                    build.run([sys.executable, '-c', 'import time; time.sleep(30)'])
        self.assertLess(time.monotonic() - started, 10)
        with builder.build_lock(build.build):
            pass

    @unittest.skipUnless(os.name == 'nt', 'Real Windows cmd quoting')
    def test_batch_spaces_and_metacharacters_remain_literal(self):
        script = self.root / 'space & dir/echo.cmd'
        self.file(script, '@echo off\r\necho "%~1"\r\n')
        result = subprocess.run(builder.batch_command([script, 'x & echo BROKEN']), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertEqual(result.stdout.strip(), '"x & echo BROKEN"')
        result = subprocess.run(builder.batch_command([script, 'value with spaces']), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        self.assertEqual(result.stdout.strip(), '"value with spaces"')


if __name__ == '__main__':
    unittest.main()
