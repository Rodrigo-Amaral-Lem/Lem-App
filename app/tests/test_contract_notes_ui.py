# SPDX-License-Identifier: GPL-3.0-only
"""Exercise membership-specific notes on the existing QML navigation fixture."""
from pathlib import Path
import traceback
from shiboken6 import delete


writes = []
reads = []
history = {}


def install(attrs):
    attrs["historicalContractNotes"] = Slot(str, str, result="QVariantMap", name="historicalContractNotes")(
        lambda self, cid, key: history.get((cid, key), {}))
    attrs["contractNotesReceived"] = Signal("QVariantMap", bool)
    attrs["contractNotesFailed"] = Signal(str, str, int, str, bool)
    attrs["loadContractNotes"] = Slot(str, str, name="loadContractNotes")(lambda self, cid, key: reads.append((cid, key)))
    attrs["saveContractNotes"] = Slot(str, str, int, str, name="saveContractNotes")(
        lambda self, cid, key, rev, note: writes.append((cid, key, rev, note)))


source = Path(__file__).with_name("test_report_tabs.py")
prefix = source.read_text(encoding="utf-8").partition("def run_tests():")[0]
marker = 'Lem = type("Lem", (QObject,), attrs)'
assert prefix.count(marker) == 1
exec(compile(prefix.replace(marker, "install(attrs)\n" + marker), str(source), "exec"))


def row(note="", revision=0):
    return {"id": "membership", "contract_id": "contract", "external_id": "PCVE-001", "attached": True,
            "notes_revision": revision, "contract_notes": note, "reports": [], "details": {}}


def enter(note, role="tech", online=True):
    js('page = "catalog"')
    values["chargers"] = [row(note, 1)]
    values["user"] = dict(values["user"], id="account", role=role)
    values["authenticated"] = online
    values["current"] = dict(values["current"], id="report", charger_id="membership", contract_id="contract")
    lem.chargersChanged.emit()
    lem.changed.emit()
    js('page = "report"')
    QTest.qWait(50)


def checks():
    try:
        enter("Chave no portão", online=False)
        assert js("contractNotesDialog.visible && contractNotesDialog.note === 'Chave no portão'")
        assert not find_visual(w.contentItem(), "editContractNotes").isVisible()
        js("contractNotesDialog.dismiss()")
        QTest.qWait(100)
        history["contract", "membership"] = row("Instruções do histórico", 2)
        values["chargers"] = []
        lem.changed.emit()
        js("showReportNotes(false)")
        assert js("contractNotesDialog.visible && !contractNotesDialog.attached && contractNotesDialog.revision === 2")
        assert js("contractNotesDialog.note === 'Instruções do histórico'")
        js("contractNotesDialog.dismiss()")
        QTest.qWait(100)
        history.clear()
        enter("")
        assert not js("contractNotesDialog.visible")
        # A server note added since the cached listing opens immediately on receipt.
        lem.contractNotesReceived.emit(row("Nova instrução", 2), False)
        assert js("contractNotesDialog.visible")
        js("contractNotesDialog.dismiss()")
        QTest.qWait(100)
        lem.contractNotesReceived.emit(row("Resposta tardia", 3), False)
        assert not js("contractNotesDialog.visible")
        js("showReportNotes(false)")
        assert js("contractNotesDialog.visible")
        js("contractNotesDialog.dismiss()")
        QTest.qWait(100)
        enter("Inicial", "admin")
        assert not js("contractNotesDialog.visible")
        js('page = "catalog"')
        QTest.qWait(100)
        trigger("contractNotes_membership")
        trigger("editContractNotes")
        editor = find_visual(w.contentItem(), "contractNotesText")
        editor.setProperty("text", "Texto do administrador")
        trigger("saveContractNotes")
        assert writes == [("contract", "membership", 1, "Texto do administrador")], writes
        lem.contractNotesFailed.emit("contract", "membership", 409, "Notas alteradas", True)
        lem.contractNotesReceived.emit(row("Texto concorrente", 2), False)
        assert editor.property("text") == "Texto do administrador"
        assert not js("contractNotesDialog.saving") and not find_visual(w.contentItem(), "saveContractNotes").property("enabled")
        # Another membership can share the PCVE ID. Ignore its late response.
        lem.contractNotesReceived.emit(dict(row("Outro contrato", 3), contract_id="outro"), False)
        assert js("contractNotesDialog.note === 'Texto concorrente'")
        trigger("cancelContractNotes")
        assert not js("contractNotesDialog.editing") and editor.property("text") == "Texto concorrente"
        js("contractNotesDialog.dismiss()")
        assert not [e for e in errors if "TypeError" in e or "ReferenceError" in e], errors
        print("PASS technician offline opening, menu and refresh, admin editing, conflict recovery and contract isolation.")
        app.exit(0)
    except Exception:
        traceback.print_exc()
        app.exit(1)


QTimer.singleShot(2400, checks)
rc = app.exec()
delete(engine)
sys.exit(rc)
