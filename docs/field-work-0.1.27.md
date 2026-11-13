# Lem 0.1.27 — field navigation and offline PDF

Update the app and server. No database reset or schema migration is required.
The shared checklist supplies new reports; the server PDF renderer also displays
corrected voltage ranges for existing frozen reports.

## Changes

- Android's right-hand position indicator stays visible while the page is scrollable,
  independently of the action bars and scroll direction. The viewport remains fixed.
- Swipe left in report content for the next question group; swipe right for the previous.
  A passive touch handler requires at least 64 logical pixels and a clearly horizontal
  direction. Vertical scrolling, button taps and the group strip retain their behavior.
  Gestures do not switch groups while the keyboard is open. Existing per-group positions
  and deferred photograph synchronization boundaries still apply.
- Opening/creating a draft preserves the catalogue's visible charger and offset when
  returning. The catalogue model stays cached while working in a report; live catalogue
  changes restore the same ID. Changing contract/search starts at the beginning.
- Export PDF uses the current local report snapshot, including unsynchronized answers,
  observations, draft status and all attached photographs. No active login or network
  connection is needed when those data and originals are present in the selected local
  account cache. Explicit logout still clears the selected account's view.
- The PDF generator runs on the worker pool. Text wraps across pages, images retain their
  proportions/orientation and are resized for document output. Original files are untouched.
  Filesystem saves are atomic. Missing evidence is reported before opening the picker
  when there is no authenticated server fallback. PDF bytes are fully generated before the
  destination is written; failures are reported instead of returning an empty PDF.
- A clean report with missing local photos can still use server PDF export while logged in.
  PDF + original photographs ZIP remains a server export requiring a session and synchronization.
- PC02 line-to-line labels now read **360–440 V**; neutral-to-line labels read **207–253 V**.
  Old frozen reports display/export these corrections without rewriting stored answers
  or revisions. These are checklist instructions, not automatic numeric answer validation.

## Code map

`app/src/ReportPdf.h/.cpp`: local validation, pagination, image decoding, destination write.
`app/src/Checklist.h`: display-only normalization of the six voltage questions.
`Lem::prepareExport`: pre-picker validation; `Lem::exportReport`: worker/server routing.
`Main.qml`: catalogue anchor, passive group swipe and persistent scroll-position bar.

## Validation

- Native `Lem-pdf-test`: tokenless dirty report, pending edits, photograph, long accented
  notes over multiple pages, missing/corrupt evidence and preservation of an existing target.
- `python app/tests/test_field_navigation.py`: real touch events for both directions,
  group boundaries and vertical rejection; persistent indicator; create/return catalogue anchor.
- Existing report tabs, Android chrome animation, offline HTTP integration and server suites.

The development environment uses Qt 6.10.2 offscreen for these tests. Final Qt 6.12
Android compilation, SAF document providers and physical-device gestures need device validation.
