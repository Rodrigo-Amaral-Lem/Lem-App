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
| `download-recovery` | Real loopback interruptions, byte-range resume, old-server fallback, bounded retries, permanent-item batch failure and staged ZIP reception |
| `windows-background` | Windows tray close during an evidence download, hidden catalogue refresh, shortcut activation and explicit exit |
| `presentation-save` | Independent settings write/read-back, failed/lost replies, conflicts, local restart and saving during evidence downloads |
| `contract-export` | Closed-only PDF/ZIP batches, local/server evidence, stale observer drafts, pending closures, duplicate filenames, independent devices, interrupted downloads, scoped cancellation, trash/session guards and password-reset client actions |
| `connector-checklist`, `measurement-rules` | Shared connector and numeric cases |
| `review-regressions`, `station-identity`, `copy-measurements`, `rules-057` | Guards, station corrections, scoped copying and manual-reading rules |
| `data-safety-storage`, `data-safety-signature`, `data-safety-cleanup` | Corrupt/unreadable snapshots and journals, signature reference remapping, and protected evidence eviction |

Test names with historical suffixes identify regressions; they are executable tests, not maintained
release documentation. Inspect failures and skips rather than assuming that an old test's fixture
still represents current required fields.

Build `Lem-migration-projection` and set `LEM_MIGRATION_BINARY` to its executable when running
`Server/server/tests/test_form_evolution.py`. This adapter requires only Qt Core and lets the
server suite compare native/Python JSON for draft, frozen-upload, closed and unknown-version
inputs. It checks visible and lost-acknowledgement payloads, idempotence, unique IDs and complete
connector-prototype inheritance. The native signature/rejected-closure tests also cover historical
signature remapping and removal of the frozen-transfer marker when recovering editable work.

## QML interaction checks

Install PySide6 in a development environment. These runners select offscreen/software rendering and
use a mock controller. They exercise actual QML controls but do not establish C++ persistence or
network correctness.

```powershell
python app/tests/test_general_notes.py
python app/tests/test_safety_layout.py
python app/tests/test_rules_057_ui.py
python app/tests/test_measurement_inputs.py
python app/tests/test_suggested_notes_ui.py
python app/tests/test_photo_delegate_stability.py
python app/tests/test_report_groups_054.py
python -X utf8 -B app/tests/test_checkpoint_save_ui.py
python -X utf8 -B app/tests/test_pcve_catalogue_ui.py
```

`test_checkpoint_save_ui.py` exercises failed draft creation, visible save errors, exact
retention of correction-date inputs, retries and first-attempt success in `Main.qml`.
For an isolated run, set `TMP` and `TEMP` to the task's scratch directory before invoking
the runner. It disables the QML disk cache and prints results to the console.

The wider `app/tests/test_*.py` collection covers navigation, scroll bounds, Android margins, import
preview, feedback, export visibility and accounts. Most are standalone runners; do not assume
`pytest app/tests` is their entry point. Read each runner's header and argument handling.

`test_pcve_catalogue_ui.py` covers the contract-independent lookup, live details, offline navigation
and the desktop administrator's edit and Excel controls. The `station-identity` native test checks
canonical details across memberships. The Android emulator should also exercise the shared catalogue
on a signed-in account and after restarting without a server connection.

## Integration runners and build-helper checks

The Windows static builder checks installed-tool discovery, shared/static kit selection, compiler
matching, source fallback, read-only SDK paths, interrupted-install recovery and incompatible cache
protection with `python app/tests/test_windows_builder.py`. `LEM_TEST_TEMP` can select its disposable
test directory. `python app/tests/test_windows_standalone.py` also checks the PE DLL allowlist and
CMake static/plugin guards. These checks complement the actual builder's single-EXE smoke test;
`-Plan` only verifies discovery and does not establish that an executable works.
For the standalone guard tests on a MinGW workstation, put MinGW/Ninja on PATH and set
`CMAKE_GENERATOR=Ninja`; otherwise CMake may select NMake and fail before reaching the guard.
With MinGW and Ninja on PATH, the builder tests compile/archive with object and dependency paths
beyond 260 characters, check an unchanged build does no work, then change a header and verify it
recompiles through the shortened paths. They verify the original installation layout is restored,
short plugin autogen paths, cancellation, timeout and build-directory locking.

