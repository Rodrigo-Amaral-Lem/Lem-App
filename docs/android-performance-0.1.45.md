# Android-focused client performance — 0.1.45

## Changes

### Isolate active report notifications

`current` previously used the broad `changed` signal, so unrelated status/busy
notifications caused QML bindings to re-read a whole report. Its getter also
converted every JSON field to QVariant on every read, including template,
snapshot, all answers and photograph metadata.

`currentChanged` now emits only when the report JSON actually changes. The
getter caches its QVariantMap and returns implicitly shared copies until the
JSON changes. Lazy validation keeps direct synchronous C++ reads correct even
before an action emits changed(). Only the current report is cached; this is
not another on-disk report copy or a cache of image pixels.

### Index photographs independently of answers

A separate `photosByCheckpoint` projection groups photo metadata once and uses
`photosChanged`, independent of answer typing and operation status. QML helpers
look up the checkpoint directly instead of filtering every photo repeatedly.
The map is rebuilt for actual additions/removals/path changes/download metadata.
The existing photo image-size limits and asynchronous loading are retained.

Qt may already reuse some unchanged delegates with the old model. The benefit
claimed here is fewer notifications, conversions and array scans; it is not a
measured reduction in image decoder invocations. A UI regression confirms that
photo delegates stay valid during editing/status changes and still disappear
on removal.

### Consume camera results on return to the app

On Android, returning to ApplicationActive now checks the camera mailbox
immediately. The existing 1-second poll remains for busy-state/delayed recovery,
so no existing recovery path is removed. CaptureActivity still writes the result
before finishing. This avoids waiting for the next polling tick when the result
is already ready; actual latency on the physical phone has not been measured.

No changes to report durability boundaries, photo upload deferral, ownership,
network concurrency, original photo resolution, cache eviction policy or the
Android window lifecycle. Desktop uses the same efficient data projections.

## Measurements

Native Qt 6.10.2 on the development Linux host, -O1 build, synthetic report with
200 photo metadata entries, 10,000 reads of current(). Three independent process
runs per version; getter warmed once before timing.

| Measure | 0.1.44 | 0.1.45 |
| --- | --- | --- |
| Median time for 10,000 reads | 2051.298165 ms | 0.600559 ms |
| Report notifications from 1,000 unchanged status notifications | 1,000 | 0 |
| Photo-model notifications from those status notifications | 1,000 broad notifications | 0 |

These are isolated data-projection measurements, not Android frame times or an
application-wide speedup factor. The test's photo entries are metadata, not 200
simultaneously decoded originals. Raw samples are in
`performance-0.1.45-measurements.json`.

## Validation

- Native `Lem-report-performance-test`: cached reads, no report/photo notifications
  for unchanged status, answer update visible immediately, photo/path change reflected.
- QML `test_photo_delegate_stability.py`: same delegate after 40 edits and 100
  status updates, followed by correct removal.
- Mobile/desktop QML report workflows, year formatting, scroll limits, desktop
  scrollbar dragging, draft-export visibility and startup variants/timing passed.
- Native connector/PDF, recovered-photo and obsolete/current session rejection tests passed.
- Server/deploy suite: 149 passed, 28 skips requiring original Excel or PowerShell 7.
- Native offline client integration passed against a disposable local API.

Local native and QML tests use Qt/PySide 6.10.2. Compile release targets with
Qt 6.12. Physical Android profiling, camera return timing and Windows/iOS runtime
checks were not performed here.

## Build and install

Build with LEM_BUILD_TESTS=ON to include the native benchmark target. QML tests
require PySide6. Update the server's version policy with the normal deploy script,
then build/install app 0.1.45 (Android code 153). The server's only functional
change in this release is admitting 0.1.45; minimum remains 0.1.39. No database
reset or new on-device image storage is required.
