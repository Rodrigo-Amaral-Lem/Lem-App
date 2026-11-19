# Field workflow

The interface is Portuguese. The workflows below describe the current source, including the editable
checklist-v7 rules. Detailed shared rules are in the
[report contract](../../Common/docs/report-contract.md).

## Select a contract and charger

An administrator creates contracts, assigns technicians and imports chargers. A technician sees
assigned contracts and can open cached work offline. The charger list combines server catalogue
summaries with local report state, so a locally created or changed report remains visible before
upload.

A physical station may belong to several contracts. Manufacturer/model/serial corrections entered in
a report are applied to the canonical station when closure succeeds, with conflict checks. An older
report keeps its historical snapshot.

Excel import accepts empty model, serial number, energy supply, power and power-limit cells.
Model (EST.03) and serial number (EST.04) must be filled before closing a report, both offline
and on the server. Drafts can be saved while these fields are still empty.

## Fill and review a report

The groups are **SEG → SITE → QVE → EST → MAN → CARG → FIN**. Switching groups retains position and
releases deferred evidence synchronization. Review shows missing answers, required photographs and
required observations; tapping an issue returns to the field.

Status questions use OK/NOK/N/A. Measurement questions keep the reading separate from `assessment`.
OK/NOK never fills or replaces a mandatory reading. N/A clears its separate assessment. NOK requires an observation, including SEG.02–SEG.06. These items show the field automatically
without an observation button or attachment controls; SEG.02 and SEG.04 also require a reason for N/A.

MAN.02 uses OK/NOK/N/A and requires an observation for NOK; N/A does not require one.

SEG.01 requires an explanatory observation for N/A; a photograph remains exempt for N/A.

SEG.07, SITE.06, QVE.14, EST.18, MAN.22, CARG.07 and FIN.03 are optional observation/media entries with no status buttons. Leaving
them empty does not block completion. MAN.12 needs a photograph when applicable; N/A exempts the photograph but requires an observation. A video does
not satisfy a required photograph.
MAN.14–18 also require a photograph for OK and NOK; N/A requires an observation and exempts
the photograph. Existing editable v6 drafts and closed-report correction copies upgrade to v7; the original closed record is preserved until replacement succeeds.

The application validates closure locally; the server validates again after synchronization. A
report marked closed locally can still have pending upload work. Confirm the synchronization state
before clearing data or changing devices.

Only the current owner can change answers/evidence or close/send a report, including when the
viewer is an administrator. Other authorized users can read and download it. Manual synchronization
also receives current catalogue data and refreshes an open report without local edits, so a report
closed by its owner is shown as closed on the viewing device. Local unsent work is preserved if
another device changes the report; resolve the reported conflict before continuing.

## Photographs, videos and editing

Use the camera/gallery controls on the relevant item. File selectors accept multiple files;
admission proceeds sequentially and reports how many were accepted/refused. Invalid files do not
prevent the remaining selection from being processed. Original gallery files are not deleted.

A downloaded photograph has enlarge, delete and **Editar fotografia** controls. The editor offers
clockwise 90° rotation and an optional **Recortar** icon button, initially off. Rotating carries the
selected area with the image; disabling crop saves the whole rotated image. **Repor** resets the
preview and **Cancelar** discards it. **Guardar** applies rotation and crop together, creating one
replacement private image and queuing the old evidence for removal only after durable local saving.
Saving without changes retains the evidence ID and avoids recompression/upload. The preview stays
open if the edit cannot be accepted; saving waits for active sync/attachment operations to finish.
Preview/download availability depends on whether the original is cached. Videos use playback/download
controls rather than image editing. Photograph/video/report limits are listed in
[Security and limits](../../Common/docs/security.md).

## Signature

Enter the technician name in FIN.02 before signing.
Signing and replacing a signature remain available during background synchronization. Acknowledgements
merge into the latest local work; they cannot discard a signature collected during an upload.

