# Field workflow

The interface is Portuguese. The workflows below describe the current source, including the editable
checklist-v8 rules. Detailed shared rules are in the
[report contract](../../Common/docs/report-contract.md).

## Select a contract and charger

An administrator creates contracts, assigns technicians and imports chargers. A technician sees
assigned contracts and can open cached work offline. The charger list combines server catalogue
summaries with local report state, so a locally created or changed report remains visible before
upload.

Open **PCVEs** in the toolbar or compact menu to search by ID or field values.
Each PCVE appears once.
Technicians see the IDs from all their active assigned contracts. Administrators can also see IDs
awaiting a contract. **Atualizar** downloads the catalogue for offline consultation.

Importing a contract workbook creates or updates the central station and links that ID to the
contract. Every linked contract uses the same station details. On desktop, administrators can use
**Importar Excel** from **PCVEs** to create or update station records independently. Open a
station and choose **Editar ficha** to change its current fields. The PCVE ID is permanent.
**Relatórios associados** lists the reports you can read through direct contract assignments or work-order visits. Back returns to the same PCVE and scroll position. Offline, it shows reports saved on this device.
Select a contract and open **Gestão → Contratos → Gerir IDs do contrato**. Use **Adicionar ou atualizar por Excel** to add memberships and update the central PCVE records shared by every linked contract. On desktop, **Editar** opens the same central record. **Retirar** starts the 14-day recovery period for that ID and its reports. Open **Recuperar IDs retirados → Recuperar**, or reimport the ID before the deadline. A later reimport starts work on the ID with its expired reports removed.
A closed report on the contract equipment card shows its local closing day, month and hour. For example, `30_03_16` means 30 March at 16:00–16:59.
Use the postal address and coordinates for navigation. The site-name field is omitted.

Manufacturer, model and serial corrections entered in a report are applied to the central station
when closure succeeds, with conflict checks. Earlier reports retain their historical snapshots.

Excel import accepts empty model, serial number, energy supply, power and power-limit cells.
Model (EST.03) and serial number (EST.04) must be filled before closing a report, both offline
and on the server. Drafts can be saved while these fields are still empty.

The optional **Notas** column supplies instructions for each ID in this contract. Technicians
see them on opening a report and can reopen **Notas do PCVE** from its menu. Offline, the app
uses the last notes saved for that membership, including when a report is opened from the
history of an ID removed from its contract. Administrators can use the notes icon beside
navigation on the contract card, then **Editar notas** and **Guardar**.

## Fill and review a report

The groups are **SEG → SITE → QVE → EST → MAN → CARG → FIN**. Select a group tab or choose **Grupos** to jump directly to another section. The three-dot **Opções do report** panel groups consultation, exports for closed reports, synchronization and management. Switching groups retains position and
releases deferred evidence synchronization. Review shows missing answers, required photographs and
required observations; tapping an issue returns to the field.

Status questions use OK/NOK/N/A. Measurement questions keep the reading separate from `assessment`.
OK/NOK never fills or replaces a mandatory reading. N/A clears its separate assessment. NOK requires an observation, including SEG.02–SEG.06. These items show the field automatically
without an observation button or attachment controls; SEG.02 and SEG.04 also require a reason for N/A.

MAN.02 uses OK/NOK/N/A and requires an observation for NOK; N/A does not require one.

SEG.01 requires an explanatory observation for N/A; a photograph remains exempt for N/A.

SEG.07, SITE.06, QVE.14, EST.18, MAN.24, CARG.07 and FIN.03 are optional observation/media entries with no status buttons. Leaving
them empty does not block completion. MAN.13 and MAN.14 need a photograph when applicable; N/A exempts the photograph but requires an observation. A video does
not satisfy a required photograph.
MAN.16–20 also require a photograph for OK and NOK; N/A requires an observation and exempts
the photograph. Existing drafts and closed-report correction copies upgrade to v8. The old measured current and evidence move to MAN.13. Complete both new times in MAN.12 and MAN.14 before submitting. The server keeps the closed original until it accepts the replacement.

The application validates closure locally; the server validates again after synchronization. A
report marked closed locally can still have pending upload work. Confirm the synchronization state
before clearing data or changing devices.

Only the current owner can change answers/evidence or close/send a report, including when the
viewer is an administrator. Other authorized users can read and download it. Manual synchronization
also receives current catalogue data and refreshes an open report without local edits, so a report
closed by its owner is shown as closed on the viewing device. Local unsent work is preserved if
another device changes the report; resolve the reported conflict before continuing.

## Photographs, videos and editing

On Android choose **Tirar fotografia** on the relevant item. Open **Outros anexos** on that item for a gallery photo, a video recording or a gallery video. On desktop choose **Adicionar fotografia**, or open **Outros anexos** for **Adicionar vídeo**. **Adicionar observação** opens the optional note. File selectors accept multiple files;
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
observations. **Copiar Medições de outro carregador…** copies only **MAN.04–MAN.10**; MAN.23/MAN.24 and the
other maintenance answers are preserved.

Choose another charger/report in the same contract and confirm applicability. Cached source answers
are preferred; missing source evidence may need downloading. Both operations copy answers,
observations and associated photographs/videos under new IDs, retaining source metadata for exports.

