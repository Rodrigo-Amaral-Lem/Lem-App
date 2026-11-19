# SPDX-License-Identifier: GPL-3.0-only
"""Build a static Windows Lem.exe using installed tools/sources; no downloads.

Generated Qt libraries, app objects, logs and the portable EXE default to the
repository's build/windows-static directory. Installed SDKs are read-only.
Python 3.10+, standard library only.
"""
import argparse
from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import threading

APP = Path(__file__).resolve().parents[1]
DEFAULT_BUILD = Path('build/windows-static')
MODULES = ('', 'Core', 'Gui', 'Widgets', 'Network', 'Concurrent', 'Qml', 'Quick', 'QuickControls2', 'QuickDialogs2', 'ShaderTools', 'Svg')
SOURCE_MODULES = ('qtbase', 'qtshadertools', 'qtdeclarative', 'qtsvg')
# The builder runs CMake explicitly before shortening paths. Ninja must use
# that edited manifest for the whole build. This does not change the cache ABI.
MANUAL_GENERATION = '-DCMAKE_SUPPRESS_REGENERATION=ON'
# Qt Quick generates paths well beyond CMake's object-file controls. Leave
# enough room for them when the checkout itself is deeply nested.
SHORT_BUILD_PATH_THRESHOLD = 96


class BuildError(Exception):
    pass


def version(value):
    match = re.fullmatch(r'(\d+)\.(\d+)\.(\d+)', value)
    return tuple(map(int, match.groups())) if match else (0, 0, 0)


def cmake_path(name, path):
    # RC compiler detection writes this value into a quoted .cmake file without
    # escaping backslashes. C:\Qt would become an invalid \Q escape there.
    return f'-D{name}={Path(path).as_posix()}'


def normalized_signature(value):
    if isinstance(value, str):
        return value.replace('\\', '/')
    if isinstance(value, list):
        return [normalized_signature(item) for item in value]
    if isinstance(value, dict):
        return {key: normalized_signature(item) for key, item in value.items()}
    return value


def qt_info(prefix):
    config = prefix / 'mkspecs/qconfig.pri'
    if not config.is_file():
        return None
    text = config.read_text(encoding='utf-8', errors='replace')
    match = re.search(r'^QT_VERSION\s*=\s*(\S+)', text, re.M)
    if not match:
        return None
    gcc = [re.search(rf'^QT_GCC_{part}_VERSION\s*=\s*(\d+)', text, re.M) for part in ('MAJOR', 'MINOR', 'PATCH')]
    return {'version': match[1], 'static': bool(re.search(r'^QT_CONFIG\s*\+=.*\bstatic\b', text, re.M)),
            'gcc': '.'.join(item[1] for item in gcc) if all(gcc) else None,
            'x64': bool(re.search(r'^QT_ARCH\s*=\s*x86_64\s*$', text, re.M)),
            'complete': all((prefix / f'lib/cmake/Qt6{module}/Qt6{module}Config.cmake').is_file() for module in MODULES)}


def source_version(source):
    config = source / 'qtbase/.cmake.conf'
    if config.is_file() and all((source / module / 'CMakeLists.txt').is_file() for module in SOURCE_MODULES) and (source / 'configure.bat').is_file():
        match = re.search(r'set\(QT_REPO_MODULE_VERSION\s+"([^"]+)"', config.read_text(encoding='utf-8'))
        if match:
            return match[1]
    return None


def inside(path, root):
    return path.resolve().is_relative_to(root.resolve())


def installed_root(prefix):
    # Versioned kits and flat custom static kits have different directory depths.
    for parent in list(prefix.parents)[:3]:
        if (parent / 'Tools').is_dir() or (parent / 'MaintenanceTool.exe').is_file():
            return parent
    return prefix.parent


