# Build and release the client

This guide describes the current `0.1.57` source. Run local commands from the sibling `ongoing_work`
directory. Paths in examples assume `App`, `Server`, `Common` and `ongoing_work` share the same
parent.

## Prerequisites and kit selection

The project requires CMake **3.25+**, C++17 and Qt **6.12+**, with Core, Gui, Qml, Quick,
QuickControls2, QuickDialogs2, Network and Concurrent. Build Qt and the application with compatible
compiler/runtime versions.

The installed Windows kit at `C:/Qt/6.12.0/mingw_64` identifies GCC **15.1** in its
`mkspecs/qconfig.pri`. Use `C:/Qt/Tools/mingw1510_64/bin/g++.exe`, not MinGW 13.1, for that kit.
Other installations must be checked against their own metadata.

Changing compiler, Qt prefix, generator or static/shared linkage requires a fresh build directory.
Moving a configured build does not update its cached absolute paths.

## Qt Creator and normal Windows builds

1. Open [CMakeLists.txt](../app/CMakeLists.txt).
2. Select the matching desktop Qt kit and compiler.
3. Set the build directory to `ongoing_work/app-build`, outside the Git repository.
4. Set `LEM_WINDOWS_STANDALONE=OFF`. It is the default for a new cache; an old cache can retain
   `ON`.
5. Configure, build and run. For a shared build, Qt DLLs/plugins must be available at runtime.

Equivalent commands in PowerShell, from `ongoing_work`:

```powershell
$env:PATH = "C:/Qt/Tools/mingw1510_64/bin;C:/Qt/Tools/Ninja;C:/Qt/Tools/CMake_64/bin;C:/Qt/6.12.0/mingw_64/bin;$env:PATH"
cmake -S ../App/app -B app-build -G Ninja `
  -DCMAKE_PREFIX_PATH=C:/Qt/6.12.0/mingw_64 `
  -DCMAKE_CXX_COMPILER=C:/Qt/Tools/mingw1510_64/bin/g++.exe `
  -DCMAKE_BUILD_TYPE=Release -DLEM_WINDOWS_STANDALONE=OFF
cmake --build app-build --target Lem --parallel 4
```

Run through Qt Creator or a Qt-enabled shell. To prepare an installed shared build, use
`cmake --install app-build --prefix app-stage`; the project installs its Qt QML deployment script
on Windows/macOS. Verify that staging directory on the destination machine. A shared `Lem.exe` alone
is not a portable package.

## Single-file Windows packaging

[build-standalone.ps1](../app/windows/build-standalone.ps1) configures/builds Lem, audits PE imports
and runs an isolated smoke test. It explicitly enables `LEM_WINDOWS_STANDALONE`. Its required input
is a **static** Qt prefix, not the normal installer kit.

```powershell
& ../App/app/windows/build-standalone.ps1 `
  -QtPrefix C:/Qt/6.12.0-static-mingw `
  -ToolchainBin C:/Qt/Tools/mingw1510_64/bin `
  -BuildDirectory "$PWD/app-static-build" -Jobs 4
```

| Parameter | Meaning |
| --- | --- |
| `-QtPrefix` | Required static Qt installation; the script installs here when `-BuildQt` is used |
| `-BuildQt` | Build/install Qt before building Lem; this can take substantial time and disk space |
| `-QtSource`, `-QtBuild` | Qt source and build trees; use separate paths without spaces when building Qt |
| `-Compiler MinGW` or `MSVC` | Native toolchain; MSVC requires an x64 Visual Studio developer shell |
| `-ToolchainBin` | Optional directory prepended to PATH during the script |
| `-BuildDirectory` | Lem output directory; specify it explicitly to keep outputs in `ongoing_work` |
| `-Jobs` | Parallel jobs, 1–128; default 4 |

For a first static Qt build, add
`-BuildQt -QtSource C:/Qt/6.12.0/Src -QtBuild C:/Qt/build-lem-static`. These external Qt SDK paths
are deliberate: the Qt build helper rejects spaces in source/build/install paths. They are not
application source directories.

The script requires CMake, Ninja, Python and the chosen compiler on PATH. It builds Qt with Schannel
rather than OpenSSL and requires Windows, offscreen, JPEG and TLS plugins. A configuration marker
prevents resuming an incompatible Qt build.

Outputs include `Lem.exe`, `dll-imports.json`, and `portable/Lem.exe` under the selected build
directory. The portable smoke directory must contain only the executable. The script also writes
`app/windows-static-transcript.log` inside this repository; move that diagnostic log into
`ongoing_work` after the run and do not stage it.

[verify-pe.py](../app/windows/verify-pe.py) can audit an existing executable without building
anything:

```powershell
python ../App/app/windows/verify-pe.py app-static-build/Lem.exe --report dll-imports.json
```

It reads normal and delay-loaded PE imports, rejects DLL names outside its explicit Windows-system
allowlist and optionally writes JSON. It uses only Python's standard library. It does not detect
arbitrary `LoadLibrary` calls or replace the isolated runtime test.

## Linux and macOS

Use the platform's Qt 6.12+ kit. From `ongoing_work`:

```bash
cmake -S ../App/app -B app-build-linux -G Ninja \
  -DCMAKE_PREFIX_PATH=/path/to/Qt/6.12.0/gcc_64 \
  -DCMAKE_BUILD_TYPE=Release
cmake --build app-build-linux --parallel 4
cmake --install app-build-linux --prefix "$PWD/app-stage-linux"
```

The Linux install adds the desktop entry and icon; distribute the required runtime libraries
separately. A macOS application bundle is configured by CMake. These commands describe supported
project configuration, not a claim that every platform has been validated on this computer.

## Android

Use an Android Qt kit, compatible SDK/NDK/JDK and the desired ABIs in Qt Creator. The project
currently declares package ID `pt.lem.app`, version name `0.1.57`, and version code **170**.
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
