# Field workflow

The interface is Portuguese. The workflows below describe the current source, including the editable
checklist-v6 corrections. Detailed shared rules are in the
[report contract](../../Common/docs/report-contract.md).

## Select a contract and charger

An administrator creates contracts, assigns technicians and imports chargers. A technician sees
assigned contracts and can open cached work offline. The charger list combines server catalogue
summaries with local report state, so a locally created or changed report remains visible before
upload.

A physical station may belong to several contracts. Manufacturer/model/serial corrections entered in
a report are applied to the canonical station when closure succeeds, with conflict checks. An older
report keeps its historical snapshot.

## Fill and review a report

The groups are **SEG → SITE → QVE → EST → MAN → CARG → FIN**. Switching groups retains position and
releases deferred evidence synchronization. Review shows missing answers, required photographs and
required observations; tapping an issue returns to the field.

Status questions use OK/NOK/N/A. Measurement questions keep the reading separate from `assessment`.
OK/NOK never fills or replaces a mandatory reading. N/A clears its separate assessment. NOK requires an observation, including SEG.02–SEG.06. These items show the field automatically
without an observation button or attachment controls; SEG.02 and SEG.04 also require a reason for N/A.

MAN.02 uses OK/NOK/N/A and requires an observation for NOK; N/A does not require one.

SEG.01 requires an explanatory observation for N/A; a photograph remains exempt for N/A.

SEG.07, SITE.06, QVE.14, EST.18, MAN.21, CARG.07 and FIN.03 are optional observation/media entries with no status buttons. Leaving
them empty does not block completion. MAN.12 needs a photograph when applicable; N/A exempts the photograph but requires an observation. A video does
not satisfy a required photograph.

The application validates closure locally; the server validates again after synchronization. A
report marked closed locally can still have pending upload work. Confirm the synchronization state
before clearing data or changing devices.

## Photographs, videos and cropping

Use the camera/gallery controls on the relevant item. File selectors accept multiple files;
admission proceeds sequentially and reports how many were accepted/refused. Invalid files do not
prevent the remaining selection from being processed. Original gallery files are not deleted.

A downloaded photograph has enlarge, delete and crop controls in one row. Cropping creates a
replacement private image and queues the old evidence for removal. Preview/download availability
depends on whether the original is cached. Videos use playback/download controls rather than image
cropping. Photograph/video/report limits are listed in
[Security and limits](../../Common/docs/security.md).

## Signature

Enter the technician name in FIN.02 before signing.

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
observations. **Copiar Medições de outro carregador…** copies only **MAN.04–MAN.10**; MAN.21 and the
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

Server PDF/ZIP exports require server access and accepted evidence. Local PDF generation uses the
current report and locally available evidence. A ZIP includes media originals as well as the report;
videos are not still photographs. Verify that required files are available before relying on an
offline export.

N/A suggestions are editable and apply only when entering N/A with empty notes. Existing notes
are preserved. See the report contract for all required-note items. FIN.02 hides general media
buttons and keeps notes and dedicated signature controls. QVE.01–03 use “Botoneira”.