def shorten_ninja_paths(directory):
    """MinGW's archiver and dependency writer still hit MAX_PATH.

    CMake's OBJECT_PATH_MAX cannot shorten generated Qt QML object basenames.
    Map long object nodes consistently across compilation and archive inputs,
    then shorten long DEP_FILE bindings. Unaffected commands remain unchanged.
    The original object layout is restored for CMake installation afterwards.
    """
    manifest = directory / 'build.ninja'
    original = manifest.read_text(encoding='utf-8')
    objects = {}
    for raw in re.findall(r'(?m)^build (.+?\.obj): [^\s]*COMPILER[^\s]* ', original):
        decoded = re.sub(r'\$([$: ])', r'\1', raw).replace('\\', '/')
        if len(str(directory / decoded)) >= 240:
            objects[raw] = '.lem-obj/' + hashlib.sha256(raw.encode('utf-8')).hexdigest()[:32] + '.obj'
    if objects:
        (directory / '.lem-obj').mkdir(exist_ok=True)
        for raw, short in sorted(objects.items(), key=lambda item: len(item[0]), reverse=True):
            original = original.replace(raw, short).replace(raw.replace('/', '\\'), short)
        (directory / 'lem-short-objects.json').write_text(json.dumps(objects, indent=2), encoding='utf-8')
    count = 0

    def rewrite(match):
        nonlocal count
        raw = match[2]
        decoded = re.sub(r'\$([$: ])', r'\1', raw).replace('\\', '/')
        path = Path(decoded)
        if not path.is_absolute():
            path = directory / path
        if len(str(path)) < 240:
            return match[0]
        count += 1
        name = hashlib.sha256(raw.encode('utf-8')).hexdigest()[:32]
        return match[1] + '.lem-deps/' + name + '.d'

    updated = re.sub(r'(?m)^(  DEP_FILE = )(.+)$', rewrite, original)
    if count or objects:
        (directory / '.lem-deps').mkdir(exist_ok=True)
        temporary = manifest.with_suffix('.ninja.tmp')
        temporary.write_text(updated, encoding='utf-8', newline='\n')
        temporary.replace(manifest)
    return count + len(objects)


def restore_install_objects(directory):
    """Keep Qt's generated installation/export paths, which use original names."""
    mapping = directory / 'lem-short-objects.json'
    if not mapping.exists():
        return
    for raw, short in json.loads(mapping.read_text(encoding='utf-8')).items():
        source = directory / short
        destination = directory / re.sub(r'\$([$: ])', r'\1', raw).replace('\\', '/')
        if not inside(source, directory) or not inside(destination, directory):
            raise BuildError('Object installation mapping points outside the build directory.')
        if not source.is_file():
            continue  # EXCLUDE_FROM_ALL objects are not necessarily built/installed.
        extended = Path('\\\\?\\' + str(destination.resolve())) if os.name == 'nt' else destination
        extended.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, extended)


def batch_command(args):
    values = [str(arg) for arg in args]
    if any(re.search(r'["%!\r\n]', arg) for arg in values):
        raise BuildError('Batch paths/arguments cannot contain quotes, %, ! or newlines.')
    # cmd /s /c needs raw double quoting, not the C-runtime list2cmdline escaping.
    return '"' + os.environ.get('COMSPEC', 'cmd.exe') + '" /d /v:off /s /c "' + ' '.join('"' + arg + '"' for arg in values) + '"'


@contextmanager
def build_lock(directory):
    """Windows releases the file lock even if a build is interrupted or killed."""
    import msvcrt
    directory.mkdir(parents=True, exist_ok=True)
    with (directory / '.lem-build.lock').open('a+b') as lock:
        if lock.tell() == 0:
            lock.write(b'0')
            lock.flush()
        lock.seek(0)
        try:
            msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
        except OSError as error:
            raise BuildError(f'Another build is using {directory}. Wait for it to finish.') from error
        try:
            yield
        finally:
            lock.seek(0)
            msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)