The two network runners start disposable local APIs. They require the sibling Server repo (or
`LEM_SERVER_REPO` pointing to its root), server dependencies, httpx/openpyxl and the specified
native executable:

```powershell
python app/tests/test_offline_sync.py build/tests/Lem-offline-test.exe
python app/tests/test_client_compatibility.py build/tests/Lem-compatibility-test.exe
```

The compatibility runner derives the client version from CMake and sets `LEM_TEST_CLIENT_VERSION`
for its native harness. Its seven scenarios exercise current login, compatible update notices,
blocked clients, missing/malformed/wrong-version policy replies and a policy change during login.
On Windows it terminates the disposable API process tree before removing logs/databases.

Append `--sessions` to the compatibility runner on a platform with a working credential store.
It starts a fresh client process for each step and checks session restoration, update notices,
recovery after an unavailable API, server revocation, changing the server and explicit logout.
`test_version_notice.py` checks the account dialog while a saved session is pending, including
narrow portrait and short landscape layouts.

They need available local sockets/ports. The offline runner uses port 18765 and also requires
`pypdf` to inspect its generated PDFs. Do not point either runner at production.

The offline runner verifies legacy reports with no contract ID or cached catalogue, exported by
both their technician and an administrator after a LEM-to-CSMTEC change. It reads the direct PDF
and the PDF inside the ZIP, checks preservation of report/evidence/ownership metadata and protects
an existing destination when contract lookup fails. `LEM_BRANDING_TEST_OUTPUT` optionally retains
these synthetic PDFs/ZIPs in a chosen test-output directory for visual inspection.

`presentation-save` uses an ephemeral loopback HTTP server. It checks that a successful HTTP write
cannot produce a saved indicator unless a subsequent read confirms issuer, client logo IDs and
revision. Malformed/stale reads, access denial and genuine conflicts remain visible; lost responses
recover when the read proves the desired state. `test_report_presentation_ui.py` checks persistent
feedback even when the global message becomes “Catálogo atualizado”, saved/unsaved selections,
mobile/desktop widths and session/administrator gates.

The offline runner also deletes/edits a second draft while another report downloads, checks that
callbacks cannot reopen the deleted draft, and exercises contract retention from a second device:
saved/unsent drafts stay pending across restart, other contracts still sync, and recovery resumes
the preserved work. `test_report_download_isolation_ui.py` checks the matching menu/confirmation
guards and retention notice on desktop/mobile layouts. `test_version_notice.py` checks update and
incompatible-version notices; the compatibility runner verifies actual pre-login requests.

`download-recovery` also needs local sockets (an ephemeral loopback port). It verifies an interrupted
6 MiB original without retransmitting its prefix, access denial without retries, and nine-file batch
continuation after one permanent failure. `local-pdf` covers controller ZIP export without a session.
Validate the native ZIP using an independent archive reader and exact evidence comparisons:

```powershell
python app/tests/test_local_bundle.py build/tests/Lem-pdf-parity.exe
```

This runner requires Pillow and pypdf and checks CRCs, Unicode names, photo/video bytes, public JSON,
issuer, and preservation of an existing destination on missing/corrupt evidence or invalid paths.

The Android OpenSSL and Windows standalone helper checks run without Android hardware or a static Qt
build:

```powershell
python app/tests/test_android_openssl.py
python app/tests/test_windows_standalone.py
```

Put CMake and a working compiler/generator on PATH for helper tests that configure small projects. A
Windows Store Python alias can fail interpreter discovery. `PWSH` /tool overrides used by individual
tests are described in their source.

## Mandatory Android graphical checks

For every UI change, build and install the changed APK in an actual Android emulator.
Use isolated demonstration data; open the affected screens, exercise the changed controls and
capture Android screenshots for visual inspection. Check icons, fonts, touch targets, clipping,
safe areas and applicable portrait/landscape or small-screen layouts. Desktop/offscreen checks,
including mobile-sized windows, are useful regressions but do not satisfy this requirement.
Keep the commands, emulator/API/ABI details and reviewed captures under `ongoing_work` and report
any emulator blocker explicitly. This is a user requirement recorded in the workspace AGENTS.md.

Photo-editor checks must show the drawn clockwise arrow, rotate the preview, enable optional crop,
and exercise Repor, Cancelar and Guardar. Check the generic photo-edit icon on the evidence card too.
Use geometric icons instead of Unicode symbols whose glyphs may be absent from Android fonts.

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

