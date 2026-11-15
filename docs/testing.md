# Test the client

Run commands from the **App repository root**. Keep build/test outputs in ignored `build/`
subdirectories and Python environments in `.venv/`. Use fictitious data and disposable storage.

## Native C++ tests

Use the [build guide](build-and-release.md) to select the matching Qt/compiler kit and set
`QT_PREFIX` and `QT_COMPILER_BIN`. From the same Qt-enabled shell, configure a fresh build:

```powershell
cmake -S app -B build/tests -G Ninja `
  "-DCMAKE_PREFIX_PATH=$env:QT_PREFIX" `
  "-DCMAKE_CXX_COMPILER=$env:QT_COMPILER_BIN/g++.exe" `
  -DLEM_WINDOWS_STANDALONE=OFF -DLEM_BUILD_TESTS=ON
cmake --build build/tests --parallel 4
$env:QT_QPA_PLATFORM = 'offscreen'
$env:QT_QUICK_BACKEND = 'software'
ctest --test-dir build/tests --output-on-failure
```

CMake always registers `qml-startup`; the additional targets require `LEM_BUILD_TESTS=ON`.
Compiler/runtime DLLs and Qt plugins must match the kit. If Windows offscreen rendering cannot
locate fonts, point `QT_QPA_FONTDIR` to the system font directory for that test shell.

| Test family | Main checks |
| --- | --- |
| `qml-startup` | Real QML startup and seven-group model, using temporary demo storage |
| `client-persistence`, `storage-migration`, `offline-persistence` | Journals, restart, private paths and acknowledgement/conflict protection |
| `local-pdf`, `report-groups` | PDF output, drawing/photo signature references, copy isolation and restart |
| `server-settings`, `catalogue-notifications`, `report-projection-performance` | Preference migration, notification scope and projection behavior |
| `network-security`, `evidence-crop-video` | Redirect/reply bounds, paths, cropping and media validation |
| `connector-checklist`, `measurement-rules` | Shared connector and numeric cases |
| `review-regressions`, `station-identity`, `copy-measurements`, `rules-057` | Guards, station corrections, scoped copying and manual-reading rules |
| `data-safety-storage`, `data-safety-signature`, `data-safety-cleanup` | Corrupt/unreadable snapshots and journals, signature reference remapping, and protected evidence eviction |

Test names with historical suffixes identify regressions; they are executable tests, not maintained
release documentation. Inspect failures and skips rather than assuming that an old test's fixture
still represents current required fields.

## QML interaction checks

Install PySide6 in a development environment. These runners select offscreen/software rendering and
use a mock controller. They exercise actual QML controls but do not establish C++ persistence or
network correctness.

```powershell
python app/tests/test_general_notes.py
python app/tests/test_safety_layout.py
python app/tests/test_rules_057_ui.py
python app/tests/test_measurement_inputs.py
python app/tests/test_photo_delegate_stability.py
python app/tests/test_report_groups_054.py
```

The wider `app/tests/test_*.py` collection covers navigation, scroll bounds, Android margins, import
preview, feedback, export visibility and accounts. Most are standalone runners; do not assume
`pytest app/tests` is their entry point. Read each runner's header and argument handling.

## Integration runners and build-helper checks

The two network runners start disposable local APIs. They require the sibling Server repo (or
`LEM_SERVER_REPO` pointing to its root), server dependencies, httpx/openpyxl and the specified
native executable:

```powershell
python app/tests/test_offline_sync.py build/tests/Lem-offline-test.exe
python app/tests/test_client_compatibility.py build/tests/Lem-compatibility-test.exe
```

They need available local sockets/ports. The offline runner uses port 18765. Do not point either
runner at production.

The Android OpenSSL and Windows standalone helper checks run without Android hardware or a static Qt
build:

```powershell
python app/tests/test_android_openssl.py
python app/tests/test_windows_standalone.py
```

Put CMake and a working compiler/generator on PATH for helper tests that configure small projects. A
Windows Store Python alias can fail interpreter discovery. `PWSH` /tool overrides used by individual
tests are described in their source.

## Release checks

Desktop/offscreen success does not verify an APK. On the intended Android device, check camera/video
return, gallery content URIs, orientation/safe areas, keyboard behavior, TLS, foreground/background
persistence and restart with unsent work.

For Windows single-file delivery, use both the PE audit and the script's isolated runtime check,
then test on a machine without Qt installed. For a shared build, test the deployed staging folder
rather than the developer build directory.

Record command transcripts and test reports in an ignored directory such as `build/test-reports/`.
Update the maintained guides when behavior changes; do not turn each test run into a new
version-named document in this repository.
