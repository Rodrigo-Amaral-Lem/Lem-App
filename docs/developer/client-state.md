# Client state, notifications and persistence

## Controller state

`Lem` is instantiated once and exposed as the QML context property `lem`.
The public getters convert Qt JSON containers into QVariant maps/lists for QML.
Those maps are projections: changing a delegate's JavaScript object does not save a report.

| Member / property | Purpose |
| --- | --- |
| `m_reports` | In-memory collection keyed by report UUID, persisted in `cache.json`. |
| `m_current` / `current` | Copy of the selected report; mutations deliberately mirror it into `m_reports`. |
| `m_user`, `m_token` | Active local account identity and in-memory bearer token. Cached identity alone is not authentication. |
| `m_contract`, `m_chargers` | Selected contract UUID and displayed catalogue. |
| `m_catalogues`, `m_catalogueTimes` | Per-contract cached summaries and last-refresh times. |
| `m_template` / `sections` | Checklist used by the selected report or the shared initial template. |
| `m_pendingEdits` | Latest journal snapshot waiting for each report ID. |
| `m_activeEdit`, `m_activeEditId` | Immutable snapshot currently being written by the worker. |
| `m_deletedReports` | Local tombstones protecting against old journal replay. |
| `m_syncConflicts` | Session-level exclusions; durable conflicts are additionally stored in report JSON. |
| `m_transfers` / `operationTransfers` | Active HTTP presentation records; not a persisted upload queue. |

`busy`, `syncing`, `refreshing` and `saving` are different states. Background
synchronization allows report editing; session changes/cleanup are more restricted.
`saving` means the journal queue still has work, including an active snapshot.
`localSaveError` exposes failed local persistence. `online()` only checks that a token
exists; actual reachability is established by HTTP results/network information.

## Notification contract

| Signal | Intended consumers |
| --- | --- |
| `changed` | General state/actions and small report projections. |
| `sectionsChanged` | Checklist model; emitted only if template content actually differs. |
| `contractsChanged` | Contract model; emitted only when contract content differs. |
| `localSaveStateChanged` | Pending/error save indicators; frequent local input must not reset checklist models. |
| `operationChanged` | Operation summary and transfer-byte widgets; progress is coalesced with a 100 ms timer. |

Do not use `changed()` for every byte-progress event. It can cause expensive
bindings/delegates to re-evaluate while a technician is typing. Likewise, do not
replace model-specific signals with the general signal for convenience.

## Storage layout

`AppStorage::prepare()` chooses the app root. Under it, `last-user.json` stores
server and user identity for reopening cached data; it does **not** contain the raw
session token. `QSettings` stores the server URL.

Each account has a cache directory identified by the first 16 hex characters of
SHA-256(server URL), then the user UUID. That directory contains:

| Path | Contents |
| --- | --- |
| `cache.json` | Contract/catalogue/report collections, selected contract, catalogue timestamps and local deleted-report IDs. |
| `edits/<sha256(report UUID)>.json` | Full snapshot of one report newer than the last full checkpoint. |
| `photos/<photo UUID>.jpg` or `.png` | Private image originals/copies used by that account/server scope. |

`queueEdits()` coalesces answer changes for 20 ms. A single worker writes an
immutable snapshot through `QSaveFile`. `completeEditWrite()` removes a pending
entry only if it still equals that snapshot. If more typing occurred during the
write, the newer entry stays queued.

`flushEdits()` is intentionally a blocking durability boundary. It waits for the
active worker, then writes the newest pending reports. `persist()` calls it before
atomically replacing the full cache and removes journals only after commit. App
backgrounding, shutdown and identity changes also flush. There is still a pending
write window before completion: the UI's save state matters, especially on storage errors.

`loadUser()` reads the cache, then replays journals only when the report ID is not
deleted, the journal owner matches the selected account, and `_edit_seq` is newer.
Startup migration must rewrite private file URLs in both checkpoints and journals.

## Local report fields