@contextmanager
def short_build_directory(directory, qt_build):
    """Keep the generated files in place and reuse the same path in CMake caches."""
    marker = directory / 'lem-build-drive.txt'
    saved = marker.read_text(encoding='ascii').strip() if marker.is_file() else ''
    if saved and not re.fullmatch(r'[D-Z]:', saved):
        raise BuildError(f'Invalid Windows build drive recorded in {marker}.')
    configured = any((path / 'CMakeCache.txt').exists() for path in (directory, qt_build))
    if not saved and (configured or len(str(directory)) < SHORT_BUILD_PATH_THRESHOLD):
        yield directory
        return
    subst = Path(os.environ.get('SystemRoot', 'C:/Windows')) / 'System32/subst.exe'
    candidates = (saved,) if saved else tuple(f'{letter}:' for letter in 'ZYXWVUTSRQP')
    # Keep the CMake build in a subdirectory. At a drive root, Qt can emit
    # both Z:/ and Z:// for the same generated source in its Ninja rules.
    parent = directory.parent
    for drive in candidates:
        root = Path(drive + '/')
        if root.exists():
            continue
        result = subprocess.run([str(subst), drive, str(parent)], capture_output=True,
                                creationflags=subprocess.CREATE_NO_WINDOW)
        if result.returncode:
            continue
        try:
            marker.write_text(drive + '\n', encoding='ascii')
            path = root / directory.name
            print(f'Using {path} for the build directory {directory}', flush=True)
            yield path
        finally:
            if root.exists() and root.samefile(parent):
                cleanup = subprocess.run([str(subst), drive, '/D'], capture_output=True,
                                         creationflags=subprocess.CREATE_NO_WINDOW)
                if cleanup.returncode:
                    raise BuildError(f'Could not release the temporary build drive {drive}.')
        return
    raise BuildError(f'No available Windows drive for the short build path ({saved or "Z: through P:"}).')


