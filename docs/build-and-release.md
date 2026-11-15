# Build and release the client

This guide describes the current `0.2.0` source. Run all commands from the **App repository
root**, which contains `app/`, `shared/` and `docs/`. Build and staging examples use ignored
subdirectories of `build/`; no sibling working directory is required.

## Prerequisites and kit selection

The project requires CMake **3.25+**, C++17 and Qt **6.12+**, with Core, Gui, Qml, Quick,
QuickControls2, QuickDialogs2, Network and Concurrent. Build Qt and the application with compatible
compiler/runtime versions.

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

[build-standalone.ps1](../app/windows/build-standalone.ps1) configures/builds Lem, audits PE imports
and runs an isolated smoke test. It explicitly enables `LEM_WINDOWS_STANDALONE`. Its required input
is a **static** Qt prefix, not the normal installer kit.

```powershell
$staticQt = $env:QT_STATIC_PREFIX
if (-not $staticQt -or -not $env:QT_COMPILER_BIN) { throw "Set QT_STATIC_PREFIX and QT_COMPILER_BIN for your static Qt kit." }
& ./app/windows/build-standalone.ps1 `
  -QtPrefix $staticQt `
  -ToolchainBin $env:QT_COMPILER_BIN `
  -BuildDirectory "$PWD/build/standalone" -Jobs 4
```

| Parameter | Meaning |
| --- | --- |
| `-QtPrefix` | Required static Qt installation; the script installs here when `-BuildQt` is used |
| `-BuildQt` | Build/install Qt before building Lem; this can take substantial time and disk space |
| `-QtSource`, `-QtBuild` | Qt source and build trees; use separate paths without spaces when building Qt |
| `-Compiler MinGW` or `MSVC` | Native toolchain; MSVC requires an x64 Visual Studio developer shell |
| `-ToolchainBin` | Optional directory prepended to PATH during the script |
| `-BuildDirectory` | Lem output directory; set explicitly to `build/standalone` in the example |
| `-Jobs` | Parallel jobs, 1–128; default 4 |

For a first static Qt build, add
`-BuildQt -QtSource $env:QT_SOURCE -QtBuild $env:QT_BUILD_DIR`. Set these variables to your
Qt source and a separate Qt build directory. Qt source/build/install paths must be space-free;
this SDK constraint does not require running the command outside the App repository.

The script requires CMake, Ninja, Python and the chosen compiler on PATH. It builds Qt with Schannel
rather than OpenSSL and requires Windows, offscreen, JPEG and TLS plugins. A configuration marker
prevents resuming an incompatible Qt build.

Outputs include `Lem.exe`, `dll-imports.json`, and `portable/Lem.exe` under the selected build
directory. The portable smoke directory must contain only the executable. The script also writes
`app/windows-static-transcript.log` inside this repository. That diagnostic log is ignored by
Git; do not force-add it.

[verify-pe.py](../app/windows/verify-pe.py) can audit an existing executable without building
anything:

```powershell
python app/windows/verify-pe.py build/standalone/Lem.exe --report build/standalone/dll-imports.json
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

## Android

Use an Android Qt kit, compatible SDK/NDK/JDK and the desired ABIs in Qt Creator. The project
currently declares package ID `pt.lem.app`, version name `0.2.0`, and version code **171**.
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

## Release 0.2.0

This release aligns the app and API at 0.2.0, Android version code 171, and checklist version 6.
It includes the revised checklist rules and N/A suggestions, optional general observations in
all groups, required technician signature on closure, 120-second/60-MiB videos and 400-MiB
report evidence totals. Synchronization fixes recover null optional answers, keep the pending
counter current and allow adding evidence while a draft is being synchronized.

Both user manuals are editable Markdown/Typst sources with regenerated PDFs for 0.2.0.

Deploy the matching server policy and update the app in place, retaining package ID `pt.lem.app`
and the existing signing key. The default minimum/latest client versions are both 0.2.0, so
older clients will need the update. Preserve local reports while updating; do not uninstall or
clear app data to resolve pending synchronization. Release artifacts still require the normal
build, signing and device checks; a source version bump does not publish an APK.

The user creates final commits. Prepare and review each repository separately, then let the
user execute the chosen commit, tag and push commands.
