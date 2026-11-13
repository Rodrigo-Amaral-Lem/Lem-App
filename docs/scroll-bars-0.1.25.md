> Implementation update: 0.1.26 replaces the height-based transition described here with fixed-viewport overlays. See [stable scrolling](stable-scroll-0.1.26.md).

# Android scroll-aware action bars — 0.1.25

Client update only. Recompile/install Lem 0.1.25 (Android version code 125).
The package remains `pt.lem.app`; no server update or database reset is needed.

## Behavior

On Android, advancing downward through a page smoothly collapses page navigation
and the bottom action row. Beginning to scroll upward reveals them again. At the
start of a page the controls are always shown. The main header containing Lem,
back, options/navigation and account stays visible at its normal height throughout.

| Page | Secondary controls that collapse |
| --- | --- |
| Report | Report heading/save summary, question-group tabs/group selector, bottom Sinc./review/status row. |
| Charger/report selection | Contract selector/refresh row, catalogue heading/search, bottom action row. |
| Management | Contract selector/refresh row and bottom action row. Management forms/actions already scroll with the page. |

Question controls (answers, notes, camera/gallery and photo actions) and charger-card
report buttons remain attached to their content and scroll normally. They are not
individually hidden while on screen. Modals keep their own scrolling; their gestures
do not operate the underlying page bars. Non-Android platforms keep fixed controls.

Changing page, report or question group reveals the secondary controls. Restored
group positions do not count as a deliberate swipe. Inline form values, drafts,
report ownership, photo queues and synchronization timing are unchanged.

## Implementation notes

`Main.qml` owns a single `chromeProgress` fraction for opacity, panel height and
layout gaps. `ScrollChrome` measures its natural child layout independently of the
animated outer height; this avoids feeding animation dimensions back into content
measurement. The footer uses the same fraction, while the main header is excluded.
Clipping and disabled input prevent collapsed controls from intercepting taps.

The active scroll source is the report ScrollView's Flickable, charger ListView,
or management ScrollView's Flickable. `Connections` watches that source rather
than dialog scroll views. Only actual `moving` states accumulate direction; plain
contentY assignments and viewport/content-size updates reset the measurement.
Offsets are clamped to the valid range so edge overshoot/rebound does not imitate
an intentional upward gesture.

The current direction thresholds are 18 logical px downward and 8 upward. Height
and opacity transition together over 200 ms with OutCubic easing. Inertia/layout
movement during the transition is ignored; an active finger can reverse its
animation smoothly. These values are presentation choices and do not affect network
or storage operations. The Android behavior can be overridden by the QML mock tests
without pretending the desktop runtime is an actual Android device.

The import helper text also uses the generic Portuguese wording “atributos” rather
than an accidental product-name plural left by an earlier branding replacement.

## Validation

Run from the extracted project root:

```bash
python app/tests/test_scroll_chrome.py
python app/tests/test_report_tabs.py report mobile
python app/tests/test_report_tabs.py report
```

The scroll suite uses an isolated QML mock with synthetic touch events and
Flickable velocity requests. It covers all three pages, partial animation states,
reversal before reaching the top, reversal during an active animation, fixed main
header/account access, short catalogues, bottom-edge rebound, programmatic scroll
positions, restored group positions and desktop opt-out. The regular UI suites
cover editing, notes, photos, menus, review, ownership/conflict actions and compact
bar behavior. These tests do not access real accounts or the live API.

The validation runtime is Qt/PySide6 6.10.2 with offscreen rendering. A complete
Qt 6.12 Android APK build and physical-device check remain separate validation
steps. In particular, verify gesture feel, keyboard appearance and safe-area/system
bars on the intended phone. Android's own status/navigation bars are not resized.