class Builder:
    def __init__(self, args, env=None):
        self.args = args
        self.env = dict(os.environ if env is None else env)
        # Keep the cache under the App repository, independent of the launch directory.
        self.build = Path(args.build_directory or APP.parent / DEFAULT_BUILD).resolve()
        self.output_directory = self.build
        self.qt_build = Path(args.qt_build or self.build / 'qt-build').resolve()
        self.log = None
        roots = [args.qt_root] if args.qt_root else [self.env.get('QT_ROOT'), 'C:/Qt', self.env.get('SystemDrive', 'C:') + '/Qt']
        if not args.qt_root:
            for name in ('qtpaths6.exe', 'qmake.exe'):
                tool = shutil.which(name, path=self.env.get('PATH', ''))
                if tool:
                    roots.append(str(installed_root(Path(tool).parents[1])))
            for name in ('QT_PREFIX', 'QT_STATIC_PREFIX'):
                if self.env.get(name):
                    roots.append(str(installed_root(Path(self.env[name]))))
        self.roots = list(dict.fromkeys(Path(root).resolve() for root in roots if root and Path(root).is_dir()))
        self.prefixes = list(dict.fromkeys(path for root in self.roots for pattern in ('*', '*/*') for path in root.glob(pattern) if qt_info(path)))

    def use_build_directory(self, path):
        if path == self.build:
            return
        if self.qt_build.is_relative_to(self.build):
            self.qt_build = path / self.qt_build.relative_to(self.build)
        if self.prefix.is_relative_to(self.build):
            self.prefix = path / self.prefix.relative_to(self.build)
        self.build = path

    def tool(self, name, candidates=()):
        for candidate in [*candidates, shutil.which(name, path=self.env.get('PATH', ''))]:
            if candidate and Path(candidate).is_file():
                return Path(candidate).resolve()
        raise BuildError(f'Missing {name}. Install it or add it to PATH; no packages are downloaded automatically.')

    def capture(self, args):
        result = subprocess.run([str(arg) for arg in args], env=self.env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=20)
        return result.stdout.strip() if result.returncode == 0 else ''

    def discover(self):
        if inside(APP, self.build):
            raise BuildError('BuildDirectory must be a separate output directory, not the app source directory or its parent.')
        if (self.build / 'CMakeCache.txt').exists() and not (self.build / 'lem-app-config.json').is_file():
            raise BuildError('Unowned app CMake cache. Select a new -BuildDirectory; existing build files are preserved.')
        for root in self.roots:
            if inside(self.build, root):
                raise BuildError(f'BuildDirectory must be outside the installed Qt directory. App default: {DEFAULT_BUILD.as_posix()}.')
        explicit = self.args.qt_prefix or self.env.get('QT_STATIC_PREFIX')
        local_prefix = self.build / 'qt'
        pending = (self.qt_build / 'lem-static-install-pending').exists()
        choices = [local_prefix] if self.args.build_qt or pending else [local_prefix, *sorted(self.prefixes, key=lambda p: version(qt_info(p)['version']), reverse=True)]
        if explicit:
            choices = [Path(explicit).resolve()]
        self.prefix = None
        self.info = None
        for candidate in choices:
            info = qt_info(candidate)
            if info and info['static'] and info['x64'] and info['complete'] and version(info['version']) >= (6, 12, 0) and bool(info['gcc']) == (self.args.compiler == 'MinGW'):
                self.prefix, self.info = candidate, info
                break
        if explicit and self.prefix is None and Path(explicit).exists() and any(Path(explicit).iterdir()):
            if not (inside(Path(explicit), self.build) and (self.qt_build / 'lem-static-config.json').is_file()):
                raise BuildError('QtPrefix requires a complete static x64 Qt 6.12+ kit for this compiler, including Qt SVG. Shared installer kits cannot produce a single EXE.')
        self.prefix = self.prefix or (Path(explicit).resolve() if explicit else local_prefix)
        if inside(self.build, self.prefix):
            raise BuildError('BuildDirectory cannot be inside the installed static Qt prefix.')
        self.build_qt = self.args.build_qt or self.info is None or pending
        self.source = None
        self.qt_version = self.info['version'] if self.info else None
        if self.build_qt:
            if not inside(self.prefix, self.build) or not inside(self.qt_build, self.build):
                raise BuildError('Generated Qt build/install directories must be inside BuildDirectory. External static kits are read-only; omit -BuildQt to reuse one.')
            if self.prefix == self.build or self.qt_build == self.build or inside(self.prefix, self.qt_build) or inside(self.qt_build, self.prefix):
                raise BuildError('QtBuild and QtPrefix must be separate subdirectories of BuildDirectory.')
            sources = [Path(self.args.qt_source).resolve()] if self.args.qt_source else [path for root in self.roots for path in [root / 'Src', *root.glob('*/Src')]]
            available = [(source_version(path), path) for path in sources]
            available = [(value, path) for value, path in available if value and version(value) >= (6, 12, 0)]
            if not available:
                raise BuildError('No installed Qt 6.12+ sources found. Install Sources with Qt Maintenance Tool or pass -QtSource, or supply an existing static -QtPrefix.')
            self.qt_version, self.source = max(available, key=lambda item: version(item[0]))
            if inside(self.build, self.source) or inside(self.source, self.build):
                raise BuildError('Installed Qt sources must be separate from generated build files.')
            if any(re.search(r'\s', str(path)) for path in (self.source, self.qt_build, self.prefix)):
                raise BuildError('Qt source builds need space-free source/build/install paths. Select a space-free app checkout or -BuildDirectory.')
        self.cmake = self.tool('cmake.exe', [root / 'Tools/CMake_64/bin/cmake.exe' for root in self.roots])
        self.ninja = self.tool('ninja.exe', [root / 'Tools/Ninja/ninja.exe' for root in self.roots])
        cmake_version = re.search(r'cmake version (\d+\.\d+\.\d+)', self.capture([self.cmake, '--version']))
        if not cmake_version or version(cmake_version[1]) < (3, 25, 0):
            raise BuildError('CMake 3.25+ is required.')
        self.perl = None
        if self.build_qt:
            self.perl = self.tool('perl.exe', [Path(self.env.get('ProgramFiles', 'C:/Program Files')) / 'Git/usr/bin/perl.exe', Path(self.env.get('LOCALAPPDATA', '')) / 'Programs/Git/usr/bin/perl.exe'])
        compiler_bin = self.args.toolchain_bin or self.env.get('QT_COMPILER_BIN')
        if self.args.compiler == 'MinGW':
            expected = self.info['gcc'] if self.info and not self.build_qt else None
            if not expected:
                for prefix in self.prefixes:
                    info = qt_info(prefix)
                    if info['version'] == self.qt_version and info['x64'] and info['gcc']:
                        expected = info['gcc']
                        break
            bins = [Path(compiler_bin)] if compiler_bin else [path / 'bin' for root in self.roots for path in sorted((root / 'Tools').glob('mingw*'), reverse=True)]
            on_path = shutil.which('g++.exe', path=self.env.get('PATH', ''))
            if on_path and not compiler_bin:
                bins.append(Path(on_path).parent)
            self.compiler = None
            for directory in bins:
                if not all((directory / name).is_file() for name in ('gcc.exe', 'g++.exe', 'windres.exe')):
                    continue
                actual = self.capture([directory / 'g++.exe', '-dumpfullversion'])
                machine = self.capture([directory / 'g++.exe', '-dumpmachine'])
                if (expected is None or actual == expected) and machine == 'x86_64-w64-mingw32':
                    self.compiler = (directory / 'g++.exe').resolve()
                    self.compiler_version = actual
                    break
            if self.compiler is None:
                raise BuildError(f'No matching x64 MinGW {expected or "compiler"} found. Pass -ToolchainBin pointing to the Qt kit compiler bin directory.')
        else:
            if self.env.get('VSCMD_ARG_TGT_ARCH') != 'x64':
                raise BuildError('MSVC builds require an x64 Native Tools / Developer PowerShell for Visual Studio.')
            self.compiler = self.tool('cl.exe', [Path(compiler_bin) / 'cl.exe'] if compiler_bin else [])
            self.tool('rc.exe')
            self.compiler_version = self.capture([self.compiler])
        directories = [self.compiler.parent, self.cmake.parent, self.ninja.parent, Path(sys.executable).parent]
        if self.perl:
            directories.append(self.perl.parent)
        self.env['PATH'] = os.pathsep.join(map(str, directories)) + os.pathsep + self.env.get('PATH', '')
        return self

    def plan(self):
        return {'app_source': str(APP), 'build_directory': str(self.output_directory), 'qt_prefix': str(self.prefix.resolve()),
                'qt_source': str(self.source) if self.source else None,
                'qt_build': str(self.qt_build.resolve()) if self.build_qt else None,
                'build_qt': self.build_qt, 'qt_version': self.qt_version, 'compiler': str(self.compiler),
                'compiler_version': self.compiler_version, 'cmake': str(self.cmake), 'ninja': str(self.ninja),
                'python': sys.executable, 'perl': str(self.perl) if self.perl else None,
                'executable': str(self.output_directory / 'portable/Lem.exe'), 'log': str(self.output_directory / 'build.log')}

    def say(self, message):
        print(message, flush=True)
        if self.log:
            self.log.write(message + '\n')
            self.log.flush()

    def run(self, args, cwd=None, env=None, timeout=None):
        self.say('[command] ' + subprocess.list2cmdline(list(map(str, args))))
        command = batch_command(args) if str(args[0]).lower().endswith(('.bat', '.cmd')) else list(map(str, args))
        with subprocess.Popen(command, cwd=cwd or self.build, env=env or self.env,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors='replace',
                              creationflags=subprocess.CREATE_NO_WINDOW) as process:
            # Keep the console responsive and the complete log together. A reader
            # thread lets the isolated smoke test retain its hard timeout.
            def forward_output():
                for line in process.stdout:
                    self.say(line.rstrip('\r\n'))
            reader = threading.Thread(target=forward_output, daemon=True)
            reader.start()
            try:
                code = process.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
                raise BuildError(f'Timed out: {args[0]}. See {self.build / "build.log"}')
            except KeyboardInterrupt:
                # Stop only this build command and its children before releasing
                # the directory lock. Orphaned Ninja/compiler jobs could otherwise
                # keep writing while the next run tries to resume the same cache.
                taskkill = Path(self.env.get('SystemRoot', 'C:/Windows')) / 'System32/taskkill.exe'
                subprocess.run([str(taskkill), '/PID', str(process.pid), '/T', '/F'],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                               creationflags=subprocess.CREATE_NO_WINDOW)
                if process.poll() is None:
                    process.kill()
                process.wait()
                raise
            finally:
                reader.join()
        if code:
            raise BuildError(f'{args[0]} failed (exit {code}). See {self.output_directory / "build.log"}')

    def guard(self, directory, filename, signature):
        marker = directory / filename
        if (directory / 'CMakeCache.txt').exists() or marker.exists():
            if not marker.is_file() or normalized_signature(json.loads(marker.read_text(encoding='utf-8'))) != normalized_signature(signature):
                raise BuildError(f'Incompatible or unowned build cache: {directory}. Select a new -BuildDirectory; existing files are preserved.')
        directory.mkdir(parents=True, exist_ok=True)
        marker.write_text(json.dumps(signature, indent=2), encoding='utf-8')

    def compile(self):
        self.build.mkdir(parents=True, exist_ok=True)
        # Never leave a previous successful result claiming this run passed.
        (self.build / 'build-result.json').unlink(missing_ok=True)
        with (self.build / 'build.log').open('a', encoding='utf-8') as self.log:
            self.say(json.dumps(self.plan(), indent=2))
            if self.build_qt:
                args = ['-static', '-static-runtime', '-release', '-cmake-generator', 'Ninja', '-platform',
                        'win32-g++' if self.args.compiler == 'MinGW' else 'win32-msvc', '-prefix', str(self.prefix),
                        '-submodules', ','.join(SOURCE_MODULES), '-nomake', 'tests', '-nomake', 'examples',
                        '-schannel', '-no-openssl', '-no-icu', '-opengl', 'desktop', '-qt-zlib', '-qt-libpng',
                        '-qt-libjpeg', '-qt-pcre', '-qt-freetype', '-qt-harfbuzz', '-qt-doubleconversion',
                        '--', '-DCMAKE_OBJECT_PATH_MAX=180', cmake_path('Python3_EXECUTABLE', sys.executable),
                        cmake_path('CMAKE_PROJECT_INCLUDE', APP / 'windows/ShortAutogenPaths.cmake')]
                signature = {'source': str(self.source), 'version': self.qt_version, 'compiler': str(self.compiler),
                             'compiler_version': self.compiler_version, 'arguments': args}
                self.guard(self.qt_build, 'lem-static-config.json', signature)
                pending = self.qt_build / 'lem-static-install-pending'
                pending.touch()
                if not (self.qt_build / 'build.ninja').is_file():
                    self.say('[1/6] Configure static Qt from installed sources')
                    self.run([self.source / 'configure.bat', *args, MANUAL_GENERATION], cwd=self.qt_build)
                else:
                    self.say('[1/6] Refresh the existing Qt configuration')
                    self.run([self.cmake, '-S', self.source, '-B', self.qt_build, MANUAL_GENERATION,
                              cmake_path('CMAKE_PROJECT_INCLUDE', APP / 'windows/ShortAutogenPaths.cmake')])
                if not (self.qt_build / 'build.ninja').is_file():
                    raise BuildError('Qt configure did not generate build.ninja. See build.log; rerun to resume.')
                shortened = shorten_ninja_paths(self.qt_build)
                self.say(f'Using short build-local paths for {shortened} long Qt object/dependency files.')
                self.say('[2/6] Build static Qt (first build can take considerable time)')
                self.run([self.cmake, '--build', self.qt_build, '--parallel', self.args.jobs])
                self.say('[3/6] Install static Qt inside the app build directory')
                restore_install_objects(self.qt_build)
                self.run([self.cmake, '--install', self.qt_build])
                info = qt_info(self.prefix)
                if not info or not info['static'] or not info['complete']:
                    raise BuildError('Static Qt installation is incomplete. Rerun to resume; see build.log.')
                pending.unlink()
            else:
                self.say('[1-3/6] Reuse installed static Qt (read-only)')
            args = ['-S', APP, '-B', self.build, '-G', 'Ninja', cmake_path('CMAKE_PREFIX_PATH', self.prefix),
                    '-DCMAKE_BUILD_TYPE=Release', '-DLEM_WINDOWS_STANDALONE=ON', '-DLEM_BUILD_TESTS=OFF',
                    cmake_path('Python3_EXECUTABLE', sys.executable), cmake_path('CMAKE_MAKE_PROGRAM', self.ninja),
                    cmake_path('CMAKE_CXX_COMPILER', self.compiler)]
            if self.args.compiler == 'MSVC':
                args.append('-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded')
            else:
                args.extend([cmake_path('CMAKE_C_COMPILER', self.compiler.parent / 'gcc.exe'), cmake_path('CMAKE_RC_COMPILER', self.compiler.parent / 'windres.exe')])
            self.guard(self.build, 'lem-app-config.json', {'arguments': list(map(str, args)), 'compiler_version': self.compiler_version})
            self.say('[4/6] Configure Lem')
            fresh = ['--fresh'] if (self.build / 'CMakeCache.txt').exists() and not (self.build / 'build.ninja').exists() else []
            self.run([self.cmake, *fresh, *args, MANUAL_GENERATION])
            if not (self.build / 'build.ninja').is_file():
                raise BuildError('Lem configure did not generate build.ninja. See build.log.')
            shorten_ninja_paths(self.build)
            self.say('[5/6] Build Lem')
            self.run([self.cmake, '--build', self.build, '--target', 'Lem', '--parallel', self.args.jobs])
            self.say('[6/6] Audit DLL imports and run isolated startup/TLS/JPEG/SVG test')
            self.run([sys.executable, APP / 'windows/verify-pe.py', self.build / 'Lem.exe', '--report', self.build / 'dll-imports.json'])
            portable = self.build / 'portable'
            portable.mkdir(exist_ok=True)
            if any(path.name != 'Lem.exe' for path in portable.iterdir()):
                raise BuildError('The portable directory must contain only Lem.exe. Existing files were preserved.')
            output = portable / 'Lem.exe'
            shutil.copy2(self.build / 'Lem.exe', output)
            isolated = self.env.copy()
            system = isolated.get('SystemRoot', 'C:\\Windows')
            isolated['PATH'] = system + '\\System32;' + system
            for key in ('QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QML_IMPORT_PATH', 'QML2_IMPORT_PATH'):
                isolated.pop(key, None)
            isolated.update(QT_QPA_PLATFORM='offscreen', QT_QUICK_BACKEND='software', QT_FORCE_STDERR_LOGGING='1',
                            QT_QPA_FONTDIR=str(Path(system) / 'Fonts'))
            self.run([output, '--smoke-test'], cwd=portable, env=isolated, timeout=30)
            (self.build / 'build-result.json').write_text(json.dumps({**self.plan(), 'verified': True}, indent=2), encoding='utf-8')
            self.say(f'Ready: {self.output_directory / "portable/Lem.exe"}')
        self.log = None


