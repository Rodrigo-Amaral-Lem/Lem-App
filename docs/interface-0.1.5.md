> Atualização 0.1.23: a identificação Android atual é `pt.lem.app`, uma instalação separada. Sincronizar trabalho pendente antes de trocar. As notas de compatibilidade desta página descrevem a entrega histórica.

# LEM 0.1.5 — visible loading state and consistent naming

## Loading feedback

The header now uses a drawn 36 px spinner with a bright white arc and a 5 px stroke. The status area shows a second 40 px green spinner on a light green background, with bold text: **A processar… Por favor aguarde.** Both animate only while an operation is busy. The controls retain their existing enabled/disabled behaviour. The spinner uses geometry rather than platform fonts or a platform-dependent control palette.

## Naming

The application name, executable target, QML module, source archive folder, API title, examples in documentation and deployment references now use **LEM**. The executable is `LEM` on Linux and `LEM.exe` on Windows. The QML module URI and the `loadFromModule` call both use `LEM`.

The existing Android package identifier is retained so deployment updates the installed app and keeps its local data. Database names, credentials and configuration keys retain their technical identities. The current C++ controller is named `Lem` and its QML context is `lem`.

## Local data compatibility

On desktop, changing the application name changes Qt's default application-data directory. At startup the app checks for exactly one previous sibling profile with the existing cache/session format, copies it into the new profile and updates local photo URLs. Source files are kept. An interrupted copy can resume; if copying cannot finish, the current session uses the original profile. Existing destination data is never replaced by a newly discovered profile. Multiple possible source profiles are not combined automatically.

The server preference is recovered first from the copied session; when needed, a unique server preference can also be read from sibling settings. This avoids embedding a previous product title in the source. Migration is a local file operation and does not reset reports or contact the server.

The deployment script now discovers a single matching installation under `~/apps` by its files, regardless of folder name, preserving the existing deployment location. If more than one installation matches, pass `-RemotePath` explicitly. An explicit path is respected. This release does not move a remote installation or alter existing database volumes.

## Update

1. Extract the archive into a new folder; its root folder is `LEM`.
2. Open `LEM/app/CMakeLists.txt` in Qt Creator and use a new build directory because the executable target and module URI have changed.
3. For Android, keep/re-enter your working `LEM_ANDROID_OPENSSL_ROOT` value. Build and deploy with the same signing key, over the installed app.
4. Confirm **0.1.5** in Conta. Trigger Atualizar or Sincronizar and check the header spinner and the status message.

Only the app needs rebuilding for this UI update. The API's source title is updated for the next normal server deployment. No server deployment or database reset was performed as part of this delivery.

## Validation

- Desktop and 390×844 QML tests passed under Qt 6.10.2/PySide6, including visible busy indicators, busy status text, section navigation, Android photo-action routing and retained answers. No QML warnings were reported; both rendered busy views were inspected.
- The C++ local-storage migration test compiled and passed on Linux with Qt Core 6.10.2. It covers copying session/photo data, rewriting cached photo URLs, retaining original files, preserving existing destination data, rejecting ambiguous profiles and retrying an interrupted migration.
- The native C++ client persistence smoke test compiled and passed with Qt 6.10.2, exercising local report responses, photo attachment, reopening the report and incomplete-report validation.
- Five deployment-discovery tests and seven Android OpenSSL CMake checks passed.
- A text-source scan found no remaining occurrence of the previous combined product title or its compact/separator variants in the packaged text files.

The complete Qt 6.12 application and its renamed QML module were not built here. Windows/Android/iOS deployment, the desktop registry branch of settings recovery and full SSH/PowerShell deployment still require verification in their respective environments. The C++ tests ran on Linux; they are not a Windows migration test.
