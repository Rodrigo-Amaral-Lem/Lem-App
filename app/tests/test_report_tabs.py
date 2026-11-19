# SPDX-License-Identifier: GPL-3.0-only
# Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

"""Isolated QML navigation regression; does not connect to the API or use real drafts.
Requires PySide6 (validated with 6.10.2). Run: python app/tests/test_report_tabs.py
For a narrow viewport: python app/tests/test_report_tabs.py report mobile
The backend is a mock; this is not a C++/server integration test.
"""

import os, sys, json, copy, re

os.environ["QT_QPA_PLATFORM"] = "offscreen"
os.environ["QT_QUICK_BACKEND"] = "software"
from PySide6.QtCore import QObject, Property, Signal, Slot, QUrl, QTimer, Qt, QPoint, QMetaObject
from PySide6.QtTest import QTest
from PySide6.QtGui import QGuiApplication, QPalette, QColor, QImage
from PySide6.QtQml import QQmlApplicationEngine, QQmlExpression, QQmlEngine
from PySide6.QtQuickControls2 import QQuickStyle
from pathlib import Path
import tempfile

PROJECT_ROOT = Path(__file__).resolve().parents[2]
PREVIEW_DIR = Path(tempfile.mkdtemp(prefix="lem-tabs-"))
app = QGuiApplication(sys.argv)
app.setApplicationVersion("0.1.32")
QQuickStyle.setStyle("Fusion")
app.styleHints().setColorScheme(Qt.ColorScheme.Dark)
p = QPalette()
for role in [QPalette.ColorRole.Window, QPalette.ColorRole.Base, QPalette.ColorRole.Button]:
    p.setColor(role, QColor("#161616"))
for role in [QPalette.ColorRole.WindowText, QPalette.ColorRole.Text, QPalette.ColorRole.ButtonText]:
    p.setColor(role, QColor("#ffffff"))
app.setPalette(p)
values = {
    "desktopCorrections": True,
    "desktopExports": True,
    "desktopStationManagement": False,
    "stations": [],
    "stationCatalogueLoaded": False,
    "stationCatalogueLoading": False,
    "stationCatalogueMessage": "",
    "stationEditResult": {},
    "contractExport": {},
    "passwordReset": {},
    "pendingReports": 1,
    "operationStatus": {
        "active": False,
        "title": "Sem transferências em curso",
        "progress": 0,
        "indeterminate": False,
    },
    "operationTransfers": [],
    "clientCompatibility": {},
    "reportPresentationSave": {},
    "saving": False,
    "localSaveError": False,
    "authenticated": False,
    "busy": False,
    "copyingGroup": "",
    "attachingFiles": False,
    "syncing": False,
    "downloadingPhotos": False,
    "currentReportDownloadingPhotos": False,
    "photoDownloadState": "A descarregar fotografias: 0 / 1",
    "refreshing": False,
    "syncState": "A enviar em segundo plano",
    "photoCacheMB": 0.0,
    "message": "DEMONSTRAÇÃO LOCAL • dados fictícios; sem ligação ao servidor.",
    "server": "https://lem.rodrod.pt",
    "user": {"id": "demo", "name": "Técnico de demonstração", "role": "admin"},
    "users": [{"id": "demo", "label": "Técnico de demonstração"}],
    "contracts": [{"id": "contract", "name": "2026_1ºS_Exemplo_TRE"}],
    "chargers": [
        {
            "id": "charger",
            "external_id": "PCVE-DEMO-001",
            "details": {
                "model": "DC 60 kW",
                "address": "Local de demonstração",
                "serial": "000012",
                "cpe": "CPE-DEMO",
            },
            "reports": [],
        }
    ],
    "current": {
        "id": "report-demo",
        "owner_id": "demo",
        "status": "draft",
        "dirty": True,
        "snapshot": {"charger": {"external_id": "PCVE-DEMO-001", "model": "DC 60 kW"}},
        "payload": {"answers": {}},
        "photos": [],
    },
    "sections": json.loads((PROJECT_ROOT / "shared/checklist-v1.json").read_text())["sections"],
    "importInfo": {},
    "importPreview": {},
}
values["photosByCheckpoint"] = {}
attrs = {"currentChanged": Signal(), "photosChanged": Signal(), "changed": Signal(), "chargersChanged": Signal(), "operationChanged": Signal(), "compatibilityChanged": Signal()}
for name, value in values.items():
    typ = (
        bool
        if isinstance(value, bool)
        else (
            str
            if isinstance(value, str)
            else (
                "QVariantMap"
                if isinstance(value, dict)
                else float if isinstance(value, float) else "QVariantList"
            )
        )
    )
    if name == "pendingReports":
        typ = int
    attrs[name] = Property(
        typ,
        lambda self, n=name: values[n],
        notify=attrs[
            "operationChanged" if name in ["operationStatus", "operationTransfers"]
            else "chargersChanged" if name == "chargers"
            else "currentChanged" if name == "current"
            else "photosChanged" if name == "photosByCheckpoint" else "changed"
        ],
    )


