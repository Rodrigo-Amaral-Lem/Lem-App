# SPDX-License-Identifier: GPL-3.0-only
# Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

"""Real QML checks: fixed first header and a complete virtualized preview."""
from pathlib import Path
from shiboken6 import delete
source = Path(__file__).with_name("test_report_tabs.py")
prefix = source.read_text().partition("def run_tests():")[0]
prefix = prefix.replace('Lem = type("Lem", (QObject,), attrs)', '''
preview_calls=[]
def previewExcel(self,sheet,row,mapping,primary,secondary,issuer,revision): preview_calls.append((sheet,row,mapping,primary,secondary,issuer,revision))
attrs["previewExcel"]=Slot(str,int,"QVariantMap",str,str,str,int)(previewExcel)
Lem = type("Lem", (QObject,), attrs)
''')
exec(compile(prefix, str(source), "exec"), globals())
values["authenticated"] = True
records = [{"external_id": f"STATION-{i:04d}", "details": {"source": {
    "Station ID": f"STATION-{i:04d}", "Morada": "Rua do Exemplo " + "longa " * 12,
    "Modelo": "DC60", "CCDR": "Norte", "Tomadas / Fichas": "2x Mennekes Type 2", "Limitação de Potência (kVA)": "0"
}}} for i in range(75)]
values["importInfo"] = {"batch_id": "preview-test", "sheets": [{"name": "Stations", "recommended_mapping": {"header_row": 1}}]}
values["importPreview"] = {"count": 75, "records": records, "errors": []}
lem.changed.emit()
js('splashActive=false;page="admin";adminArea.currentIndex=1')
for width, height in [(320,700), (390,844), (1050,850)]:
    w.setWidth(width); w.setHeight(height); QTest.qWait(150)
    assert js('typeof headerRow === "undefined"')
    assert js('typeof mapRepeater === "undefined"')
    assert js('importPreviewList.count') == 75
    trigger("previewImportButton")
    assert preview_calls[-1] == ("Stations", 1, {}, "", "", "Lem", 0)
    js('importPreviewList.positionViewAtEnd()'); QTest.qWait(250)
    assert js('importPreviewList.itemAtIndex(74).modelData.external_id') == "STATION-0074"
    assert js('importPreviewList.itemAtIndex(74).width <= importPreviewList.width')
    assert js('win.previewDetails(importPreviewList.itemAtIndex(74).modelData).length') == 5
    assert js('win.previewDetails(importPreviewList.itemAtIndex(74).modelData).some(row => row.value === "2x AC Type 2")')
    assert js('win.chargerMatches({details:{connectors:"Mennekes Type 2"}}, "ac type 2")')
    assert js('win.chargerMatches({details:{connectors:"AC Type 2"}}, "mennekes type 2")')
    assert find_visual(w.contentItem(), "confirmImportButton").property("enabled")
values["importPreview"] = {"count": 75, "sample": records[:10], "errors": []}
lem.changed.emit(); QTest.qWait(50)
assert not find_visual(w.contentItem(), "confirmImportButton").property("enabled")
values["importPreview"] = {"count": 75, "records": records, "errors": [{"row": 78, "id": "bad", "error": "ID duplicado"}]}
lem.changed.emit(); QTest.qWait(50)
assert not find_visual(w.contentItem(), "confirmImportButton").property("enabled")
values["importPreview"]["errors"] = []
lem.changed.emit(); QTest.qWait(50)
js('adminScroll.contentY=importPreviewList.mapToItem(adminScroll.contentItem,0,0).y')
QTest.qWait(100)
assert w.grabWindow().save(str(PREVIEW_DIR / "import-all-stations.png"))
assert not errors, errors
print("PASS: 75 records, final record accessible, every source field, first header fixed, mobile/desktop layout, old-server and error safeguards")
print("Preview:", PREVIEW_DIR)
delete(engine)