- **Assinar** opens the drawing canvas.
- On desktop, **Anexar fotografia da assinatura** selects a local JPG/PNG of the signature. It is
  registered as the signature, not merely an unrelated FIN.02 attachment.
- Replacing a signature replaces its evidence reference. Changing the technician name invalidates
  the previous signature.

The exported signature is associated with the recorded name and timestamp. A random attachment at
another checkpoint cannot satisfy that reference. Signature controls do not make every signature
mandatory; closure follows the saved checklist and validates references that are present.

## Reuse another charger's work

**Copiar QVE de outro carregador…** copies the QVE group, including its optional general
observations. **Copiar Medições de outro carregador…** copies only **MAN.04–MAN.10**; MAN.21/MAN.22 and the
other maintenance answers are preserved.

Choose another charger/report in the same contract and confirm applicability. Cached source answers
are preferred; missing source evidence may need downloading. Both operations copy answers,
observations and associated photographs/videos under new IDs, retaining source metadata for exports.

You can request the other group while one is copying and choose a different source. The second
operation runs afterward. Only the group being replaced is locked for editing. Closure, changing
report/session and conflicting cleanup wait for the operation. If staging fails, the current target
group is conserved. The pending copy is not a durable queue across an application restart.

## Synchronization, recovery and ownership

Use manual synchronization when ready to send deferred evidence. A lost network acknowledgement is
retried using the report's durable revision/fingerprint data; do not recreate or delete the report
just because a request failed.

If ownership, access or revisions conflict, inspect the local/server comparison. Keep protected work
or create an independent draft before choosing a resolution. Ownership transfer belongs to
administrators and retains history. A permanently deleted server report is not restored by an old
local cache.

## Offline downloads, cleanup and exports

Download evidence in advance for reports needed offline. Keeping metadata locally does not guarantee
every original image/video is present.

**Limpar cópias locais** keeps reports, answers, history and evidence metadata. It removes private
evidence files only from fully acknowledged reports that are eligible for cleanup, releases the
offline pin, and does not delete server data. Pending, conflicted or unconfirmed work keeps its
files. Gallery originals remain untouched.

PDF and ZIP exports use the device when the report is closed and all original evidence is local,
including before synchronization. Otherwise the app uses the server, requiring a session, network
access and a synchronized report. Exporting does not upload or acknowledge pending work. A ZIP
includes the PDF, report JSON and original photos/videos; it preserves the selected issuer/style.

Evidence downloads run up to four requests at once. Transient network/server failures retry up to
three times; updated servers support resuming the current original from its received byte offset.
A permanently failed item does not discard the remaining queue. Completed files remain available;
repeating **Descarregar evidências** requests only missing files. Cancellation/app exit does not
retain an unfinished file's partial bytes. Server ZIP failures retry the whole archive, while keeping
the chosen destination unchanged until reception finishes.

Downloading report A does not block editing, copying or deleting a draft in report B. Actions that
would replace/delete report A stay protected until its downloads finish. Account/server changes and
global cache cleanup still wait for transfers, because they affect every report's storage/session.

Reports in a contract awaiting its 30-day deletion cannot synchronize. The API rejects requests even
from already logged-in users. When the app receives this rejection, or its administrator deletes the
contract, it retains a suspension marker with the local answers and evidence, including across restart.
Other contracts continue synchronizing. The report displays the reason; after administrative recovery,
refresh/synchronize the contract list to lift the suspension and send the preserved work.

Drafts, closed reports and PDF/ZIP exports use `ID_Contrato`. The PDF and JSON inside the
ZIP share that name, and the original evidence filenames include the same prefix plus the
evidence ID. Spaces and characters that cannot appear in filenames become hyphens. Intervention
dates appear separately in the catalogue and inside the report.

