# QML presentation guide

[Main.qml](../../app/qml/Main.qml) is intentionally still one file in this
readability-only edition. Moving components between files would introduce resource,
creation-context and binding-scope changes; that is a separate functional refactor.
Formatting now expands declarations/handlers so those boundaries can be read directly.

## Read the file in sections

1. **Application window and splash:** branding, viewport defaults and two-second
   nonblocking startup popup/timer.
2. **Palette and reusable inline controls:** `UiIcon`, spinner, buttons, labels,
   text fields/areas and combo boxes. Icons draw geometry rather than relying on
   Unicode glyph coverage on Android.
3. **Report/group helpers and view state:** selection, scroll positions, answer/
   evidence lookup, review issues, import mappings and permission projections.
4. **Header and menus:** navigation, report actions and account/session icon.
5. **Dialogs:** confirmations, account, operation status, report review, downloads,
   ownership transfer and protected server comparison.
6. **Pages:** catalogue, selected report with active group loader, administration/import UI.

From 0.1.24, the footer is one stable row: the animated operation-status icon opens
the detailed status dialog, while **Sinc.** initiates sending. Compact feedback is
a dismissible overlay, not a second toolbar row. Header controls keep 48 px touch
targets around smaller visual surfaces. See [compact bars](../compact-bars-0.1.24.md).

Search for an `id` or `objectName` to locate a surface. `objectName` values are
also used by automated UI tests; changing them changes the test interface even
when the visible label stays the same.

## Scroll-aware Android navigation

From 0.1.26, `ScrollChrome` and `appFooter` are overlays translated/faded above a
fixed page viewport. Permanent scroll-content insets keep first/last items reachable.
Do not animate their contribution to a parent layout: that changes viewport height,
question positions and scrollbar geometry while the user is swiping. `pagePositionBar`
uses a stable track and active visible-area ratios. The main Lem header is excluded.

The active page scroll source supplies direction changes; plain programmatic
positioning and dialog scrolling do not trigger hiding. Question actions remain
ordinary scrolling content. No persistence/network work belongs in these callbacks.
See [stable scrolling](../stable-scroll-0.1.26.md) for the invariant checks and the
single-row OK/NOK/N/A/eraser controls.

## Durable state versus presentation state

| State | Owner |
| --- | --- |
| Report answers, photo metadata, revision, conflict markers | C++ `Lem` / persisted JSON. |
| Server owner/permission/revision decisions | FastAPI / database. |
| `page`, selected question group, per-group scroll offsets | QML presentation state. |
| Optional notes editor visibility, focus target | QML, derived from stored answers as needed. |
| Confirmation target report/photo/checkpoint IDs | Dialog-local properties captured before opening. |
| Per-byte transfer progress | C++ `operationTransfers`, read by the status dialog. |

Bindings use `reportData: lem.current` as a cached projection for helper loops.
Delegates call `lem.answer()` or another explicit action; they do not persist
changes by mutating that projection. Notes for NOK remain required; optional notes
can be hidden/removed through the existing confirmation flow.

The `editing` property requires a local draft, current owner and no protected
conflict. Read-only controls remain visible for completed/other-owned/protected
reports. The server repeats these checks independently. An admin's delete privilege
must not be reused as a generic editing flag.

## Responsiveness constraints

`sectionPanels` describes the groups, but `sectionLoader` activates only the selected
group's questions. Instantiating the full checklist at once increases creation and
binding work, especially on Android. Group changes preserve scroll offsets and
release the C++ photograph hold through `reportSyncBoundary()`.

A normal answer tap should not rebuild `lem.sections`. C++ emits `sectionsChanged`
only when the actual template changes. Save feedback has `localSaveStateChanged`;
transport progress has `operationChanged`. Keep those separate from full model
notifications.

