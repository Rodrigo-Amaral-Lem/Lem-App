# LEM App

The Qt client for charger maintenance reports. The current source declares **0.1.57**, uses
checklist **6**, and includes the manual-reading, general-notes and signature-photo changes approved
after that release number was assigned. There is no newly numbered binary release implied by these
guides.

## Start here

| Guide | Use it for |
| --- | --- |
| [Build and release](docs/build-and-release.md) | Qt Creator, desktop builds, Android HTTPS, Windows packaging and troubleshooting |
| [Architecture](docs/architecture.md) | Source ownership, state, persistence, synchronization and asynchronous work |
| [Field workflow](docs/field-workflow.md) | Reports, evidence, copying, signatures, conflicts and local cleanup |
| [Testing](docs/testing.md) | Native tests, QML tests, integration runners and platform checks |

Open [app/CMakeLists.txt](app/CMakeLists.txt) in Qt Creator. Use a matching Qt/compiler kit and a
**new build directory inside `../ongoing_work/` **. A normal Windows build uses
`LEM_WINDOWS_STANDALONE=OFF`; a single-file build requires a separate static Qt installation.

## Repository boundaries

- `app/` contains the client and its platform/build helpers.
- `shared/` is a tracked snapshot of the canonical resources in `../Common/shared/`.
- `docs/` contains the four maintained client guides above. Update them when behavior changes rather
  than adding version-named notes.

See the [workspace guide](../Common/docs/workspace.md) for Git/review rules and the
[report contract](../Common/docs/report-contract.md) for shared validation rules. Edit shared
resources in Common, run its synchronization tool, and review the snapshots in both component
repositories. Keep generated files in `ongoing_work`; `Brand_Images` remains user-managed.

## License

First-party LEM source code, tests, build/deployment tools, configuration, schemas and accompanying
project documentation are licensed under the **GNU General Public License, version 3 only**
(`GPL-3.0-only`). See [LICENSE](LICENSE) for the complete terms and warranty disclaimer.

Source files carry SPDX notices. JSON data keeps its original strict format and has adjacent
`.json.license` notices. Existing third-party license notices remain applicable; this declaration
does not relicense Qt, Python dependencies, bundled fonts or third-party customer artwork/trademarks.
Generated reports and customer data are not relicensed by this source-code notice.

A complete license copy also accompanies the client source at [app/LICENSE](app/LICENSE).