def pendingCount(self):
    return 1


def contractId(self):
    return "contract"


attrs["pendingCount"] = Slot(result=int)(pendingCount)
attrs["contractId"] = Slot(result=str)(contractId)
attrs["canSaveReportPresentation"] = Property(bool, lambda self:
    values['authenticated'] and values['user'].get('role') == 'admin'
    and not any(values[key] for key in ('busy', 'syncing', 'refreshing')), notify=attrs['changed'])
attrs["refreshStations"] = Slot()(lambda self: None)
attrs["useDefaultServer"] = Slot()(lambda self: None)
attrs["prepareExport"] = Slot(str, result=bool)(lambda self, kind: True)
qve_sources = [{"id":"other-report", "label":"PCVE-OUTRO · Concluído"}]
qve_copy_calls = []
signature_calls = []
def qveCopySources(self): return qve_sources
attrs["qveCopySources"] = Slot(result="QVariantList")(qveCopySources)
def copyQveFrom(self, rid): qve_copy_calls.append(rid)
attrs["copyQveFrom"] = Slot(str)(copyQveFrom)
measurement_copy_calls = []
def measurementCopySources(self): return qve_sources
def copyMeasurementsFrom(self, rid): measurement_copy_calls.append(rid)
def attachMany(self, checkpoint, urls): pass
attrs["measurementCopySources"] = Slot(result="QVariantList")(measurementCopySources)
attrs["copyMeasurementsFrom"] = Slot(str)(copyMeasurementsFrom)
attrs["attachMany"] = Slot(str, "QVariantList")(attachMany)

def saveSignature(self, checkpoint, png):
    signature_calls.append((checkpoint, png))
    return True
attrs["saveSignature"] = Slot(str, str, result=bool)(saveSignature)



def answer(self, checkpoint, value, notes):
    a = values["current"]["payload"]["answers"].setdefault(checkpoint, {})
    a.update(value=value, notes=notes)
    if value in ("NA", "N/A", "Não Aplicável"): a.pop("assessment", None)
    self.changed.emit()


def beginNotesEdit(self, key):
    a = values["current"]["payload"]["answers"].get(key, {})
    if a.get("suggested_notes") and a.get("suggested_notes") == a.get("notes"):
        a["notes"] = ""
        a.pop("suggested_notes", None)
        self.changed.emit()
attrs["beginNotesEdit"] = Slot(str)(beginNotesEdit)

attrs["answer"] = Slot(str, str, str)(answer)
def setAssessment(self, key, status):
    a = values["current"]["payload"]["answers"].setdefault(key, {})
    if a.get("value") in ("NA", "N/A", "Não Aplicável"): a["value"] = ""
    a.setdefault("value", "")
    a.setdefault("notes", "")
    a["assessment"] = status
    self.changed.emit()
attrs["setAssessment"] = Slot(str, str)(setAssessment)
camera_calls = []
removal_calls = []


def removePhoto(self, photo_id):
    removal_calls.append(photo_id)
    values["current"]["photos"] = [p for p in values["current"]["photos"] if p["id"] != photo_id]
    self.changed.emit()


attrs["removePhoto"] = Slot(str)(removePhoto)
delete_report_calls = []


def deleteReport(self, rid):
    delete_report_calls.append(rid)


attrs["deleteReport"] = Slot(str)(deleteReport)
finish_calls = []


def finish(self):
    finish_calls.append(True)


attrs["finish"] = Slot()(finish)


def takePhoto(self, checkpoint):
    camera_calls.append(checkpoint)


attrs["takePhoto"] = Slot(str)(takePhoto)


def photoDownloading(self, pid):
    return False


attrs["photoDownloading"] = Slot(str, result=bool)(photoDownloading)
fetch_calls = []


def fetchPhoto(self, pid):
    fetch_calls.append(pid)


attrs["fetchPhoto"] = Slot(str)(fetchPhoto)


def cleanPhotoCache(self):
    pass


attrs["cleanPhotoCache"] = Slot()(cleanPhotoCache)
clear_copy_calls = []


def clearLocalCopies(self):
    clear_copy_calls.append(True)


attrs["clearLocalCopies"] = Slot()(clearLocalCopies)
download_calls = []


def photoDownloadSummary(self):
    return {"total": 1, "missing": 1, "bytes": 2048, "unknown": 0}


attrs["photoDownloadSummary"] = Slot(result="QVariantMap")(photoDownloadSummary)


def downloadReportPhotos(self):
    download_calls.append(values["current"]["id"])