def parser():
    result = argparse.ArgumentParser(description=__doc__)
    for name in ('qt-prefix', 'qt-source', 'qt-build', 'qt-root', 'toolchain-bin', 'build-directory'):
        result.add_argument('--' + name)
    result.add_argument('--compiler', choices=('MinGW', 'MSVC'), default='MinGW')
    result.add_argument('--jobs', type=int, choices=range(1, 129), metavar='1..128', default=4)
    result.add_argument('--build-qt', action='store_true', help='Explicitly rebuild Qt inside the app build directory')
    result.add_argument('--plan', action='store_true', help='Print detected inputs/output paths without writing or compiling')
    return result


def main():
    if sys.platform != 'win32':
        raise BuildError('This tool requires Windows.')
    sys.stdout.reconfigure(errors='replace')
    sys.stderr.reconfigure(errors='replace')
    build = Builder(parser().parse_args()).discover()
    if build.args.plan:
        print(json.dumps(build.plan(), indent=2))
    else:
        with build_lock(build.build), short_build_directory(build.build, build.qt_build) as directory:
            build.use_build_directory(directory)
            build.compile()


if __name__ == '__main__':
    try:
        main()
    except (BuildError, OSError, ValueError) as error:
        print(f'FAILED: {error}', file=sys.stderr)
        sys.exit(1)
    except KeyboardInterrupt:
        print('Build cancelled. Run the same command to resume.', file=sys.stderr)
        sys.exit(130)