| Field | Meaning |
| --- | --- |
| `id`, `charger_id`, `owner_id` | Report, charger and current/local owner identities. |
| `revision` | Last server revision known to this local report; zero before initial creation is confirmed. |
| `status` | Local `draft` or `closed`; closed locally may still be draft remotely while images upload. |
| `payload.answers[checkpoint]` | `{value, notes}`. Status values are empty, `OK`, `NOK`, `NA`; text questions retain text. |
| `payload.started`, `payload.finished` | Client-provided dates used when creating/closing remotely. |
| `snapshot`, `template` | Stored charger/contract context and checklist for this intervention. |
| `photos` | Metadata plus local paths and upload acknowledgement flags; image bytes are separate files. |
| `dirty` | Local work still needs acknowledgement/reconciliation. |
| `_edit_seq` | Monotonic local mutation counter. It is not a server revision. |
| `_defer_photo_sync` | Hold automatic sending until leaving the question group/report or manual send/closure. |
| `deleted_photos` | Photo UUIDs to delete remotely, including potentially accepted but unacknowledged uploads. |
| `_inflight_payload`, `_inflight_revision` | Durable fingerprint for recognizing a PUT accepted before its response was lost. |
| `_ack_deleted` | Transient batch acknowledgement list used while merging photo removals. |
| `_conflict`, `_conflict_reason` | Durable protection and readable explanation after an ownership/revision/permission conflict. |
| `_conflict_remote` | Separately fetched server comparison; never a replacement for local answers. |
| `_conflict_copy_id`, `_conflict_copy_seq` | Reference to an independent backup copy and source edit sequence at copy time. |
| `payload.source_report_id` | Lineage of a new independent draft copied from another local report. |
| `offline_photos`, `last_access`, `last_sync` | Pin and timestamps supporting image availability/cache policy. |

A photo entry has `id`, `checkpoint`, `filename`, `path`, `uploaded` and, when
available, `mime`, `size` and server SHA-256 `digest`. `uploaded=true` means the
server acknowledged the image, not that the image necessarily exists on this device.

## Mutation discipline

For new code, capture report/photo IDs before asynchronous work. Read the latest
entry again when reconciling results; the active page may have changed and the
technician may have edited answers meanwhile. Use `.value()` for read-only JSON
lookups where accidental insertion would be harmful. Check local persistence
results before deleting files or claiming the operation is durable.

## Server preference selection (0.1.29)

`ServerSettings.h` is shared by every platform. On unmarked older settings, bare
HTTPS prototype aliases under the default server's DNS suffix migrate to the current
Lem root; independent/custom-path/port addresses stay intact. Saves mark an explicit
preference schema so subsequent launches respect the selection. `useDefaultServer()`
reuses the normal guarded server-switch path. Cache namespaces remain isolated by
server/user; this preference migration does not copy reports into another server's cache.

## Catalogue projection notifications (0.1.34)

`chargers` is a cached QVariant projection of the selected catalogue with local report summaries overlaid. The controller rebuilds/compares it on `changed()` and emits `chargersChanged()` only when its contents differ. QML refilters on that narrower signal, search changes or navigation. A progress/status-only change must not trigger JavaScript catalogue filtering. The cache contains metadata only, no extra image copies.


## Report projections (0.1.45)

`current` now uses `currentChanged`, not the broad `changed` signal. The getter
keeps an implicitly shared QVariant projection until report JSON changes.
`photosByCheckpoint` has its own `photosChanged` signal, driven only by changes
to the active report photo array. Answer changes must not notify this photo model.
The controller's changed listener reconciles these narrower signals synchronously.
See `android-performance-0.1.45.md` for measurements and regression tests.


## 0.1.55: cadastro comum e checklist explícita

`stations` é a ficha física; `chargers` é o vínculo de contrato, conservando os
UUIDs anteriores. `details` usa os dados da estação, e `legacy_details` guarda
a importação antiga para recuperação. Os catálogos incluem `station_id`.
Cada PUT de report envia `checklist_version`. Novos reports usam a versão 4;
reports já existentes usam a sua versão congelada. No fecho, `station_update`
confirma os dados canónicos para os catálogos locais. Consulte as notas 0.1.55.
