# Startup screens (0.1.41)

Each fresh app process makes one random draw. Resuming the app from the
background does not trigger another splash. All platforms use the same rules.

| Screen | Probability | Duration |
| --- | --- | --- |
| Lem logo | 89% | 2 seconds |
| Engineer greeting | 5% | 2 seconds |
| Checklist completed | 5% | 2.4 seconds |
| Charging connector | 1% | 2.4 seconds |

These outcomes are mutually exclusive. No logo is shown before or after a
selected animation by the app-owned splash. Transparent PNG frames are embedded
as Qt resources and drawn on the same page background. They require no network,
stop when the splash closes, and do not play in the normal interface. All six
textures are decoded before the frame timer starts. An image loading error or a
3-second loading timeout falls back to the logo instead of blocking access.
Android may still display its operating-system launch screen before Qt starts.

The splash is an in-scene FocusScope overlay, not a Popup or a separate native
window. Closing it only changes its visibility. Ordinary Image items plus a
Timer replace AnimatedImage/QMovie, removing the GIF decoder and plugin from
this path. The frame PNGs contain alpha, so no off-white rectangle remains.

The supplied Android log shows main() returning 0 before a FORTIFY destroyed-
mutex abort on hwuiTask0 during graphics teardown. It does not establish which
interaction initiated the exit, or implicate the GIF decoder conclusively.
This change removes the startup popup and decoder paths as a robustness fix;
physical Android validation is still needed. Lightweight Android lifecycle
messages now record lastWindowClosed/aboutToQuit if an exit recurs.

Implementation: `app/qml/Main.qml`, `app/assets/startup/`,
`app/CMakeLists.txt`, `app/cmake/WindowsStandalone.cmake`.

Validation: `python app/tests/test_startup_variants.py` with PySide6 and rcc.
Exercises percentage boundaries, exclusive visibility, all three PNG animations,
frame advancement, full display durations, animation shutdown, event-loop survival after splash and fallback.
The shared navigation test fixes its draw to the logo for repeatable tests.
Validated here with Qt/PySide 6.10.2; target builds still require Qt 6.12.

App 0.1.41 R1 includes a server policy promotion: deploy the server to admit
client 0.1.41 (minimum remains 0.1.39). Existing 0.1.41 app builds do not need
to be rebuilt; no database reset is required.
