# Automatically open exported PDFs - 0.1.37 R4

After a successful PDF export, Lem asks the operating system to open the saved
file in its associated PDF viewer. This applies to local/offline PDFs and the
server fallback, across desktop and Android. ZIP exports retain their existing
behaviour.

The local PDF worker finishes writing and closes the destination before its
GUI-thread completion callback requests the viewer. The server path likewise
closes/commits the destination before requesting it. Failed generation, invalid
server data, cancelled file selection or failed writes never request a viewer.
Draft export restrictions remain unchanged.

`QDesktopServices::openUrl` handles the platform handoff: desktop file associations
and Qt's Android content-URI/FileProvider support. No embedded PDF engine,
additional file copy or server change is needed. The system may present an app
chooser when no default is selected. A PDF viewer must be installed.

If the launch request returns false, the normal export notification explains
that the PDF is saved but could not be opened. The document remains in the chosen
location. A successful launch request does not guarantee that a third-party
viewer will finish opening the document; Qt cannot report that later outcome.

Validation:

- Compiled the changed native controller and extended offline PDF regression
  against the available Qt 6.10.2 validation runtime.
- Intercepted the desktop file-open request to avoid starting a real viewer.
  The handler verified the complete PDF was readable, including its EOF marker,
  at the instant opening was requested.
- Confirmed one launch for a successful offline export with no logged-in session,
  and no launch for a draft or failed write to a missing destination directory.
- Existing long-note/photo rendering and missing/corrupt evidence destination
  protection checks passed.
- Server success/failure placement was reviewed; its writer and response validation
  remain unchanged. The Android viewer handoff and full release builds still
  need confirmation on the physical device/desktop kit.

Reference: https://doc.qt.io/qt-6.12/qdesktopservices.html

Rebuild/install the app only; no server/database update. Protocol/client version
stays 0.1.37. Android build code: 145.