attrs["downloadReportPhotos"] = Slot()(downloadReportPhotos)
boundary_calls = []


def reportSyncBoundary(self):
    boundary_calls.append(True)


attrs["reportSyncBoundary"] = Slot()(reportSyncBoundary)
ownership_calls = []


def loadReportOwners(self):
    ownership_calls.append("owners")


def transferReport(self, owner):
    ownership_calls.append(("transfer", owner))


def viewServerReport(self):
    ownership_calls.append("server")


def copyLocalReport(self):
    ownership_calls.append("copy")


def useServerReport(self):
    ownership_calls.append("use-server")


attrs["loadReportOwners"] = Slot()(loadReportOwners)
attrs["transferReport"] = Slot(str)(transferReport)
attrs["viewServerReport"] = Slot()(viewServerReport)
attrs["copyLocalReport"] = Slot()(copyLocalReport)
attrs["useServerReport"] = Slot()(useServerReport)
def reportBrands(self):
    return json.loads((PROJECT_ROOT / "shared/branding/report-logos.json").read_text(encoding="utf-8"))

attrs["reportBrands"] = Slot(result="QVariantList")(reportBrands)
def invalidatePreview(self):
    pass

attrs["invalidatePreview"] = Slot()(invalidatePreview)
def connectorText(self, text):
    return re.sub(r'\bMennekes\s+(?:Type|Tipo)\s*2\b', 'AC Type 2', text, flags=re.I)
attrs['connectorText'] = Slot(str, result=str)(connectorText)
Lem = type("Lem", (QObject,), attrs)
lem = Lem()
notified_current = None
notified_photos = None

def notify_report_models():
    global notified_current, notified_photos
    current = values["current"]
    if current != notified_current:
        notified_current = copy.deepcopy(current)
        lem.currentChanged.emit()
    photos = current.get("photos", [])
    if photos != notified_photos:
        notified_photos = copy.deepcopy(photos)
        groups = {}
        for photo in photos:
            groups.setdefault(photo["checkpoint"], []).append(copy.deepcopy(photo))
        values["photosByCheckpoint"] = groups
        lem.photosChanged.emit()

lem.changed.connect(notify_report_models)
notify_report_models()
# Mock data is mutated directly by these navigation tests; production emits the
# narrower signal only when its catalogue projection actually changes.
lem.changed.connect(lem.chargersChanged.emit)
engine = QQmlApplicationEngine()
engine.setInitialProperties(
    {"startupDraw": 0.5, "logoSource": QUrl.fromLocalFile(str(PROJECT_ROOT / "shared/branding/lem-logo-tight.svg"))}
)
engine.rootContext().setContextProperty("lem", lem)
errors = []
engine.warnings.connect(lambda es: errors.extend(str(e.toString()) for e in es))
engine.load(QUrl.fromLocalFile(str((PROJECT_ROOT / "app/qml/Main.qml").resolve())))
if not engine.rootObjects():
    print("\n".join(errors))
    sys.exit(1)
w = engine.rootObjects()[0]
page = sys.argv[1] if len(sys.argv) > 1 else "report"
w.setProperty("page", page)
if page == "report":
    w.setProperty("selectedSection", next(i for i, section in enumerate(values["sections"]) if section["id"] == "EST"))
if "mobile" in sys.argv:
    w.setWidth(390)
    w.setHeight(844)


def find_visual(item, name):
    if item.objectName() == name:
        return item
    for c in item.childItems():
        r = find_visual(c, name)
        if r is not None:
            return r
    return None


def js(expression):
    result = QQmlExpression(QQmlEngine.contextForObject(w), w, expression)
    value, undefined = result.evaluate()
    assert not result.hasError(), result.error().toString()
    return value


def trigger(name):
    item = find_visual(w.contentItem(), name)
    assert item is not None, name
    assert item.isVisible(), name + " is hidden"
    assert item.property("enabled"), name + " is disabled"
    assert QMetaObject.invokeMethod(item, "clicked", Qt.ConnectionType.DirectConnection), name
    QTest.qWait(50)
    return item


def pointer_click(name):
    item = find_visual(w.contentItem(), name)
    assert item is not None and item.isVisible(), name
    point = item.mapToScene(QPoint(int(item.width() / 2), int(item.height() / 2)))
    QTest.mouseClick(
        w,
        Qt.MouseButton.LeftButton,
        Qt.KeyboardModifier.NoModifier,
        QPoint(int(point.x()), int(point.y())),
    )
    QTest.qWait(80)


def win_notes_text():
    return find_visual(w.contentItem(), "notes_EST.01").property("text")