On Windows/Linux desktop, **Contratos → Exportar todos os PDFs… / Exportar todos os ZIPs…**
exports one file per closed report into a new folder under the chosen destination. A session and
network connection are required to discover the current complete server list. The owner's locally
closed pending reports are also included if they have no conflict/suspension or unfinished correction.
Other users' stale drafts never hide closed server reports. Cached evidence is reused with current
server report metadata; otherwise each export is streamed from the server, with bounded retries.
The queue processes one report at a time without changing the open report or blocking its editing.
Cancel stops this batch's requests only and retains completed files. Failed reports and unprocessed
IDs appear in `resumo-exportacao.json`; repeating creates another folder, never replaces previous
deliveries. Duplicate public filenames use a `report-<internal ID>` subfolder. Individual exports
remain available on Android; whole-contract export is desktop-only.

Administrators can use **Gestão → Contas e acessos**, select a technician, and choose
**Repor password do técnico…**. Enter the replacement twice (12–200 characters) and confirm.
The server accepts only technician targets, revokes every previous session and records the actor
and target in its audit. Reports, ownership, assignments and device caches remain unchanged.
Administrator passwords still use the server administration console. Passwords are neither stored
in the app's account list/results nor included in audit output. This action requires the updated
server endpoint; older compatible servers return an explicit update/error message.

Administrators can select a contract and open **Gestão → Importar / formato dos reports**.
Choose **Lem** or **CSMTEC** as the issuer on the left, then zero, one or two client logos on
the right. Import preview includes these choices; **Guardar formato dos reports** changes them
without importing an Excel. Technicians cannot change these settings.

Changes apply to future PDF/ZIP exports of all reports in that contract, including closed reports.
Previously downloaded files are unchanged. Refresh/synchronize each device to receive the updated
presentation before exporting offline. Answers, ownership, closure and report revision remain intact.

Saving settings applies the server's confirmation immediately. Online export checks the current
contract format before rendering. Selecting an issuer in the list still requires **Guardar formato
dos reports**; an explicit notice identifies unsaved choices.

On Windows/Linux desktop, an administrator can use **Corrigir report fechado…** on a synchronized
closed report. Edit the working copy's answers, observations and evidence as with a draft. Use
**Editar início e fim…** for the intervention dates (including timezone). At review, choose
**Guardar correção e substituir original…** and confirm. The original remains available until the
server accepts the complete replacement, which preserves the technician's ownership. Cancelling
the working copy leaves the original. If another administrator replaces it first, the losing copy
is protected for recovery. A pending correction cannot be exported as a completed replacement.

Changing manufacturer (EST.02), model (EST.03) or serial (EST.04) also updates the server's charger
record for that physical ID when the correction commits, across its contracts. Draft edits and
cancellation do not update the record. If another intervention changed the same field meanwhile,
the server rejects the conflicting correction and preserves both reports; review the current
charger data before resolving it. Unchanged fields and other historical reports are preserved.

The corrected report gets a new internal ID. The changed dates appear in its PDF and JSON.
The export filename uses the charger ID and contract name.

N/A suggestions are editable and apply only when entering N/A with empty notes. Existing notes
are preserved. See the report contract for all required-note items. FIN.02 hides general media
buttons and keeps notes and dedicated signature controls. QVE.01–03 use “Botoneira”.

## Current measurements and photo tools (0.2.6)

MAN.11/12 accept at most six integer digits in mA (1 A = 1000 mA; 0.03 A = 30 mA).
MAN.12 and MAN.18 recalculate from MAN.11 using the boundaries in the shared report contract.
A migrated ambiguous reading shows its previous text and needs confirmation. Untouched suggested
observations clear on state change or when entering the notes editor; custom text survives.
EST.01 records before views/cleanliness; MAN.21 after views/cleanliness; general notes are MAN.22.
EST.10 is removed, EST.13 only checks tariff and EST.16 only DGEG stickers. EST.15 checks the payment
terminal (TPM) and suggests `Sem TPM` for N/A.

The photo editor uses two 48-pixel icon-only controls: clockwise rotation and crop. Crop is optional
and its selected background shows whether it is active. Both icons are drawn, independent of fonts.