## Contract presentation and PDF parity

`test_report_presentation_ui.py` checks issuer/client-logo loading, updates, responsive
widths and admin-only visibility. `test_offline_sync.py` also changes settings between
two users and checks that closed answers/revision and protected local edits survive.

Build `Lem-pdf-parity` with `LEM_BUILD_TESTS=ON`, then run:

```powershell
python app/tests/test_pdf_parity.py build/tests/Lem-pdf-parity.exe build/pdf-parity
```

This requires the sibling Server dependencies and Poppler's `pdftoppm`. Both brands,
two client logos, measurements, evidence and multi-page notes are compared: text and
all drawing coordinates must agree; image comparisons allow negligible font-edge
antialiasing differences. Repeat with `LEM_NATIVE_PDF_PLATFORM=windows` for the Windows
GUI font engine; default is offscreen. Use the same fixtures on Linux with the pinned
PySide6 version. Keep fixture paths local to the test environment.

`test_report_correction_ui.py` covers desktop/admin controls, date editing and confirmation.
The native offline integration creates a correction of another technician's report, retains
server-side evidence and edits dates and charger identity after a later intervention has updated the
station. It retries a cached legacy identity conflict using manual Sync even when an earlier
report in the same batch receives HTTP 423 for another contract. It then restores the
correction's own contract from a second device and recovers a lost commit response with one
manual Sync. It checks the original on disk before confirmation, its removal after the durable
replacement, the restored owner and all three catalogue fields. The checkpoint-failure case keeps
the pending correction eligible for retry. Another device retains its unsent work. The branding
scenario fails the settings writer's follow-up catalogue request, then exports from a different
device without manually refreshing.
Set `LEM_TEST_GDB` to a local GDB executable only for diagnostic runs of this disposable fixture.
The native `station-identity` test also checks that staged corrections do not change cached
catalogues and that committed canonical values reach all cached memberships, including legacy
templates and compatibility with a server that omits `station_update`.

`data-safety-signature` signs while background sync/refresh flags are active and merges an older
acknowledgement without losing the signature. `data-safety-rotation` checks clockwise pixel
orientation, four-turn round trips, combined rotation/cropping at every orientation, no-op saving,
new evidence IDs/deletion tombstones, restart recovery and owner/closed guards.
`data-safety-rejected-closure` checks durable editing recovery after a rejected offline closure.
`data-safety-storage` blocks checkpoint open for new drafts and correction-date changes.
On Windows it also holds a file handle that allows the temporary write and denies atomic
replacement. The cases check truthful failure, rollback, fresh-controller journal replay,
retry, and preservation of reports and evidence after a successful reload.
`data-safety-migration-sync` uses a loopback HTTP fixture to verify that identical answers cannot
skip the draft PUT when the form version or historical-upload marker differs. It also checks that
an already matching form does not cause an unnecessary write.
The HTTP integration replaces a signature during an actual upload and repeats the observer
sequence: cache draft, sync draft, owner closes/syncs, observer syncs and sees closed without clearing
data. QML media/signature tests cover narrow screens, the combined photo editor (optional crop,
rotated selection geometry, reset/cancel and guarded Save) and signing during sync.

`test_contract_admin_ui.py` checks desktop-only batch controls, picker context, progress/cancel and
administrator-only technician reset, including confirmation mismatch, immutable target and clearing
password fields on accept/cancel. It uses a mock backend; the native/API cases exercise real requests.

## Checklist v7 regressions

Shared `maintenance-cases.json` covers trip endpoints and earth-resistance boundaries with exact
arithmetic. Exercise 6-digit integer mA grammar, legacy unit conversion/confirmation, untouched
suggestions versus custom notes, and closed-correction migration without losing photographs.
Server `test_maintenance_v7.py` also covers stale writers, incomplete correction rollback, repeated
commit, staged historical v6 upload and forged OK assessments. Native measurement tests mirror
boundary cases and verify local reload/closure behavior. UI changes require actual Android emulator
screenshots and touch exercises, including portrait, landscape and narrow layouts; offscreen tests
alone do not satisfy the user's instruction. Keep per-run evidence in ongoing_work.

Also cover migration before automatic synchronization of a cached draft that has never been opened,
including its in-flight payload and durable cache. Oversized HTTP 401/426 evidence responses must
reach the failure callback with the authentication status, not the completed-file callback.