The operation-transfer repeater uses an integer count and indexed lookups instead
of replacing a QVariantList model on every byte notification. The status dialog
shows report/photo batch progress separately from transport bytes. When the upload
body is sent but the server has not replied, the indicator stays indeterminate:
100% transport upload is not confirmation of a committed report.

`compact` uses the existing width/orientation rules; touch targets are 48 px on
Android/compact view and 44 px otherwise. Keep the explicit application palette
and shared control colors consistent. System file dialogs may retain platform styling.

## Navigation and asynchronous confirmations

Menus toggle their `visible` state and use the existing outside-parent close policy.
Changing this casually can recreate the earlier press-to-close/reopen problem.

When a destructive/ownership dialog opens, store the report ID and verify it still
matches `lem.current.id` at confirmation. C++ also captures IDs for requests and
photo results. Do not depend on the page that happens to be active when a response
returns. Keyboard/focus handling belongs in UI actions; network callbacks should
not directly reach into question delegates.

The conflict banner leaves local work visible and protected. `serverVersionDialog`
reads `_conflict_remote`; it is a read-only answer/observation comparison with photo
count, not a second editable report editor or automatic merge tool. `copyLocalReport()`
and `useServerReport()` remain C++ actions with independent durability guards.

## Where to change a feature later

| Intended change | Follow this route |
| --- | --- |
| Add a question | Shared checklist -> C++ default/template loading -> server validation -> report snapshot/export behavior. |
| Change answer validation | UI guidance -> `Lem::finish()` -> server `put_report()` -> workflow tests. |
| Add a photograph action | QML captured IDs -> C++ local file/queue handling -> server owner/revision checks. |
| Add operation feedback | `trackReply`/`operationStatus` -> `operationChanged` bindings; avoid model reset signals. |
| Change contract import fields | `import_profiles.py`/`importer.py` -> server preview -> QML mapping fields -> import tests. |

Run [test_report_tabs.py](../../app/tests/test_report_tabs.py) for both phone and
desktop sizes. It uses a mock backend: passing it demonstrates UI behavior, not
server authorization or durable C++ persistence. Those have separate tests.

From 0.1.27 the standalone position bar stays active whenever visible. Catalogue
rows remain cached while a report is open; entering the catalogue refreshes the
projection and restores a charger-ID/within-card anchor. The passive `PointHandler`
on report content recognizes deliberate horizontal swipes without taking the
Flickable's vertical grab. It reuses `selectSection()` and its synchronization boundary.

From 0.1.29 `feedbackToast` follows the visible footer height (`height * chromeProgress`)
plus a gap. Its hit area never intersects the footer; it is enabled on desktop too.
No toast height enters page layouts, so message changes cannot move report questions.
`test_feedback_layout.py` checks multiple viewport sizes and animation positions.

From 0.1.31 all page Flickables use `StopAtBounds` for both boundsBehavior and
boundsMovement; catalogue ListView declares these directly, while the two
ScrollView-created Flickables are configured after completion. Dragging/flicking
past the final item cannot pull it far above the viewport. `pageBottomInset` reserves
only the footer overlap (excluding the outer page margin) and pageGap. ColumnLayouts
subtract their existing spacing from the final spacer; ListView has no such extra
layout gap. The inset stays fixed throughout chromeProgress animations.

## Rendered page bounds (0.1.32)

Report/admin pages are explicit Flickables. `reportScroll.contentHeight` follows
`sectionCard.lastCheckpointBottom`, measured from the actual last checkpoint through
the Loader/panel offsets, plus the fixed end inset. It does not inherit ScrollView's
automatic child extent. Admin uses its explicit final inset item. The footer's height
is touchHeight plus its padding, regardless of visibility or row implicit-size changes.
`trimCatalogueTail()` caps against the last instantiated delegate without changing
ListView's virtual contentHeight; a guard prevents recursive scroll-direction feedback.
The position indicator recognizes this rendered catalogue end too. Keyboard handlers
and attached scrollbars retain desktop input. See the new transition regression.