You can request the other group while one is copying and choose a different source. The second
operation runs afterward. Only the group being replaced is locked for editing. Closure, changing
report/session and conflicting cleanup wait for the operation. If staging fails, the current target
group is conserved. The pending copy is not a durable queue across an application restart.

## Synchronization, recovery and ownership

On Windows, closing the window keeps Lem in the notification area. Open the icon or
launch the shortcut to restore the same session. Choose **Sair** from the icon to quit.
On Android, an evidence download started in the app continues after Home. With
notifications enabled, tapping the transfer notification brings the app back.
While signed in and in the background, the client refreshes accessible contracts,
the selected contract's chargers and the PCVE database. Android schedules these
checks when a network is available. If the OS ends the process, reopen Lem and
request any missing originals again.

Use manual synchronization when ready to send deferred evidence. A lost network acknowledgement is
retried using the report's durable revision/fingerprint data; do not recreate or delete the report
just because a request failed.

Use the arrows beside **Retirar** to reorder visits in the planner. Open **Informação do ID** on a visit to expand its details. If a PCVE is temporarily withdrawn from its contract, **Recuperar rascunho local** in the order
opens the draft kept on the device. Once the administrator restores the membership, synchronize
to retry the original visit. A different or completed visit still requires separate recovery.

When another dispatcher saves an order during editing, choose **Rever alterações**. Fields changed
on one side are combined. For conflicting fields, choose the displayed server value or keep your
own value before saving against the current revision.

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

On desktop, cleanup and **Permitir limpeza destas fotografias** suppress automatic evidence
downloads for that report from followed orders. **Descarregar todas as evidências** restores offline
retention. Administrators can choose **Deixar de seguir** after an order is completed.

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
closed report. The server copies the existing evidence into the working draft. You can edit answers
and observations, remove an attachment or add a new one with an empty local evidence cache. Only new
attachments are uploaded. Download an individual file if you want to inspect its contents.
The photo editor preserves historical signature images. An explicit removal also removes
the signature reference from the correction. Use
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

## Measurements and photo tools

MAN.11 and MAN.13 accept at most six integer digits in mA (1 A = 1000 mA; 0.03 A = 30 mA).
MAN.13 and MAN.20 recalculate from MAN.11 using the boundaries in the shared report contract.
MAN.12 (configured) and MAN.14 (measured) accept integer readings from 0 to 999999 ms, or NA. The form shows **1s = 1000ms**. MAN.12 has optional photography. For MAN.14 choose OK/NOK/N/A. Use **Leitura indisponível (NA)** when the test result is known and the tester could not record a time. OK still requires a photograph. NOK requires a photograph and an observation. Choosing N/A as the test result requires an observation and makes the photograph optional. MAN.13 and MAN.14 suggest “Proteção diferencial não dispara” for NOK and “Não testado devido a veículo em carga” for N/A.
A migrated ambiguous reading shows its previous text and needs confirmation. Untouched suggested
observations clear on state change or when entering the notes editor; custom text survives.
EST.01 records before views/cleanliness; MAN.23 after views/cleanliness; general notes are MAN.24.
EST.10 is removed, EST.13 only checks tariff and EST.16 only DGEG stickers. EST.15 checks the payment
terminal (TPM) and suggests `Sem TPM` for N/A.

The photo editor uses two 48-pixel icon-only controls: clockwise rotation and crop. Crop is optional
and its selected background shows whether it is active. Both icons are drawn, independent of fonts.

### Reconnecting during a planned visit

Open the order to see who owns the accepted draft. A distinct unsent copy remains available through the local recovery action. On restored access, synchronize the original report. For a permanent handoff, save its recovery ZIP and give it to the new owner. Retain the original device copy until the recipient checks the answers and attachments. A later visit to the same membership has a separate item ID.

For a replanned corrective or commissioning visit, start its draft from the new order. The draft and evidence from the impeded visit remain available through recovery. If the office schedules a future order while an individually shared report is offline, the App keeps it pending and rechecks the start restriction on Sync. It also rechecks automatically while online.

For an individual handoff, first sync the outgoing technician's changes. In the intervention, the administrator selects the next technician under **Atribuição individual** and chooses **Transferir atribuição e rascunho**. The new technician receives the same draft and its synchronized evidence. If another user changes the draft before the action, review the refreshed intervention and try again.

A decimal point or comma in an integer mA reading remains visible until corrected. Review and correct the value before closure. A rejected oversized report reopens under its original ID. The device keeps its evidence and checks the finish against the recorded start.

## Recovering a pending intervention or corrective contract ID

An online administrator opens a pending corrective or commissioning request (including one with a draft), selects **Apagar pedido** and types **APAGAR**. In the same intervention list choose **Recuperar pedidos apagados → Recuperar pedido** before 48 hours have elapsed. Recovery restores the request, its synchronized draft and original attachments. A job with a closed report cannot be deleted this way.

In **Contratos de corretivas**, select the contract’s **Gerir IDs** row. **Retirar** starts a 14-day recovery period. **Recuperar** against that ID restores its coverage and the jobs and reports taken out with it. A separately deleted job keeps its original 48-hour deadline. Restore the coverage first, then the job while both actions are still available. A whole preventive contract has its own 30-day recovery procedure.

Refresh the membership list if a removal is refused after another administrator recovers the ID. When a pending intervention is permanently deleted, its work-order visit records the impediment. If it was the last pending item, the order completes.
