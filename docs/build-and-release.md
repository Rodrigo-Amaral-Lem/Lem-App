# Build and release the client

This guide describes the current `0.7.0` source. Run all commands from the **App repository
root**, which contains `app/`, `shared/` and `docs/`. Build and staging examples use ignored
subdirectories of `build/`; no sibling working directory is required.

The current version is **0.7.0**, Android code **190**, checklist **7**. The minimum is **0.6.1**.
Client 0.6.1 receives an update notice and can still sign in and sync. Clients through 0.6.0
label commissioning work-order items as preventive and receive the upgrade explanation.
Update the API and its version settings before distributing the app. Install over the existing
profile to retain local reports.
Prepare detailed messages and user-executed `git commit -S` commands under the
[version policy](../../Common/docs/maintenance.md#version-policy).

## Prerequisites and kit selection

The project requires CMake **3.25+**, C++17 and Qt **6.12+**, with Core, Gui, Qml, Quick,
QuickControls2, QuickDialogs2, Network, Concurrent and Svg. Windows also uses Qt Widgets
for the notification-area icon and its menu. The header and static startup screen
use Qt's SVG image plugin. Build Qt and the application with compatible compiler/runtime versions.

Select the compiler supplied for your Qt kit. Check its `mkspecs/qconfig.pri` metadata rather
than choosing a compiler from an unrelated Qt installation. In the PowerShell examples, set
`QT_PREFIX` to the selected shared Qt installation and `QT_COMPILER_BIN` to its matching MinGW
compiler directory. Put CMake and Ninja on PATH. These are installed SDK/tool paths, not
additional source repositories.

Changing compiler, Qt prefix, generator or static/shared linkage requires a fresh build directory.
Moving a configured build does not update its cached absolute paths.

## Qt Creator and normal Windows builds

1. Open [CMakeLists.txt](../app/CMakeLists.txt).
2. Select the matching desktop Qt kit and compiler.
3. Set a fresh build directory, for example `build/desktop` under this repository root.
4. Set `LEM_WINDOWS_STANDALONE=OFF`. It is the default for a new cache; an old cache can retain
   `ON`.
5. Configure, build and run. For a shared build, Qt DLLs/plugins must be available at runtime.

Equivalent commands in PowerShell, from the App repository root:

```powershell
$qtPrefix = $env:QT_PREFIX
$compilerBin = $env:QT_COMPILER_BIN
if (-not $qtPrefix -or -not $compilerBin) { throw "Set QT_PREFIX and QT_COMPILER_BIN for your installed Qt kit." }
$env:PATH = "$compilerBin;$qtPrefix/bin;$env:PATH"
cmake -S app -B build/desktop -G Ninja `
  "-DCMAKE_PREFIX_PATH=$qtPrefix" `
  "-DCMAKE_CXX_COMPILER=$compilerBin/g++.exe" `
  -DCMAKE_BUILD_TYPE=Release -DLEM_WINDOWS_STANDALONE=OFF
cmake --build build/desktop --target Lem --parallel 4
```

Run through Qt Creator or a Qt-enabled shell. To prepare an installed shared build, use
`cmake --install build/desktop --prefix build/stage`; the project installs its Qt QML deployment script
on Windows/macOS. Verify that staging directory on the destination machine. A shared `Lem.exe` alone
is not a portable package.

## Single-file Windows packaging

[build-standalone.ps1](../tools/build-standalone.ps1) finds the installed tools, builds Lem,
audits PE imports and runs an isolated startup/TLS/JPEG test. It explicitly enables
`LEM_WINDOWS_STANDALONE`. Use PowerShell from the App repository root.

```powershell
& ./tools/build-standalone.ps1 -Plan   # inspect detected inputs and output paths
& ./tools/build-standalone.ps1         # build and verify
```

The default output is **`build/windows-static/portable/Lem.exe`**, relative to the App repository,
regardless of the terminal's current directory. All generated Qt libraries, Qt/app compilation
files, logs and reports are inside `build/windows-static`. Installed Qt directories are read-only.

The helper first reuses its local static Qt installation, then looks for an existing static x64
Qt 6.12+ installation. If none is suitable, it uses the installed Qt Sources component and builds
static Qt under the app build directory. The usual shared installer kit cannot be linked into a
single EXE, but its compiler metadata selects the matching installed MinGW. The first Qt source
build takes substantially longer; subsequent runs reuse it and rebuild changed app files.

Discovery checks `C:\Qt`, `QT_ROOT`, Qt locations on PATH and `QT_PREFIX`/`QT_STATIC_PREFIX`.
CMake and Ninja from Qt Tools are preferred, and Git for Windows can supply Perl. Python is found
from installed interpreters, `LEM_PYTHON` or the Python launcher; Windows Store aliases are skipped.
The tool does not download or install dependencies. Missing inputs produce an actionable error.

| Parameter | Meaning |
| --- | --- |
| `-Plan` | Print detected inputs/output paths without writing files or compiling |
| `-QtRoot` | Search a nonstandard installed Qt root instead of the default locations |
| `-QtPrefix` | Reuse an existing static kit; a new installation must be inside `BuildDirectory` |
| `-BuildQt` | Explicitly rebuild the app-local static Qt; normally detected automatically |
| `-QtSource` | Override the installed Qt Sources directory |
| `-QtBuild` | Override the Qt compilation subdirectory within `BuildDirectory` |
| `-Compiler MinGW` or `MSVC` | Native toolchain; MSVC requires an x64 Visual Studio developer shell |
| `-ToolchainBin` | Override the matching compiler's `bin` directory; its version/architecture are checked |
| `-BuildDirectory` | Output root, default `build/windows-static` in the App repository. Use a path outside any installed Qt root. |
| `-Jobs` | Parallel jobs, 1–128; default 4 |

Qt source/build/install paths must be space-free. The Qt build uses Schannel TLS and requires Windows,
offscreen, JPEG and SVG plugins. For a local cache created before Qt SVG was added, select a fresh
build directory. Configuration markers reject incompatible compiler, Qt and source caches. Rerun
an interrupted build to resume with the same configuration. External static kits are read-only.
Only one build can use an output directory at a time. In a deeply nested checkout, a new build uses
a temporary Windows drive for shorter paths. It records the letter in the output directory so it
can reopen the same CMake cache. If that drive is occupied, wait until it is free or select a fresh
`-BuildDirectory` under `build/`.
The helper shortens long generated Ninja object/dependency paths inside `.lem-obj/` and `.lem-deps/`.
MinGW can write a long object path but its archiver/dependency writer can still fail on that path.
The helper runs CMake explicitly before shortening compile and archive paths, then starts Ninja
with that manifest. Before installation, it copies objects back to their original paths so Qt's
installation and export files resolve them.
Qt's generated plugin `moc` files use short `.ag/` directories as well. A local CMake include sets
`AUTOGEN_BUILD_DIR` when long plugin targets are created, before Qt calculates its metatype paths;
normal library targets retain their original paths. No installed Qt source files are patched.

Outputs include `Lem.exe`, `portable/Lem.exe`, `dll-imports.json`, `build-result.json` after success,
and an appended `build.log` with full configure/compiler output. The portable smoke directory must
contain only the executable. Qt generated files are under `qt-build/` and `qt/`. These outputs are
ignored by Git. This replaces the previous `app/windows-static-transcript.log` location.

[verify-pe.py](../app/windows/verify-pe.py) can audit an existing executable without building
anything:

```powershell
python app/windows/verify-pe.py build/windows-static/Lem.exe --report build/windows-static/dll-imports.json
```

It reads normal and delay-loaded PE imports, rejects DLL names outside its explicit Windows-system
allowlist and optionally writes JSON. It uses only Python's standard library. It does not detect
arbitrary `LoadLibrary` calls or replace the isolated runtime test.

## Linux and macOS

Use the platform's Qt 6.12+ kit. From the App repository root, with `QT_PREFIX` set to that kit:

```bash
cmake -S app -B build/desktop -G Ninja \
  -DCMAKE_PREFIX_PATH="$QT_PREFIX" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/desktop --parallel 4
cmake --install build/desktop --prefix "$PWD/build/stage"
```

The Linux install adds the desktop entry and icon; distribute the required runtime libraries
separately. A macOS application bundle is configured by CMake. These commands describe supported
project configuration, not a claim that every platform has been validated on this computer.

For persistent sign-in on Linux, install `secret-tool` (for example, `libsecret-tools` on Ubuntu)
and run the app in a desktop session with an available Secret Service keyring. When the
credential store is unavailable, users can still sign in for the current run.

## Android

### Shell build and USB deployment (Windows / Ubuntu)

The two commands are `build` and `deploy`. Run them from the App repository root.
Windows PowerShell:

```powershell
.\tools\android.ps1 build
.\tools\android.ps1 deploy
```

Ubuntu 22.04+ on x86_64:

```bash
bash tools/android.sh build
bash tools/android.sh deploy
```

The launchers share [android.py](../tools/android.py). They find Python 3.10+ or install
it with Windows `winget` / Ubuntu `apt`. Windows needs Microsoft App Installer (`winget`)
for automatic Python/JDK/Git installation. If script execution is disabled in PowerShell,
invoke `powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\android.ps1 build`
for that process. `LEM_PYTHON` can point to an existing Python executable.

`build` produces a **debug APK**, defaulting to `arm64-v8a`, using Qt **6.12.0**, Java **21**,
NDK **27.2.12479018**, Android platform **37.0** and build tools **37.0.0**. It reuses matching
installed tools. Missing SDK packages and Android OpenSSL 3 libraries are installed automatically.
Missing CMake/Ninja are installed in an isolated tooling environment; Gradle is supplied by Qt's
wrapper and downloads its own distribution/dependencies on the first build.

If a matching Qt host or Android kit is absent, the script builds the needed kit from the official
Qt source tag, including Qt Base, Shader Tools, Declarative (Quick/Controls/Dialogs), SVG and Image
Formats. It builds the host tools before the Android libraries, using MinGW on Windows. The 6.12.0
source build applies the four pinned Qt Base patches listed in `QT_PATCHES` in the driver, including
CVE-2026-116337. Existing Qt installations are reused as supplied; their patch status is not changed
or certified by the script. Partially installed, unmanaged kits are never overwritten.

| Dependency | Default location |
| --- | --- |
| Windows Qt | `C:\Qt\6.12.0\mingw_64` and `C:\Qt\6.12.0\android_arm64_v8a` |
| Ubuntu Qt | `~/Qt/6.12.0/gcc_64` and `~/Qt/6.12.0/android_arm64_v8a` |
| Windows Android SDK/NDK | `%LOCALAPPDATA%\Android\Sdk`, with NDK under `ndk/27.2.12479018` |
| Ubuntu Android SDK/NDK | `~/Android/Sdk`, with NDK under `ndk/27.2.12479018` |
| Java | Windows Temurin under `Program Files/Eclipse Adoptium`; Ubuntu package under `/usr/lib/jvm` |
| Additional CMake/Ninja and Qt source/build cache | `<qt-root>/.lem-android` |
| APK and success receipt | `build/android-arm64-v8a/`, inside the App repository |

The first Qt source build can take hours and tens of GB. Ubuntu may prompt for `sudo` to install
compiler/system development packages. Run the script as your normal user; it invokes `sudo` only
for package installation. On Windows, use short, space-free Qt source/build/install paths
(the default `C:\Qt\.lem-android` cache avoids long object-file paths). A different
ABI, compiler or Qt kit needs a separate build directory. The script detects incompatible reuse
and concurrent commands instead of deleting an existing build.

Useful options (same spelling on both platforms):

| Option | Purpose |
| --- | --- |
| `--jobs 4` | Limit native build parallelism; default at most 4 |
| `--no-bootstrap` | Require installed dependencies; do not download/install missing tools |
| `--inherit-windows-path` | In WSL, retain inherited Windows-drive PATH entries for custom tool setups |
| `--accept-licenses` | Accept Android SDK and Qt open-source licenses noninteractively; otherwise use their prompts |
| `--qt-root PATH`, `--qt-version 6.12.x` | Select installation root/version; this dependency matrix covers Qt 6.12.x |
| `--qt-host PATH`, `--qt-android PATH` | Select existing matching desktop/Android kits |
| `--sdk PATH`, `--java-home PATH` | Override SDK and JDK discovery |
| `--compiler-bin PATH`, `--openssl-root PATH` | Override the matching MinGW compiler or Android OpenSSL installation |
| `--build-dir PATH`, `--cache-dir PATH` | Relocate app outputs or dependency/source-build cache |
| `--abi arm64-v8a` | Also accepts `armeabi-v7a`, `x86_64`, `x86`; requires/builds that Qt kit |
| `--serial SERIAL` | Select a particular device for `deploy` |
| `--launch` | Open Lem after `deploy` |
| `--apk PATH` | Explicitly deploy a particular APK instead of the successful build receipt |

For USB deployment, enable Developer options and USB debugging on the phone, connect a data-capable
USB cable, unlock it and accept its authorization prompt. On Windows, the phone may require its
manufacturer's USB driver. On Ubuntu, `adb` needs permission to access the USB device (configure the
distribution's Android udev rules if it reports insufficient permissions). Multiple devices require
`--serial`; offline and unauthorized devices are rejected.

`deploy` uses **`adb install -r`**, retaining existing Lem data. It does not uninstall, clear data,
force a version downgrade or rebuild automatically. If using a custom build directory/ABI, pass
the same options to both commands. Deployment checks the APK against the successful build receipt;
a failed subsequent build invalidates that receipt. Signing/version conflicts stop deployment.
Debug signing keys are machine-specific: use the same key as the installed app when updating it
from another computer; do not uninstall to work around a mismatch while reports are pending.

In WSL, build on the Linux filesystem for better I/O performance. WSL does not automatically expose
Windows USB devices: attach the phone using USB passthrough, or run the Windows `deploy --apk PATH`
command against the APK produced in Ubuntu. A Windows and Linux build must use separate SDK/Qt kits
and separate build directories. The Linux subprocess environment excludes inherited `/mnt/<drive>`
PATH entries in WSL to avoid slow Windows filesystem searches during CMake configuration; your shell
environment is unchanged. Explicit Qt/SDK/JDK paths are honored. Use `--inherit-windows-path` if your
custom Linux build tools are on a Windows-mounted drive. Installed Ubuntu system packages are checked
before invoking `apt`, so retries do not require `sudo` when those packages are already present.

Upstream references: [Qt command-line APK builds](https://doc.qt.io/qt-6/android-building-projects-from-commandline.html),
[building Qt for Android from source](https://doc.qt.io/qt-6/android-building.html),
[Qt Android prerequisites](https://doc.qt.io/qt-6/android-configure-dev-environment.html),
[Android SDK Manager](https://developer.android.com/tools/sdkmanager), and
[Android OpenSSL libraries](https://github.com/KDAB/android_openssl).

Tooling regression checks (no SDK, network or connected phone required):

```bash
python -m unittest discover -s tools/tests -v
```

### Qt Creator and device checks

Use an Android Qt kit, compatible SDK/NDK/JDK and the desired ABIs in Qt Creator. The project
currently declares package ID `pt.lem.app`, version name `0.7.0`, and version code **190**.
Increase the version code for a future Android release. Changing a package ID creates a different
installation; synchronize outstanding reports before switching or uninstalling.

HTTPS requires Android **OpenSSL 3** binaries for every selected ABI.
[AndroidOpenSSL.cmake](../app/cmake/AndroidOpenSSL.cmake) discovers standard Qt SDK locations or
accepts `LEM_ANDROID_OPENSSL_ROOT` as a CMake PATH/environment variable. It expects `libcrypto_3.so`
and `libssl_3.so` and validates ELF architecture headers. Desktop libraries, `/system/lib`,
wrong-ABI files and unsuffixed older library names are rejected. See the candidate paths in the
CMake error when discovery fails.

The Android package contains Java camera/video handling, FileProvider configuration and backup
exclusions. Check camera return, gallery/content URIs, orientation, system-bar margins, TLS,
signature capture and offline recovery on a real device. Offscreen desktop tests do not establish
Android correctness.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| `A single Lem.exe requires STATIC Qt` | Disable the option for a normal shared kit, or use the static packaging route |
| Compiler/runtime DLL errors | Match the compiler to the Qt kit and avoid older MinGW DLLs earlier in the executable search path |
| Icon compilation fails in a path with spaces | Reconfigure the current source; MinGW icon compilation is isolated from inherited Qt include paths |
| Missing `build.ninja` | Configuration did not finish; read the preceding error and use a fresh compatible build tree |
| Missing Python on Windows | Put an actual interpreter on PATH; a Windows Store execution alias is not an installed interpreter |
| Android HTTPS configuration fails | Supply the suffixed OpenSSL 3 pair for every ABI |
| Qt patch advisory during configure | CMake emits a version-based diagnostic below 6.12.1; check the vendor's patch status for the actual SDK. Building Lem does not patch Qt |

Release validation and current test commands are in [Testing](testing.md). Keep SDK advisories and
dependency checks distinct from evidence that a particular application build was tested.

## Revision 0.2.5

App/API version `0.2.5` uses Android version code 176 and minimum/latest `0.2.5`.
MAN.14–18 now require photographs for applicable readings and observations for N/A. Upgrade app
and server together; preserve local reports. A rejected pending closure becomes editable with its
answers/evidence retained. Background signing, clockwise photo rotation and observer-cache closure
refresh are covered by native/API/UI tests. Accepted closed history is unchanged.

## Revision 0.2.4

App/API version is `0.2.4`, Android version code 175, minimum compatible client `0.2.2`.
The server fixes first-time contract presentation writes on PostgreSQL by checking an explicit
`RETURNING` result instead of INSERT `rowcount`. This repairs saves from older compatible clients
too. The app verifies saved settings with an independent read, keeps success/error feedback beside
the controls and permits settings writes while independent evidence downloads continue.

Update the server and its latest-client policy, then update the app in place. Save the intended
contract format again: earlier rejected transactions left the default LEM selection in the database.
Existing reports/evidence and administrator/technician ownership remain unchanged. Real PostgreSQL
and SQLite tests cover restart, concurrent administrators and PDF/ZIP output; native/QML tests cover
lost acknowledgements, failures, durable local settings and persistent save feedback.

## Revision 0.2.3

This compatible patch repairs PDF/ZIP exports of reports cached before contract presentation was
introduced. Missing contract IDs are recovered from catalogue data or a read-only report lookup;
an administrator can export a technician's closed report with the current contract issuer/logos
without modifying its owner, answers, evidence or revision. Unknown/invalid lookup responses do
not overwrite an export destination with the old default brand.

App/API version is `0.2.3`, Android version code 174, checklist version 6. Keep the minimum client
at `0.2.2` and promote latest to `0.2.3`: older compatible clients receive an update notice. Update
the server's latest-version policy before updating the app in place. No report/data migration or
reimport is required. The native/API regressions include administrator exports of technician reports,
missing legacy IDs/catalogues, direct PDFs, PDFs inside ZIPs and failed-lookup destination protection.

## Revision 0.2.2

This revision aligns the app and API at 0.2.2, Android version code 173, and checklist version 6.
It includes the revised checklist rules and N/A suggestions, optional general observations in
all groups, required technician signature on closure, 120-second/60-MiB videos and 400-MiB
report evidence totals. Synchronization fixes recover null optional answers, keep the pending
counter current and allow adding evidence while a draft is being synchronized. Evidence downloads
protect actions on their own report while allowing work on other reports. Contracts in retention
suspend their pending reports durably until a fresh contract list confirms restoration.
Desktop administrators can correct closed reports through atomic replacement. Contract branding
is applied from its save acknowledgement and checked before online exports. Evidence downloads
share a bounded server cache and retain independent cancellation/range state.

Both user manuals are editable Markdown/Typst sources with regenerated PDFs for 0.2.0.

Deploy the matching server policy and update the app in place, retaining package ID `pt.lem.app`
and the existing signing key. Default minimum/latest client versions are `0.2.2`/`0.2.2`:
earlier apps do not understand replacement tombstones and cannot safely preserve pending old-copy
changes. They must update before logging in to this server revision.
Follow the [version policy](../../Common/docs/maintenance.md#version-policy) for future changes.
Preserve local reports while updating; do not uninstall or
clear app data to resolve pending synchronization. Release artifacts still require the normal
build, signing and device checks; a source version bump does not publish an APK.

The user creates final commits. Prepare and review each repository separately, then let the
user execute the chosen commit, tag and push commands.
