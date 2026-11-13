# 0.1.32 R1 — footer safe-area feedback loop

## Evidence and cause

The AGM Android log identifies `Main.qml:1397` / `:1398`: the footer's
`Translate.y` binding. The same warning is reproducible offscreen with Qt 6.10.2
by adding a 24 px bottom safe-area margin to the stationary window content item
and hiding/revealing the footer. The previous tests had zero system margins,
which masked the failure.

The styled ToolBar defines separate top/bottom/left/right padding bindings to
its own SafeArea. Setting only `padding: 4` does not override these explicit
per-side style bindings. Translating a footer downwards increases its own bottom
safe margin; its bottom padding then increases its height, which increases the
translation again. That height also feeds `pageBottomInset`, adding a large
blank scroll tail. In the reproduction the footer grew from 73 px to over
700 px instead of retaining a constant height.

Qt documents that an item's position must not depend on its own safe area:
https://doc.qt.io/qt-6/qml-qtquick-safearea.html

## Fix

Set all four footer padding properties to 4. ApplicationWindow already keeps
its content item inside the system safe area; this overlay is parented and
anchored to that content item. The footer therefore needs no second, moving
safe-area correction. Its height is `touchHeight + 8`; only its translation and
opacity animate. The document's end inset, viewport and scroll thumb stay
constant during those animations. System status/navigation protection in
ApplicationWindow remains enabled.

No server/API/database changes. The app/protocol version remains 0.1.32,
Android version name is `0.1.32-r1` and installation code is 134. The next Android
installation code must exceed 134. Compile in a fresh build directory and check
**Interface R1** in Account. Existing data need not be deleted.

## Verification

- `python app/tests/test_footer_safe_area.py`: real SafeArea propagation with
  24, 48 and zero bottom margins on reports, charger IDs and Management. Checks
  stable footer height, content extent, viewport, scroll position and scrollbar
  through each animation frame and rejects QML binding warnings.
- This test fails against the D1 interface and passes with the R1 fix.
- Existing scroll direction/touch reversal, natural page-end, report interaction
  and diagnostic clipboard tests also run against R1.

Validation uses Qt 6.10.2 offscreen, not a physical AGM/Qt 6.12 APK. The exact
reported loop and inflated height are reproduced and removed locally; the
remaining device check is to scroll past the final card and reverse direction.
The diagnostic tools remain available if a device-specific symptom persists.
