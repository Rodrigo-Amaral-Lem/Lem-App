#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build/deploy Lem on Windows and Ubuntu. Python 3.10+, standard library only."""
from __future__ import annotations

import argparse
import contextlib
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import zipfile

REPO = Path(__file__).resolve().parents[1]
QT_VERSION = "6.12.0"
NDK_VERSION = "27.2.12479018"
BUILD_TOOLS = "37.0.0"
SDK_PLATFORM = "android-37.0"
OPENSSL_COMMIT = "b71f1470962019bd89534a2919f5925f93bc5779"
MODULES = ("qtbase", "qtshadertools", "qtdeclarative", "qtsvg", "qtimageformats")
ABIS = {"arm64-v8a": "android_arm64_v8a", "armeabi-v7a": "android_armv7",
        "x86_64": "android_x86_64", "x86": "android_x86"}
CLI_BUILD = "15859902"
CLI_SHA256 = {
    "win": "90ae805d20434428bffcb699c290860f19bb5f66a67e6b330067e3de801fb04a",
    "linux": "4e4c464f145a7512b57d088ac6c278c03c9eea610886b35a5e0804e74eedf583",
}
QT_PATCHES = {
    "CVE-2026-105924-qtbase-6.12.diff": "c27d27b86aaf2cdab0c18d513fbb94a432e98782eddfc5f856387e4be68be50b",
    "CVE-2026-105943-qtbase-6.12.diff": "dd22629ce948cf0a900421323cca331907180242a9b51c3fd75b25c75f068099",
    # Upstream numbered these in reverse dependency order: 0001's test extends
    # the header-size test introduced by 0002.
    "CVE-2026-116337-qtbase-6.12-0002.diff": "83abc89d507dbd9a94dc5718b0125492c1c37b0609a8f404a1f164925940faec",
    "CVE-2026-116337-qtbase-6.12-0001.diff": "74c5028985dcc0455255909de4dc80f50cf5472dbf465ce46ebc791a7b080f12",
}


class ToolError(RuntimeError):
    pass


def build_environment(environment, *, windows, inherit_windows_path=False, kernel=""):
    result = environment.copy()
    if not windows and not inherit_windows_path and (environment.get("WSL_DISTRO_NAME") or "microsoft" in kernel.lower()):
        # CMake expands PATH entries into package search prefixes. Searching
        # Windows mounts through WSL's 9P filesystem can add minutes to configure.
        # Explicit SDK/Qt/JDK paths are still honored and prepended by discovery.
        result["PATH"] = ":".join(path for path in environment.get("PATH", "").split(":")
                                  if not re.match(r"^/mnt/[a-zA-Z](?:/|$)", path))
    return result