def run_tests():
    try:
        import copy

        assert w.property("title") == "Lem"
        assert len(splash_checks) == 2 and not js("splashActive")
        mobile = "mobile" in sys.argv
        if mobile:
            w.setProperty("androidCamera", True)
        account = find_visual(w.contentItem(), "accountButton")
        assert account.isVisible() and "Sem sessão iniciada" in account.property(
            "text"
        ), "Cached identity must not look signed in"
        assert find_visual(w.contentItem(), "accountStatusIcon").property("kind") == "warning"
        header = find_visual(w.contentItem(), "appHeader")
        assert account.width() <= 48 and header.height() <= 60
        logo = find_visual(header, "headerLogo")
        assert logo.property("sourceSize").height() > 0 and logo.property("paintedWidth") > 0
        origin = logo.mapToItem(header, QPoint(0, 0))
        assert origin.x() >= 6 and origin.y() >= 4, "Logo needs clearance from the top and leading edges"
        assert header.height() - origin.y() - logo.height() >= 6, "Logo needs clearance above the toolbar rule"
        trigger("accountButton")
        assert js("loginDialog.opened")
        trigger("clearLocalCopiesButton")
        assert js("clearLocalCopiesDialog.opened")
        trigger("cancelClearLocalCopies")
        assert clear_copy_calls == []
        trigger("clearLocalCopiesButton")
        trigger("confirmClearLocalCopies")
        assert clear_copy_calls == [True]
        js("loginDialog.close()")
        QTest.qWait(40)
        values["authenticated"] = True
        lem.changed.emit()
        QTest.qWait(40)
        assert "Sessão iniciada" in account.property("text") and values["user"][
            "name"
        ] in account.property("text")
        assert find_visual(w.contentItem(), "accountStatusIcon").property("kind") == "check"
        values["authenticated"] = False
        lem.changed.emit()
        QTest.qWait(40)
        assert "Sem sessão iniciada" in account.property("text")
        values["saving"] = True
        lem.changed.emit()
        QTest.qWait(30)
        assert "A guardar" in find_visual(w.contentItem(), "reportSaveState").property("text")
        values["saving"] = False
        values["localSaveError"] = True
        lem.changed.emit()
        QTest.qWait(30)
        assert "Erro ao guardar" in find_visual(w.contentItem(), "reportSaveState").property("text")
        values["localSaveError"] = False
        lem.changed.emit()
        # Ownership and administrator-only closed report deletion, with confirmation.
        assert js("canDeleteReport")
        trigger("reportOptionsButton")
        trigger("deleteReportAction")
        assert js("deleteReportDialog.opened")
        trigger("cancelDeleteReport")
        assert delete_report_calls == []
        trigger("reportOptionsButton")
        trigger("deleteReportAction")
        trigger("confirmDeleteReport")
        assert delete_report_calls == ["report-demo"]
        old_user = values["user"].copy()
        values["user"]["role"] = "tech"
        values["current"]["status"] = "closed"
        lem.changed.emit()
        QTest.qWait(30)
        assert not js("canDeleteReport")
        values["current"]["status"] = "draft"
        values["current"]["owner_id"] = "another"
        lem.changed.emit()
        QTest.qWait(30)
        assert not js("canDeleteReport")
        values["current"]["owner_id"] = "demo"
        lem.changed.emit()
        QTest.qWait(30)
        assert js("canDeleteReport")
        values["user"] = old_user
        values["current"]["status"] = "closed"
        lem.changed.emit()
        QTest.qWait(30)
        assert js("canDeleteReport")
        values["current"]["status"] = "draft"
        lem.changed.emit()
        # Real press/release events reproduce the outside-press/reopen bug.
        if mobile:
            js('page="catalog"')
            QTest.qWait(50)
            for _ in range(3):
                pointer_click("navigationButton")
                assert js("navMenu.visible")
                pointer_click("navigationButton")
                assert not js("navMenu.visible")
            pointer_click("navigationButton")
            QTest.keyClick(w, Qt.Key.Key_Escape)
            QTest.qWait(60)
            assert not js("navMenu.visible")
            pointer_click("navigationButton")
            QTest.mouseClick(
                w,
                Qt.MouseButton.LeftButton,
                Qt.KeyboardModifier.NoModifier,
                QPoint(8, int(w.height() / 2)),
            )
            QTest.qWait(70)
            assert not js("navMenu.visible")
            js('page="report"')
            QTest.qWait(50)
        for _ in range(3):
            pointer_click("reportOptionsButton")
            assert js("reportMenu.visible")
            pointer_click("reportOptionsButton")
            assert not js("reportMenu.visible")
        assert not js(
            'Array.from({length:reportMenu.count},(_,i)=>reportMenu.itemAt(i).text).includes("Conta")'
        )
        js("statusDialog.open()")
        QTest.qWait(50)
        values["operationStatus"] = {
            "active": True,
            "title": "Enviar fotografia 2 / 4 · EST.01",
            "progress": 0.25,
            "indeterminate": False,
        }
        values["operationTransfers"] = [
            {
                "title": "Enviar fotografia",
                "context": "PCVE-DEMO · EST.01",
                "progress": 0.5,
                "done": 1024,
                "total": 2048,
                "direction": "upload",
                "indeterminate": False,
                "waiting": False,
            }
        ]
        lem.operationChanged.emit()
        QTest.qWait(50)
        assert find_visual(w.contentItem(), "operationProgressBar").property("value") == 0.25
        assert find_visual(w.contentItem(), "operationProgressText").property("text") == "25%"
        values["operationStatus"]["indeterminate"] = True
        lem.operationChanged.emit()
        QTest.qWait(30)
        assert find_visual(w.contentItem(), "operationProgressBar").property("indeterminate")
        js("statusDialog.close()")
        values["operationTransfers"] = []
        values["operationStatus"] = {
            "active": False,
            "title": "Concluído",
            "progress": 1,
            "indeterminate": False,
        }
        lem.operationChanged.emit()
        expected_items = sum(len(section["items"]) for section in values["sections"])
        assert js("totalItems()") == expected_items
        est_index = next(i for i, section in enumerate(values["sections"]) if section["id"] == "EST")
        assert js(f"sectionPanels.itemAt({est_index}).checkpoints.count") == len(values["sections"][est_index]["items"])
        assert js(
            "sectionPanels.itemAt(0).checkpoints===null"
        ), "Inactive groups must not allocate all question controls"
        optional_items = sum(bool(item.get("optional")) for section in values["sections"] for item in section["items"])
        assert js("totalAnswered()") == optional_items
        assert js("pendingItems().length") == expected_items - optional_items
        notes = find_visual(w.contentItem(), "notes_EST.01")
        assert not notes.isVisible(), "Optional notes must start collapsed"
        assert not find_visual(w.contentItem(), "answer_EST.01_OK").property("checked")
        clear = find_visual(w.contentItem(), "clearAnswer_EST.01")
        assert clear.isVisible() and not clear.property("enabled")

        def check_answer_row():
            names = [
                "answer_EST.01_OK",
                "answer_EST.01_NOK",
                "answer_EST.01_NA",
                "clearAnswer_EST.01",
            ]
            buttons = [find_visual(w.contentItem(), n) for n in names]
            ys = [b.mapToScene(QPoint(0, 0)).y() for b in buttons]
            assert max(ys) - min(ys) < 1, "All four response actions must share one row"
            minimum = 48 if mobile else 44
            assert all(b.width() >= minimum and b.height() >= minimum for b in buttons)

        check_answer_row()
        if mobile:
            width = w.width()
            w.setWidth(320)
            QTest.qWait(60)
            check_answer_row()
            w.setWidth(width)
            QTest.qWait(60)
        trigger("answer_EST.01_NOK")
        assert notes.isVisible(), "NOK must open required observations"
        assert find_visual(w.contentItem(), "toggleNotes_EST.01").property(
            "enabled"
        ), "Mandatory observations can be cleared after confirmation"
        assert "Falta observação NOK" in js(f"itemIssues(lem.sections[{est_index}].items[0]).join()")
        notes.forceActiveFocus()
        for ch in "Teste":
            QTest.keyClick(w, ord(ch))
        QTest.qWait(50)
        assert values["current"]["payload"]["answers"]["EST.01"]["notes"] == "Teste"
        assert "Falta observação NOK" not in js(f"itemIssues(lem.sections[{est_index}].items[0]).join()")
        assert (
            find_visual(w.contentItem(), "photoRequirement_EST.01").property("text")
            == "Fotografia obrigatória em falta"
        )
        trigger("answer_EST.01_NA")
        assert js(f"itemIssues(lem.sections[{est_index}].items[0]).length") == 0, "NA exempts photograph"
        trigger("answer_EST.01_OK")
        assert js("pendingItems().length") == expected_items - optional_items, "Answered item still needs required evidence"
        values["current"]["photos"] = [
            {"checkpoint": "EST.01", "id": "photo-demo", "uploaded": True}
        ]
        lem.changed.emit()
        QTest.qWait(50)
        assert js("pendingItems().length") == expected_items - optional_items - 1
        assert js("totalAnswered()") == optional_items + 1
        # Clearing only removes the status, retaining the observations and attached evidence.
        before_photos = copy.deepcopy(values["current"]["photos"])
        trigger("clearAnswer_EST.01")
        QTest.qWait(40)
        assert values["current"]["payload"]["answers"]["EST.01"] == {"value": "", "notes": "Teste"}
        assert values["current"]["photos"] == before_photos
        assert not clear.property("enabled")
        check_answer_row()
        trigger("answer_EST.01_OK")
        QTest.qWait(40)
        download = find_visual(w.contentItem(), "downloadPhoto_photo-demo")
        assert download.isVisible() and download.property("symbol") == "download"
        trigger("downloadPhoto_photo-demo")
        assert fetch_calls == ["photo-demo"]
        fixture = QImage(64, 48, QImage.Format.Format_RGB32)
        fixture.fill(QColor("#135f58"))
        fixture_path = PREVIEW_DIR / "evidence.png"
        assert fixture.save(str(fixture_path))
        values["current"]["photos"][0]["path"] = QUrl.fromLocalFile(str(fixture_path)).toString()
        lem.changed.emit()
        QTest.qWait(100)
        enlarge = find_visual(w.contentItem(), "enlargePhoto_photo-demo")
        remove = find_visual(w.contentItem(), "removePhoto_photo-demo")
        assert enlarge.isVisible() and enlarge.property("symbol") == "zoom"
        assert remove.property("symbol") == "trash" and remove.property("destructive")
        assert abs(enlarge.y() - remove.y()) < 1 and enlarge.width() >= 44 and remove.height() >= 44
        assert not find_visual(w.contentItem(), "downloadPhoto_photo-demo").isVisible()
        trigger("enlargePhoto_photo-demo")
        assert js("photoPreview.opened")
        trigger("closePhotoPreview")
        assert not js("photoPreview.opened")
        trigger("removePhoto_photo-demo")
        assert js("removePhotoDialog.opened")
        trigger("cancelRemovePhoto")
        assert len(values["current"]["photos"]) == 1 and removal_calls == []
        trigger("removePhoto_photo-demo")
        trigger("confirmRemovePhoto")
        assert removal_calls == ["photo-demo"] and values["current"]["photos"] == []
        assert js("pendingItems().length") == expected_items - optional_items
        values["current"]["photos"] = [
            {"checkpoint": "EST.01", "id": "photo-demo", "uploaded": True}
        ]
        lem.changed.emit()
        QTest.qWait(40)
        snapshot = copy.deepcopy(values["current"])
        js("selectSection(0)")
        previous_boundaries = len(boundary_calls)
        for i, section in enumerate(values["sections"]):
            js("selectSection(" + str(i) + ")")
            QTest.qWait(50)
            assert w.property("selectedSection") == i
            for j, g in enumerate(values["sections"]):
                assert find_visual(w.contentItem(), "sectionPanel_" + g["id"]).isVisible() == (
                    i == j
                )
            assert values["current"] == snapshot, "Navigation changed report data"
            assert (
                len(boundary_calls) == previous_boundaries + i
            ), "A group change should release one photo batch"
        last = find_visual(w.contentItem(), "sectionTab_FIN")
        last.forceActiveFocus()
        QTest.keyClick(w, Qt.Key.Key_Left)
        QTest.qWait(40)
        assert w.property("selectedSection") == 5
        js(f"selectSection({est_index})")
        QTest.qWait(50)
        scroll = find_visual(w.contentItem(), "reportScroll")
        scroll.property("contentItem").setProperty("contentY", 300)
        js("selectSection(4)")
        QTest.qWait(50)
        js(f"selectSection({est_index})")
        QTest.qWait(50)
        assert (
            abs(scroll.property("contentItem").property("contentY") - 300) < 2
        ), "Group scroll position lost"
        assert find_visual(w.contentItem(), "notes_EST.01").property("text") == "Teste"
        # Optional observations can be dismissed, with confirmation before deleting text.
        trigger("toggleNotes_EST.01")
        assert js("removeNotesDialog.opened")
        trigger("cancelRemoveNotes")
        assert win_notes_text() == "Teste"
        trigger("toggleNotes_EST.01")
        trigger("confirmRemoveNotes")
        notes = find_visual(w.contentItem(), "notes_EST.01")
        assert (
            not notes.isVisible()
            and values["current"]["payload"]["answers"]["EST.01"]["notes"] == ""
        )
        assert values["current"]["payload"]["answers"]["EST.01"]["value"] == "OK"
        trigger("toggleNotes_EST.01")
        assert notes.isVisible()
        trigger("toggleNotes_EST.01")
        assert not notes.isVisible(), "Empty optional notes close without confirmation"
        trigger("toggleNotes_EST.01")
        notes.forceActiveFocus()
        for ch in "Teste":
            QTest.keyClick(w, ord(ch))
        take = find_visual(w.contentItem(), "addPhoto_EST.01")
        choose = find_visual(w.contentItem(), "choosePhoto_EST.01")
        if mobile:
            assert take.property("text") == "Tirar fotografia"
            assert choose.isVisible()
            obs = find_visual(w.contentItem(), "toggleNotes_EST.01")
            assert take.property("symbol") == "camera" and choose.property("symbol") == "gallery"
            assert (
                abs(take.y() - choose.y()) < 1 and abs(take.y() - obs.y()) < 1
            ), "Three actions must share one row"
            trigger("addPhoto_EST.01")
            assert camera_calls == ["EST.01"]
            trigger("allGroupsButton")
            assert js("groupDialog.opened")
            js("groupDialog.close()")
            QTest.qWait(50)
            assert find_visual(w.contentItem(), "answer_EST.01_OK").height() >= 48
        else:
            assert take.property("text") == "Adicionar fotografia"
            assert not choose.isVisible()
        assert find_visual(w.contentItem(), "downloadAllReportPhotosButton").isVisible()
        trigger("downloadAllReportPhotosButton")
        assert js("photoDownloadDialog.opened")
        assert js("photoDownloadDialog.reportId") == "report-demo"
        js("photoDownloadDialog.close()")
        QTest.qWait(40)
        trigger("reportOptionsButton")
        trigger("downloadReportPhotosAction")
        assert js("photoDownloadDialog.opened")
        trigger("confirmDownloadReportPhotos")
        assert download_calls == ["report-demo"]
        values["downloadingPhotos"] = True
        lem.changed.emit()
        QTest.qWait(40)
        assert find_visual(w.contentItem(), "answer_EST.01_OK").property("enabled")
        assert find_visual(w.contentItem(), "operationStatusButton").property("primary")
        assert not find_visual(w.contentItem(), "downloadAllReportPhotosButton").property("enabled")
        values["downloadingPhotos"] = False
        lem.changed.emit()
        trigger("reviewReportButton")
        assert js("reviewDialog.opened")
        assert (
            find_visual(w.contentItem(), "reviewCard_EST.04").width() > 250
        ), "Review content collapsed"
        assert not find_visual(w.contentItem(), "finishReportButton").property("enabled")
        trigger("reviewJump_EST.04")
        assert not js("reviewDialog.opened")
        assert w.property("selectedSection") == est_index
        assert w.property("focusedCheckpoint") == "EST.04"
        assert scroll.property("contentItem").property("contentY") > 0
        # A complete report must enable the final action only after all requirements pass.
        for section in values["sections"]:
            for item in section["items"]:
                values["current"]["payload"]["answers"][item["id"]] = {"value": "NA", "notes": "Sem veículo disponível" if item.get("na_notes_required") else ""}
        # Closure also requires unconditional evidence and a matching signature.
        for section in values["sections"]:
            for item in section["items"]:
                if item.get("photo_always") or item.get("signature_required"):
                    photo_id = "complete-" + item["id"]
                    values["current"]["photos"].append({"id": photo_id, "checkpoint": item["id"], "mime": "image/png", "uploaded": True})
                    if item.get("signature_required"):
                        values["current"]["payload"]["answers"][item["id"]]["value"] = "Técnico de teste"
                        values["current"]["payload"].setdefault("signatures", {})[item["id"]] = {"signed_by": "Técnico de teste", "photo_id": photo_id}
        lem.changed.emit()
        QTest.qWait(50)
        assert js("pendingItems().length") == 0, js("JSON.stringify(pendingItems())")
        trigger("reviewReportButton")
        trigger("finishReportButton")
        assert finish_calls == [True]
        values["current"]["status"] = "closed"
        lem.changed.emit()
        QTest.qWait(50)
        assert not find_visual(
            w.contentItem(), "answer_EST.01_OK"
        ).isVisible(), "Completed report must use read-only labels"
        assert not find_visual(w.contentItem(), "addPhoto_EST.01").isVisible()
        assert not find_visual(w.contentItem(), "removePhoto_photo-demo").isVisible()
        values["current"]["status"] = "draft"
        values["syncing"] = True
        lem.changed.emit()
        QTest.qWait(50)
        assert find_visual(w.contentItem(), "answer_EST.01_OK").property("enabled")
        assert find_visual(w.contentItem(), "addPhoto_EST.01").property("enabled")
        assert find_visual(w.contentItem(), "reviewReportButton").property("enabled")
        assert not find_visual(w.contentItem(), "syncButton").property("enabled")
        values["syncing"] = False
        lem.changed.emit()
        # The status action stays available in one row, including long messages and busy states.
        baseline_footer = js("appFooter.height")
        if mobile:
            assert baseline_footer <= 56, baseline_footer
            assert js("header.height") <= 50  # 48 px targets plus style divider
            for name in [
                "operationStatusButton",
                "syncButton",
                "reviewReportButton",
                "accountButton",
                "reportBackButton",
            ]:
                button = find_visual(w.contentItem(), name)
                assert button.width() >= 48 and button.height() >= 48, name
            assert find_visual(w.contentItem(), "syncButton").property("text").startswith("Sinc.")
        if mobile:
            saved_width, saved_height = w.width(), w.height()
            w.setWidth(320)
            w.setHeight(640)
            QTest.qWait(60)
            assert js("appFooter.height") == baseline_footer
            for name in ["operationStatusButton", "syncButton", "reviewReportButton"]:
                button = find_visual(w.contentItem(), name)
                assert button.width() >= 48 and button.height() >= 48, name
                assert button.mapToScene(QPoint(0, 0)).x() >= 0
                assert button.mapToScene(QPoint(int(button.width()), 0)).x() <= w.width()
            w.setWidth(saved_width)
            w.setHeight(saved_height)
            QTest.qWait(60)
        values["message"] = "Mensagem de teste muito longa " * 20
        lem.changed.emit()
        js("showFeedback=true")
        QTest.qWait(50)
        assert js("appFooter.height") == baseline_footer
        if mobile:
            assert find_visual(w.contentItem(), "feedbackToast").isVisible()
            trigger("dismissFeedbackButton")
            assert not js("showFeedback")
        pointer_click("operationStatusButton")
        assert js("statusDialog.opened")
        js("statusDialog.close()")
        # In-flight feedback remains conspicuous without expanding the footer.
        values["operationStatus"] = {
            "active": True,
            "title": "Carregar dados do report",
            "progress": 0,
            "indeterminate": True,
        }
        lem.operationChanged.emit()
        values["busy"] = True
        lem.changed.emit()
        QTest.qWait(50)
        assert find_visual(w.contentItem(), "operationStatusButton").property("primary")
        assert js("operationActive")
        assert (
            find_visual(w.contentItem(), "operationStatus").property("text")
            == "Carregar dados do report"
        )
        assert js("appFooter.height") == baseline_footer
        status_button = find_visual(w.contentItem(), "operationStatusButton")
        assert status_button.property("enabled")
        trigger("operationStatusButton")
        assert js("statusDialog.opened")
        js("statusDialog.close()")
        icon = find_visual(w.contentItem(), "operationStatusIcon")
        start_rotation = icon.property("rotation")
        QTest.qWait(180)
        assert (
            icon.property("rotation") != start_rotation
        ), "Operation icon must rotate while active"
        values["busy"] = False
        values["operationStatus"]["active"] = False
        lem.operationChanged.emit()
        lem.changed.emit()
        QTest.qWait(60)
        assert not js("operationActive")
        stopped_rotation = icon.property("rotation")
        QTest.qWait(120)
        assert icon.property("rotation") == stopped_rotation, "Idle icon must stop rotating"
        values["current"]["_conflict"] = True
        values["current"]["_conflict_reason"] = "Propriedade alterada"
        lem.changed.emit()
        QTest.qWait(60)
        assert not js("editing")
        trigger("viewConflictServer")
        assert ownership_calls[-1] == "server"
        js("serverVersionDialog.close()")
        trigger("copyConflictReport")
        trigger("confirmCopyReport")
        assert ownership_calls[-1] == "copy"
        values["current"].pop("_conflict")
        values["current"].pop("_conflict_reason")
        values["current"]["revision"] = 3
        values["current"]["dirty"] = False
        values["users"] = [{"id": "other-tech", "label": "Outro técnico"}]
        lem.changed.emit()
        QTest.qWait(50)
        js("transferDialog.reportId=lem.current.id;lem.loadReportOwners();transferDialog.open()")
        QTest.qWait(50)
        trigger("confirmTransferReport")
        assert ownership_calls[-1] == ("transfer", "other-tech")
        values["busy"] = False
        values["current"] = snapshot
        lem.changed.emit()
        js(f"selectSection({est_index})")
        js("showFeedback=false")
        QTest.qWait(80)
        scroll.property("contentItem").setProperty("contentY", 0)
        QTest.qWait(80)
        assert not errors, errors
        path = str(PREVIEW_DIR / ("tabs" + ("-mobile" if mobile else "") + ".png"))
        assert w.grabWindow().save(path)
        print("Preview:", path)
        print(
            "PASS: photo removal/cancel/closed guards; one-tap answers; conditional NOK notes; evidence/NA validation; review navigation; guarded completion; all groups; retained answers/photos/scroll; camera routing; read-only view; visible loading; no QML warnings"
        )
        app.exit(0)
    except Exception:
        import traceback

        traceback.print_exc()
        app.exit(1)


splash_checks = []


def check_splash():
    assert js("splashActive") and js("startupSplash.visible")
    assert js("startupSplash.width") >= w.width() - 1
    assert js("startupSplash.height") >= w.height() - 1
    assert js("startupLogo.status") == 1
    assert w.grabWindow().pixelColor(5, 5).name() == "#ffffff"
    splash_checks.append(True)


QTimer.singleShot(100, check_splash)
QTimer.singleShot(1700, check_splash)
QTimer.singleShot(2400, run_tests)
rc = app.exec()
del engine
sys.exit(rc)
