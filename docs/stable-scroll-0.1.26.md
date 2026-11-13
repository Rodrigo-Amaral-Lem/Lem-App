# Stable scroll geometry and compact answer actions — 0.1.26

Client update only. Recompile/install Lem 0.1.26 (Android version code 126).
No server update, database reset or synchronization migration is required.

## Response actions

Status questions now have a single row containing **OK**, **NOK**, **N/A** and a
geometric eraser icon. The icon has the accessible name/tooltip **Limpar resposta**,
including the checkpoint ID for assistive technology. Its width is reserved even
when there is no response, in which case it is disabled. Selecting an answer no
longer inserts another button row or changes the other buttons' widths.

Clearing removes only the status value. Existing notes and photographic evidence
remain associated with the question, as before. Editing/closed-report/ownership
and busy guards continue to apply. Narrow-phone tests verify all four actions share
a row and retain at least 48 px touch targets at 320 px viewport width.

## Why the previous animation drifted

Version 0.1.25 animated the heights of secondary navigation panels and the window
footer. Their parent layout then changed the reading viewport on every animation
frame. This shifted questions independently of the user's gesture and changed the
scrollbar ratio/track while the controls were appearing or disappearing.

The corrected layout keeps the page viewport fixed. Top controls and the bottom
bar are positioned as overlays and move with `Translate` plus opacity. They do
not contribute a changing height to the page layout. The main Lem header remains
fixed and outside these transitions.

Each scrollable page has permanent top/bottom content insets. At the beginning,
the first content sits below the top overlays; at the end, the last question/card/
management action can be scrolled clear of the footer. These insets do not depend
on animation progress, so content height and scroll position stay unchanged when
only chrome visibility changes. On an upward scroll, the controls may cover the
content briefly while sliding into view; they do not drag that content with them.

The Android page-position indicator uses a stable track and the active scroll
view's visible-area ratios. Its geometry is independent of the overlay animation.
Genuine content changes, a different question group, keyboard appearance or window
resizing may still legitimately change scroll geometry.

Report, catalogue and management pages retain the direction-based behavior:
downward scrolling hides secondary navigation, upward scrolling reveals it, and
the beginning always reveals it. Finger/inertia reversals no longer need to be
suppressed to avoid animation-driven viewport updates. Dialog scrolling and plain
programmatic positioning remain excluded from gesture detection. Desktop controls
remain visible.

## Implementation map

- `ScrollChrome` measures its natural layout once per real content/width change;
  animated translation/opacity leave that size untouched.
- `ChromeBackdrop` prevents content behind the overlay from obscuring its controls.
- `appFooter` is a positioned ToolBar, not the geometry-reserving window footer.
- Report/admin column spacers and catalogue ListView header/footer items reserve
  content reachability without resizing the viewport.
- `pagePositionBar` has fixed track insets; catalogue beginning calculations include
  the inline ListView header before the first delegate.
- The question-group strip re-reveals its selected tab after width changes so its
  initial overlay layout cannot leave that tab out of view.

## Verification

The QML suites run in isolated offscreen Qt/PySide6 6.10.2, without live accounts:

```bash
python app/tests/test_report_tabs.py report mobile
python app/tests/test_report_tabs.py report
python app/tests/test_scroll_chrome.py
```

Checks include response-row geometry, notes/photo retention after clearing, physical
pointer access to the overlaid status action, simulated touch swipes/reversal,
all three pages, top/bottom behavior and desktop opt-out. During repeated show/hide
animations, each sampled frame must preserve viewport height, content height,
content offset, viewport screen position, scrollbar size/track/actual thumb height,
and (on the report) an actual question's screen position.

Final Qt 6.12 APK compilation and physical Android gesture validation remain separate
steps. Backend sources, deployment scripts and the shared checklist are unchanged.