def digest(path: Path, algorithm="sha256") -> str:
    h = hashlib.new(algorithm)
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def write_json(path: Path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
    temporary.replace(path)


def read_json(path: Path):
    return json.loads(path.read_text(encoding="utf-8"))


def download(url: str, destination: Path, checksum: str, algorithm="sha256") -> Path:
    if destination.is_file() and digest(destination, algorithm) == checksum:
        return destination
    destination.parent.mkdir(parents=True, exist_ok=True)
    partial = destination.with_suffix(destination.suffix + ".part")
    print(f"Downloading {url}", flush=True)
    try:
        with urllib.request.urlopen(url, timeout=120) as response, partial.open("wb") as output:
            shutil.copyfileobj(response, output, 1024 * 1024)
        if digest(partial, algorithm) != checksum:
            raise ToolError(f"Checksum mismatch: {url}")
        partial.replace(destination)
    finally:
        partial.unlink(missing_ok=True)
    return destination


def extract_zip(archive: Path, destination: Path):
    """Reject traversal/symlinks, and preserve executable bits on Linux."""
    destination.mkdir(parents=True, exist_ok=True)
    root = destination.resolve()
    with zipfile.ZipFile(archive) as z:
        for member in z.infolist():
            target = (root / member.filename).resolve()
            mode = member.external_attr >> 16
            if not target.is_relative_to(root) or (mode & 0o170000) == 0o120000:
                raise ToolError(f"Unsafe archive member: {member.filename}")
            z.extract(member, root)
            if mode & 0o111 and not member.is_dir():
                target.chmod(target.stat().st_mode | 0o111)


def batch_command(args):
    # cmd expands % and ! even inside quotes. Reject them instead of attempting
    # fragile shell escaping. Everything else is quoted, including SDK package ';'.
    values = [str(a) for a in args]
    if any(re.search(r'["%!\r\n]', a) for a in values):
        raise ToolError('Windows batch tool paths/arguments cannot contain quotes, %, ! or newlines.')
    # Pass a raw command line: list2cmdline's C-runtime escaping is incompatible
    # with cmd.exe's /s /c quoting and can expose shell metacharacters in paths.
    command = '"' + " ".join('"' + a + '"' for a in values) + '"'
    return '"' + os.environ.get("COMSPEC", "cmd.exe") + '" /d /v:off /s /c ' + command


def qt_version(prefix: Path):
    config = prefix / "mkspecs/qconfig.pri"
    if config.is_file():
        match = re.search(r"^QT_VERSION\s*=\s*(\S+)", config.read_text(), re.M)
        if match:
            return match[1]
    return None


def select_device(output: str, serial: str | None) -> str:
    devices = {}
    for line in output.splitlines():
        fields = line.split()
        if len(fields) >= 2 and fields[1] in ("device", "unauthorized", "offline", "recovery", "sideload", "bootloader", "authorizing", "connecting"):
            devices[fields[0]] = fields[1]
        elif len(fields) >= 3 and fields[1:3] == ["no", "permissions"]:
            devices[fields[0]] = "no permissions (check Android USB/udev rules)"
    if serial:
        if devices.get(serial) != "device":
            raise ToolError(f"Device {serial}: {devices.get(serial, 'not connected')}. Unlock it and authorize USB debugging.")
        return serial
    if len(devices) != 1:
        raise ToolError("Connect one phone, or pass --serial SERIAL. Devices: " + (str(devices) if devices else "none"))
    return select_device(output, next(iter(devices)))


@contextlib.contextmanager
def lock_directory(directory: Path):
    """OS lock is released even when the process dies; no stale PID lock."""
    directory.mkdir(parents=True, exist_ok=True)
    with (directory / ".lem-android.lock").open("a+b") as handle:
        handle.seek(0)
        handle.write(b"0")
        handle.flush()
        handle.seek(0)
        try:
            if os.name == "nt":
                import msvcrt
                msvcrt.locking(handle.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError as error:
            raise ToolError(f"Another Lem Android command is using {directory}") from error
        try:
            yield
        finally:
            if os.name == "nt":
                handle.seek(0)
                msvcrt.locking(handle.fileno(), msvcrt.LK_UNLCK, 1)


class Android:
    def __init__(self, args):
        self.args = args
        self.windows = os.name == "nt"
        self.env = build_environment(os.environ, windows=self.windows,
                                     inherit_windows_path=args.inherit_windows_path, kernel=platform.release())
        self.prepend(Path(sys.executable).parent)
        self.exe = ".exe" if self.windows else ""
        self.qt_root = Path(args.qt_root or ("C:/Qt" if self.windows else Path.home() / "Qt")).expanduser().resolve()
        sdk_default = (Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData/Local")) / "Android/Sdk"
                       if self.windows else Path.home() / "Android/Sdk")
        self.sdk = Path(args.sdk or self.env.get("ANDROID_SDK_ROOT") or self.env.get("ANDROID_HOME") or sdk_default).expanduser().resolve()
        self.cache = Path(args.cache_dir or self.qt_root / ".lem-android").expanduser().resolve()
        self.build = Path(args.build_dir or REPO / "build" / f"android-{args.abi}").expanduser().resolve()
        version_root = self.qt_root / args.qt_version
        self.host = Path(args.qt_host or self.env.get("QT_HOST_PATH") or version_root / ("mingw_64" if self.windows else "gcc_64")).expanduser().resolve()
        self.target = Path(args.qt_android or version_root / ABIS[args.abi]).expanduser().resolve()
        self.ndk = self.sdk / "ndk" / NDK_VERSION
        self.receipt = self.build / "lem-apk.json"

    def run(self, args, *, cwd=None, capture=False, input_text=None, check=True):
        args = [str(a) for a in args]
        print("+ " + shlex.join(args), flush=True)
        command = batch_command(args) if self.windows and args[0].lower().endswith((".bat", ".cmd")) else args
        result = subprocess.run(command, cwd=cwd, env=self.env, text=True, errors="replace",
                                stdout=subprocess.PIPE if capture else None,
                                stderr=subprocess.STDOUT if capture else None, input=input_text)
        if check and result.returncode:
            if capture:
                print(result.stdout, flush=True)
            raise ToolError(f"Command failed ({result.returncode}): {args[0]}")
        return result

    def prepend(self, *directories):
        self.env["PATH"] = os.pathsep.join(str(p) for p in directories) + os.pathsep + self.env.get("PATH", "")

    def missing(self, description):
        if self.args.no_bootstrap:
            raise ToolError(f"Missing {description}; install it or run without --no-bootstrap.")
        print(f"Setting up {description}", flush=True)

    def apt(self, packages):
        if not Path("/etc/os-release").is_file() or 'ID=ubuntu' not in Path("/etc/os-release").read_text():
            raise ToolError("Automatic Linux dependency installation supports Ubuntu. Install prerequisites manually or use Ubuntu.")
        status = self.run(["dpkg-query", "-W", "-f=${binary:Package}\\t${db:Status-Status}\\n", "--", *packages],
                          capture=True, check=False)
        installed = {line.split("\t")[0].split(":")[0] for line in status.stdout.splitlines()
                     if line.endswith("\tinstalled")}
        missing = [package for package in packages if package not in installed]
        if not missing:
            return
        prefix = [] if os.geteuid() == 0 else ["sudo"]
        self.run([*prefix, "apt-get", "update"])
        self.run([*prefix, "apt-get", "install", "-y", *missing])

    def winget(self, package):
        winget = shutil.which("winget", path=self.env["PATH"])
        if not winget:
            raise ToolError("Install Microsoft App Installer (winget), then rerun; or supply already-installed dependencies.")
        self.run([winget, "install", "--id", package, "--exact", "--source", "winget",
                  "--accept-source-agreements", "--accept-package-agreements", "--disable-interactivity"])

    def executable(self, name, candidates=(), minimum=None):
        choices = [*candidates, shutil.which(name, path=self.env["PATH"])]
        for candidate in choices:
            if not candidate or not Path(candidate).is_file():
                continue
            result = self.run([candidate, "--version"], capture=True, check=False)
            match = re.search(r"(\d+)\.(\d+)(?:\.(\d+))?", result.stdout or "")
            if result.returncode == 0 and (minimum is None or (match and tuple(int(n or 0) for n in match.groups()) >= minimum)):
                return Path(candidate).resolve()
        return None

    def cmake_tools(self):
        scripts = self.cache / "python-tools" / ("Scripts" if self.windows else "bin")
        self.cmake = self.executable("cmake", [self.qt_root / "Tools/CMake_64/bin/cmake.exe", scripts / ("cmake" + self.exe)], (3, 25, 0))
        self.ninja = self.executable("ninja", [self.qt_root / "Tools/Ninja/ninja.exe", scripts / ("ninja" + self.exe)])
        if not self.cmake or not self.ninja:
            self.missing("CMake 3.25+ and Ninja (isolated Python environment)")
            if not self.windows:
                self.apt(["python3-venv"])
            self.run([sys.executable, "-m", "venv", self.cache / "python-tools"])
            python = scripts / ("python.exe" if self.windows else "python")
            self.run([python, "-m", "pip", "install", "cmake==3.30.5", "ninja==1.13.0"])
            self.cmake = self.cmake or self.executable("cmake", [scripts / ("cmake" + self.exe)], (3, 25, 0))
            self.ninja = self.ninja or self.executable("ninja", [scripts / ("ninja" + self.exe)])
        if not self.cmake or not self.ninja:
            raise ToolError("CMake/Ninja installation did not provide working executables.")
        self.prepend(self.cmake.parent, self.ninja.parent)

    def java(self):
        def find():
            candidates = [self.args.java_home, self.env.get("JAVA_HOME")]
            command = shutil.which("javac", path=self.env["PATH"])
            if command:
                candidates.append(Path(command).resolve().parent.parent)
            roots = [Path("C:/Program Files/Eclipse Adoptium"), Path("C:/Program Files/Java")] if self.windows else [Path("/usr/lib/jvm")]
            for root in roots:
                if root.exists():
                    candidates.extend(sorted(root.glob("*21*"), reverse=True))
            for candidate in candidates:
                if not candidate:
                    continue
                home = Path(candidate).expanduser().resolve()
                javac = home / "bin" / ("javac" + self.exe)
                if not javac.is_file():
                    continue
                result = self.run([javac, "-version"], capture=True, check=False)
                if result.returncode == 0 and re.search(r"javac 21(?:\.|\s|$)", result.stdout):
                    return home
            return None
        home = find()
        if not home:
            self.missing("JDK 21")
            if self.args.java_home:
                raise ToolError("--java-home does not contain a working JDK 21.")
            self.winget("EclipseAdoptium.Temurin.21.JDK") if self.windows else self.apt(["openjdk-21-jdk-headless"])
            home = find()
        if not home:
            raise ToolError("JDK 21 not found after installation. Pass --java-home PATH.")
        self.env["JAVA_HOME"] = str(home)
        self.prepend(home / "bin")

    def sdk_manager(self):
        suffix = ".bat" if self.windows else ""
        managers = [self.sdk / "cmdline-tools/latest/bin" / ("sdkmanager" + suffix)]
        managers.extend(sorted(self.sdk.glob(f"cmdline-tools/*/bin/sdkmanager{suffix}"), reverse=True))
        for manager in managers:
            if manager.is_file():
                return manager
        self.missing("Android SDK command-line tools")
        system = "win" if self.windows else "linux"
        filename = f"commandlinetools-{system}-{CLI_BUILD}_latest.zip"
        archive = download(f"https://dl.google.com/android/repository/{filename}", self.cache / "downloads" / filename, CLI_SHA256[system])
        destination = self.sdk / "cmdline-tools" / ("lem-" + CLI_BUILD)
        if destination.exists():
            raise ToolError(f"Incomplete command-line tools at {destination}; move that directory aside and retry.")
        with tempfile.TemporaryDirectory(dir=self.cache) as temporary:
            extract_zip(archive, Path(temporary))
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.move(str(Path(temporary) / "cmdline-tools"), destination)
        return destination / "bin" / ("sdkmanager" + suffix)

    def android_sdk(self, deploy_only=False):
        self.env.update(ANDROID_HOME=str(self.sdk), ANDROID_SDK_ROOT=str(self.sdk), ANDROID_NDK_ROOT=str(self.ndk))
        required = {"platform-tools": self.sdk / "platform-tools" / ("adb" + self.exe)}
        if not deploy_only:
            required.update({f"platforms;{SDK_PLATFORM}": self.sdk / "platforms" / SDK_PLATFORM / "android.jar",
                             f"build-tools;{BUILD_TOOLS}": self.sdk / "build-tools" / BUILD_TOOLS / ("aapt2" + self.exe),
                             f"ndk;{NDK_VERSION}": self.ndk / "build/cmake/android.toolchain.cmake"})
        missing = [name for name, path in required.items() if not path.is_file()]
        if missing:
            self.missing("Android packages: " + ", ".join(missing))
            self.java()
            manager = self.sdk_manager()
            answers = "y\n" * 100 if self.args.accept_licenses else None
            self.run([manager, f"--sdk_root={self.sdk}", "--licenses"], input_text=answers)
            self.run([manager, f"--sdk_root={self.sdk}", *missing], input_text=answers)
            absent = [str(p) for p in required.values() if not p.is_file()]
            if absent:
                raise ToolError("SDK installation incomplete: " + ", ".join(absent))
        self.adb = required["platform-tools"]

    def git(self):
        def find():
            git = shutil.which("git", path=self.env["PATH"])
            if not git and self.windows:
                candidates = [Path("C:/Program Files/Git/cmd/git.exe"),
                              Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData/Local")) / "Programs/Git/cmd/git.exe"]
                git = next((str(p) for p in candidates if p.is_file()), None)
            return git
        git = find()
        if not git:
            self.missing("Git")
            self.winget("Git.Git") if self.windows else self.apt(["git", "ca-certificates"])
            git = find()
        if not git:
            raise ToolError("Git was not found. Open a fresh terminal after installation.")
        self.prepend(Path(git).parent)
        return git

    def openssl(self):
        root = Path(self.args.openssl_root or self.env.get("LEM_ANDROID_OPENSSL_ROOT") or self.sdk / "android_openssl").resolve()
        def valid():
            return all((root / "ssl_3" / self.args.abi / name).is_file() for name in ("libcrypto_3.so", "libssl_3.so"))
        if not valid():
            self.missing("Android OpenSSL 3 libraries")
            if root.exists():
                raise ToolError(f"OpenSSL directory is incomplete: {root}. Supply --openssl-root or move it aside; it will not be overwritten.")
            git = self.git()
            root.parent.mkdir(parents=True, exist_ok=True)
            with tempfile.TemporaryDirectory(prefix="lem-openssl-", dir=self.cache) as temporary:
                checkout = Path(temporary) / "repo"
                self.run([git, "init", checkout])
                self.run([git, "-C", checkout, "remote", "add", "origin", "https://github.com/KDAB/android_openssl.git"])
                self.run([git, "-C", checkout, "fetch", "--depth", "1", "origin", OPENSSL_COMMIT])
                self.run([git, "-C", checkout, "checkout", "--detach", "FETCH_HEAD"])
                shutil.move(str(checkout), root)
            if not valid():
                raise ToolError("Downloaded OpenSSL libraries are incomplete.")
        return root

    def compiler(self, source_build=False):
        if not self.windows:
            if source_build:
                self.missing("Ubuntu Qt source-build prerequisites")
                self.apt(["build-essential", "perl", "pkg-config", "libgl1-mesa-dev", "libegl1-mesa-dev",
                          "libfontconfig1-dev", "libfreetype-dev", "libx11-xcb-dev", "libxkbcommon-dev",
                          "libxkbcommon-x11-dev", "libxcb-cursor-dev", "libxcb-icccm4-dev", "libxcb-image0-dev",
                          "libxcb-keysyms1-dev", "libxcb-randr0-dev", "libxcb-render-util0-dev", "libxcb-shape0-dev",
                          "libxcb-shm0-dev", "libxcb-sync-dev", "libxcb-xfixes0-dev", "libxcb-xinerama0-dev",
                          "libssl-dev", "zlib1g-dev", "libdbus-1-dev"])
            return
        candidates = [self.args.compiler_bin, self.env.get("QT_COMPILER_BIN"), self.qt_root / "Tools/mingw1510_64/bin"]
        config = self.host / "mkspecs/qconfig.pri"
        expected = "15.1.0"
        if config.is_file():
            values = [re.search(rf"^QT_GCC_{part}_VERSION\s*=\s*(\d+)", config.read_text(), re.M) for part in ("MAJOR", "MINOR", "PATCH")]
            if all(values):
                expected = ".".join(v[1] for v in values)
        candidates.extend(sorted((self.qt_root / "Tools").glob("mingw*/bin"), reverse=True))
        for directory in candidates:
            if directory and (Path(directory) / "g++.exe").is_file():
                result = self.run([Path(directory) / "g++.exe", "-dumpfullversion"], capture=True, check=False)
                if result.returncode == 0 and result.stdout.strip() == expected:
                    self.prepend(Path(directory))
                    return
        self.missing(f"MinGW {expected}")
        if expected != "15.1.0":
            raise ToolError(f"Supply --compiler-bin for the host Qt kit's MinGW {expected}.")
        destination = self.qt_root / "Tools/mingw1510_64"
        if destination.exists():
            raise ToolError(f"Incomplete MinGW at {destination}; move it aside or supply --compiler-bin.")
        filename = "15.1.0-202610271023mingw1510.7z"
        url = "https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/tools_mingw1510/qt.tools.win64_mingw1510/" + filename
        archive = download(url, self.cache / "downloads" / filename,
                           "23250e2bb68d8e9a54170975d0027a405175c7ed01b397ffe72e2e89bfe00ed3")
        destination.mkdir(parents=True)
        self.run([self.cmake, "-E", "tar", "xf", archive], cwd=destination)
        if not (destination / "bin/g++.exe").is_file():
            raise ToolError(f"Unexpected MinGW archive layout under {destination}.")
        self.prepend(destination / "bin")

    def qt(self):
        missing = []
        for prefix in (self.host, self.target):
            version = qt_version(prefix)
            if version and version != self.args.qt_version:
                raise ToolError(f"{prefix} is Qt {version}, expected {self.args.qt_version}. Host and Android kits must match.")
            configs = [prefix / f"lib/cmake/Qt6{module}/Qt6{module}Config.cmake" for module in ("Core", "Gui", "Qml", "Quick", "QuickControls2", "QuickDialogs2", "Network", "Concurrent", "ShaderTools")]
            marker = prefix / ".lem-source-install.json"
            incomplete = marker.exists() and read_json(marker).get("status") != "complete"
            if not version or incomplete or not all(p.is_file() for p in configs):
                missing.append(prefix)
        toolchain = self.target / "lib/cmake/Qt6/qt.toolchain.cmake"
        if self.target not in missing and toolchain.is_file():
            abi = re.search(r'set\(ANDROID_ABI\s+"([^\"]+)"', toolchain.read_text())
            if abi and abi[1] != self.args.abi:
                raise ToolError(f"Android Qt kit is for {abi[1]}, requested {self.args.abi}. Select its matching kit.")
        self.compiler(source_build=bool(missing))
        if missing:
            self.missing("Qt host/Android source build (this can take hours and tens of GB)")
            self.build_qt(missing)
        # Host Qt tools need both the kit's DLLs and its matching MinGW runtime.
        self.prepend(self.host / "bin")

    def build_qt(self, missing):
        if self.target in missing and not (self.openssl_root / "ssl_3/include/openssl/ssl.h").is_file():
            raise ToolError("Building Qt requires OpenSSL headers at <openssl-root>/ssl_3/include/openssl/ssl.h.")
        git = self.git()
        if self.windows:
            perl_dir = Path(git).resolve().parents[1] / "usr/bin"
            if (perl_dir / "perl.exe").exists():
                # Add after compiler tools, so Git utilities cannot replace them.
                self.env["PATH"] += os.pathsep + str(perl_dir)
        if not shutil.which("perl", path=self.env["PATH"]):
            raise ToolError("Qt source builds require Perl. Install Git for Windows (includes Perl), or Ubuntu perl.")
        source = self.cache / "src" / f"qt-{self.args.qt_version}"
        if self.windows and any(" " in str(p) for p in (source, self.host, self.target)):
            raise ToolError("Qt source/build/install paths on Windows must not contain spaces. Use --qt-root C:\\Qt and a space-free --cache-dir.")
        for prefix in missing:
            marker = prefix / ".lem-source-install.json"
            if prefix.exists() and any(prefix.iterdir()) and not marker.is_file():
                raise ToolError(f"Refusing to overwrite unmanaged/incomplete Qt at {prefix}; use a separate --qt-root.")
        if not source.exists():
            source.parent.mkdir(parents=True, exist_ok=True)
            self.run([git, "-c", "core.longpaths=true", "clone", "--depth", "1", "--branch", f"v{self.args.qt_version}",
                      "https://code.qt.io/qt/qt5.git", source])
        self.run([git, "-C", source, "-c", "core.longpaths=true", "submodule", "update", "--init", "--depth", "1", *MODULES])
        if self.args.qt_version == "6.12.0":
            for name, checksum in QT_PATCHES.items():
                patch = download("https://download.qt.io/official_releases/qt/6.12/" + name, self.cache / "downloads" / name, checksum)
                # Later upstream tests extend earlier ones, so reverse-check the
                # runtime hunks when resuming. Apply the full patch on first use.
                applied = self.run([git, "-C", source / "qtbase", "apply", "--reverse", "--check", "--exclude=tests/*", patch], capture=True, check=False)
                if applied.returncode != 0:
                    self.run([git, "-C", source / "qtbase", "apply", "--check", patch])
                    self.run([git, "-C", source / "qtbase", "apply", patch])
        configure = source / ("configure.bat" if self.windows else "configure")
        for prefix in (self.host, self.target):
            if prefix not in missing:
                continue
            android = prefix == self.target
            directory = self.cache / "qt-build" / self.args.qt_version / (self.args.abi if android else "host")
            options = [configure, "-prefix", prefix, "-release", "-opensource", "-nomake", "examples", "-nomake", "tests",
                       "-submodules", ",".join(MODULES)]
            if self.args.accept_licenses:
                options.append("-confirm-license")
            if android:
                options += ["-qt-host-path", self.host, "-android-abis", self.args.abi,
                            "-android-sdk", self.sdk, "-android-ndk", self.ndk, "-openssl-runtime"]
            options += ["--", "-G", "Ninja", f"-DCMAKE_MAKE_PROGRAM={self.ninja}",
                        f"-DPython_EXECUTABLE={sys.executable}", f"-DPython3_EXECUTABLE={sys.executable}"]
            if android:
                ssl = self.openssl_root / "ssl_3"
                options += [f"-DOPENSSL_INCLUDE_DIR={ssl}/include",
                            f"-DOPENSSL_SSL_LIBRARY={ssl}/{self.args.abi}/libssl_3.so",
                            f"-DOPENSSL_CRYPTO_LIBRARY={ssl}/{self.args.abi}/libcrypto_3.so",
                            "-DQT_ALLOW_DOWNLOAD=ON", f"-DQT_ANDROID_SDK_BUILD_TOOLS_REVISION={BUILD_TOOLS}"]
            config = {"options": [str(x) for x in options], "patches": QT_PATCHES if self.args.qt_version == "6.12.0" else {},
                      "compiler": shutil.which("g++", path=self.env["PATH"])}
            self.guard_build(directory, config)
            prefix.mkdir(parents=True, exist_ok=True)
            write_json(prefix / ".lem-source-install.json", {"status": "building", **config})
            self.configure_qt(options, directory)
            self.run([self.cmake, "--build", directory, "--parallel", self.args.jobs])
            self.run([self.cmake, "--install", directory])
            write_json(prefix / ".lem-source-install.json", {"status": "complete", **config})
            if not android:
                self.prepend(self.host / "bin")
        self.prepend(self.host / "bin")

    def configure_qt(self, options, directory):
        if not self.windows:
            self.run(options, cwd=directory)
            return
        # Qt 6.12's top-level configure.bat ends with DEL and can return success
        # after CMake failed. Invoke its CMake entry point directly, preserving
        # the same option-file format, licensing and submodule selection.
        arguments = [str(a) for a in options[1:]]
        if any("\n" in a or "\r" in a for a in arguments):
            raise ToolError("Qt configure arguments cannot contain newlines.")
        optfile = directory / "lem-configure.opt"
        optfile.write_text("\n".join(arguments) + "\n", encoding="utf-8")
        source = Path(options[0]).parent
        self.run([self.cmake, f"-DOPTFILE={optfile}", f"-DTOP_LEVEL_SRC_PATH={source}",
                  "-P", source / "cmake/QtTopLevelConfigureScript.cmake"], cwd=directory)

    @staticmethod
    def guard_build(directory, config):
        marker = directory / "lem-build-config.json"
        if marker.exists() and read_json(marker) != config:
            raise ToolError(f"Build configuration changed. Choose a fresh --build-dir (or Qt --cache-dir): {directory}")
        if not marker.exists() and (directory / "CMakeCache.txt").exists():
            raise ToolError(f"Existing unmanaged CMake build at {directory}. Choose a fresh directory.")
        write_json(marker, config)

    def build_apk(self):
        # Invalidate before dependency/configure work: deploy must never use an old
        # success receipt after any failed attempt to rebuild this directory.
        self.receipt.unlink(missing_ok=True)
        self.cmake_tools()
        self.java()
        self.android_sdk()
        self.openssl_root = self.openssl()
        self.qt()
        self.env["QT_HOST_PATH"] = str(self.host)
        config = {"source": str(REPO), "host": str(self.host), "qt": str(self.target), "sdk": str(self.sdk),
                  "ndk": str(self.ndk), "abi": self.args.abi, "type": "Debug", "cmake": str(self.cmake),
                  "ninja": str(self.ninja), "openssl": str(self.openssl_root), "java": self.env["JAVA_HOME"]}
        self.guard_build(self.build, config)
        self.run([self.cmake, "-S", REPO / "app", "-B", self.build, "-G", "Ninja",
                  f"-DCMAKE_MAKE_PROGRAM={self.ninja}", f"-DCMAKE_TOOLCHAIN_FILE={self.target}/lib/cmake/Qt6/qt.toolchain.cmake",
                  f"-DQT_HOST_PATH={self.host}", f"-DANDROID_SDK_ROOT={self.sdk}", f"-DANDROID_NDK_ROOT={self.ndk}",
                  f"-DANDROID_ABI={self.args.abi}", "-DANDROID_PLATFORM=android-28", "-DCMAKE_BUILD_TYPE=Debug",
                  "-DQT_USE_TARGET_ANDROID_BUILD_DIR=ON", f"-DQT_ANDROID_SDK_BUILD_TOOLS_REVISION={BUILD_TOOLS}",
                  f"-DLEM_ANDROID_OPENSSL_ROOT={self.openssl_root}", "-DLEM_BUILD_TESTS=OFF", "-DLEM_WINDOWS_STANDALONE=OFF"])
        self.run([self.cmake, "--build", self.build, "--target", "apk", "--parallel", self.args.jobs])
        candidates = list(self.build.glob("android-build*/build/outputs/apk/debug/*.apk"))
        if len(candidates) != 1:
            raise ToolError(f"Expected one debug APK, found {len(candidates)} under {self.build}.")
        apk = candidates[0].resolve()
        write_json(self.receipt, {"apk": str(apk), "sha256": digest(apk), "abi": self.args.abi, "config": config})
        print(f"\nAPK ready: {apk}\nDeploy with the same --build-dir and --abi options.", flush=True)

    def deploy(self):
        if self.args.apk:
            apk = Path(self.args.apk).expanduser().resolve()
            if not apk.is_file() or apk.suffix.lower() != ".apk":
                raise ToolError(f"APK not found: {apk}")
        else:
            if not self.receipt.is_file():
                raise ToolError("No successful build receipt. Run build first, or explicitly select --apk PATH.")
            receipt = read_json(self.receipt)
            if receipt.get("abi") != self.args.abi:
                raise ToolError(f"Build receipt is for {receipt.get('abi')}; pass the matching --abi to deploy.")
            apk = Path(receipt["apk"])
            if not apk.is_file() or digest(apk) != receipt["sha256"]:
                raise ToolError("APK changed or is missing since build. Rebuild before deploying.")
        self.android_sdk(deploy_only=True)
        result = self.run([self.adb, "devices", "-l"], capture=True)
        serial = select_device(result.stdout, self.args.serial)
        self.run([self.adb, "-s", serial, "install", "-r", apk])
        if self.args.launch:
            self.run([self.adb, "-s", serial, "shell", "am", "start", "-n", "pt.lem.app/org.qtproject.qt.android.bindings.QtActivity"])
        print(f"Installed {apk.name} on {serial}. Existing app data was retained.", flush=True)


def parser():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    p.add_argument("command", choices=("build", "deploy"))
    p.add_argument("--abi", choices=tuple(ABIS), default="arm64-v8a")
    p.add_argument("--build-dir", help="App build/output directory; default App/build/android-<ABI>")
    p.add_argument("--qt-root", help="Qt installation root; default C:\\Qt or ~/Qt")
    p.add_argument("--qt-version", default=QT_VERSION)
    p.add_argument("--qt-host", help="Existing desktop Qt kit, matching the Android kit version")
    p.add_argument("--qt-android", help="Existing Android Qt kit for the selected ABI")
    p.add_argument("--sdk", help="Android SDK root; also accepts ANDROID_SDK_ROOT/ANDROID_HOME")
    p.add_argument("--java-home", help="JDK 21 root")
    p.add_argument("--openssl-root", help="Directory containing ssl_3/<ABI>/libcrypto_3.so and libssl_3.so")
    p.add_argument("--compiler-bin", help="Matching Windows host Qt MinGW bin directory")
    p.add_argument("--cache-dir", help="Dependency downloads and Qt source/build directories; default <qt-root>/.lem-android")
    p.add_argument("--jobs", type=int, default=min(4, os.cpu_count() or 1))
    p.add_argument("--no-bootstrap", action="store_true", help="Fail instead of downloading/installing dependencies")
    p.add_argument("--inherit-windows-path", action="store_true", help="In WSL, retain inherited /mnt/<drive> PATH entries for custom tool setups")
    p.add_argument("--accept-licenses", action="store_true", help="Accept Qt open-source/Android SDK licenses noninteractively")
    p.add_argument("--serial", help="adb device serial (required when several devices are connected)")
    p.add_argument("--apk", help="Deploy an explicitly selected APK instead of the last successful build")
    p.add_argument("--launch", action="store_true", help="Open Lem after deployment")
    return p


def main(argv=None):
    p = parser()
    args = p.parse_args(argv)
    if not 1 <= args.jobs <= 128:
        p.error("--jobs must be between 1 and 128")
    if not re.fullmatch(r"6\.12\.\d+", args.qt_version):
        p.error("This dependency matrix supports Qt 6.12.x")
    if args.command == "build" and (args.apk or args.serial or args.launch):
        p.error("--apk, --serial and --launch apply only to deploy")
    if platform.system() not in ("Windows", "Linux") or platform.machine().lower() not in ("amd64", "x86_64"):
        p.error("Supported hosts: Windows x64 and Ubuntu x86_64")
    try:
        task = Android(args)
        with lock_directory(task.build):
            if args.command == "build":
                with lock_directory(task.cache):
                    task.build_apk()
            else:
                task.deploy()
        return 0
    except (ToolError, OSError, ValueError, KeyError, zipfile.BadZipFile) as error:
        print(f"\nERROR: {error}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("\nCancelled; rerun the same command to resume.", file=sys.stderr)
        return 130


if __name__ == "__main__":
    sys.exit(main())
