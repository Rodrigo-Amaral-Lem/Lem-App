# SPDX-License-Identifier: GPL-3.0-only
# Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

"""Check the independent PCVE menu and desktop administration with real QML."""

from pathlib import Path

from shiboken6 import delete


def install_catalogue_fixture(attrs, values, Slot):
    details = {
        "serial": "000071", "manufacturer": "Fabricante verificado", "model": "Modelo X",
        "power": "60", "power_unit": "kVA", "latitude": 0, "connectors": "Mennekes Tipo 2",
        "source": {"Fabricante": "Fabricante importado", "Núcleo": "Parque Norte"},
        "extra.Zona": "Norte",
    }
    values["stations"] = [
        {"id": "station", "external_id": "PCVE-071", "updated": "rev-1", "details": details},
        {"id": "other-period", "external_id": "PCVE-099", "updated": "rev-1", "details": {}},
    ]
    values["stationCatalogueLoaded"] = True
    values["desktopStationManagement"] = True
    values["authenticated"] = True
    values["user"] = {"id": "tech-catalogue", "role": "tech", "name": "Técnico"}
    values["chargers"] = []

    def refresh_stations(self):
        refreshes.append(True)

    def save_station(self, station, updated, fields):
        writes.append((station, updated, fields))
        values["stationEditResult"] = {"id": station, "state": "error", "message": "Falha de teste"}
        self.changed.emit()

    def preview_stations(self, sheet, row, mapping):
        previews.append((sheet, row, mapping))
        values["importPreview"] = {
            "count": 1, "errors": [], "records": [{"external_id": "PCVE-NOVO", "details": {}}]}
        self.changed.emit()

    def commit_excel(self):
        commits.append(True)
        values["importInfo"] = {}
        values["importPreview"] = {}
        values["message"] = "Importação concluída. 1 criados, 0 atualizados."
        self.changed.emit()

    attrs["contractId"] = Slot(result=str, name="contractId")(lambda self: "")
    attrs["refreshStations"] = Slot(name="refreshStations")(refresh_stations)
    attrs["saveStation"] = Slot(str, str, "QVariantMap", name="saveStation")(save_station)
    attrs["previewStationExcel"] = Slot(str, int, "QVariantMap", name="previewStationExcel")(preview_stations)
    attrs["commitExcel"] = Slot(name="commitExcel")(commit_excel)


refreshes, writes, previews, commits = [], [], [], []
source = Path(__file__).with_name("test_report_tabs.py")
prefix, separator, _ = source.read_text(encoding="utf-8").partition("def run_tests():")
marker = 'Lem = type("Lem", (QObject,), attrs)'
assert separator and prefix.count(marker) == 1
prefix = prefix.replace(marker, "install_catalogue_fixture(attrs, values, Slot)\n" + marker)
exec(compile(prefix, str(source), "exec"))


def checks():
    try:
        js('page="catalog"; showFeedback=false')
        w.setWidth(1100)
        w.setHeight(770)
        pointer_click("pcveCatalogueButton")
        assert js('page === "pcves" && !contractChrome.visible')
        assert refreshes and js("pcveList.count") == 2
        assert not find_visual(w.contentItem(), "importPcves").isVisible()
        js('pcveSearch.text=" parque NORTE "')
        QTest.qWait(250)
        assert js("pcveList.count") == 1
        pointer_click("pcveCard_station")
        assert js("pcveDetailsDialog.opened")
        assert find_visual(w.contentItem(), "pcveDetailsId").property("text") == "PCVE-071"
        assert not find_visual(w.contentItem(), "editPcve").isVisible()
        for expected in ("000071", "60 kVA", "0", "AC Type 2", "Fabricante verificado", "Norte"):
            assert js(f'pcveDetailsDialog.currentFields.some(row => row.value === "{expected}")'), expected
        assert js('pcveDetailsDialog.excelFields.some(row => row.value === "Fabricante importado")')

        values["stations"][0]["details"]["serial"] = "000072"
        lem.changed.emit()
        assert js('pcveDetailsDialog.currentFields.some(row => row.value === "000072")')
        values["chargers"] = [{"id": "membership", "external_id": "PCVE-PROJECTED", "details": {}, "reports": []}]
        lem.changed.emit()
        assert js("pcveDetailsDialog.opened && pcveList.count === 1")
        js("pcveDetailsDialog.close()")

        js('pcveSearch.text="sem resultado"')
        QTest.qWait(250)
        assert "Nenhum PCVE" in find_visual(w.contentItem(), "pcveCatalogueStatus").property("text")
        trigger("clearPcveSearch")
        assert js("pcveList.count") == 2

        values["user"]["role"] = "admin"
        values["stations"].append(
            {"id": "unassigned", "external_id": "PCVE-SEM-CONTRATO", "updated": "rev-1", "details": {}})
        lem.changed.emit()
        assert js("pcveList.count") == 3
        trigger("pcveCard_station")
        trigger("editPcve")
        assert js("pcveEditDialog.opened")
        assert find_visual(w.contentItem(), "pcveEdit_model").property("text") == "Modelo X"
        assert find_visual(w.contentItem(), "pcveEdit_extra.Zona").property("text") == "Norte"
        assert not find_visual(w.contentItem(), "savePcve").property("enabled")
        js('pcveEditDialog.setField("model", "Modelo atualizado")')
        trigger("savePcve")
        assert writes[-1] == ("station", "rev-1", {"model": "Modelo atualizado"})
        assert js('pcveEditDialog.opened && pcveEditDialog.edits.model === "Modelo atualizado"')
        assert find_visual(w.contentItem(), "pcveEditStatus").property("text") == "Falha de teste"
        assert w.grabWindow().save(str(PREVIEW_DIR / "desktop-pcve-edit.png"))
        values["stations"][0]["updated"] = "rev-2"
        values["stations"][0]["details"]["model"] = "Modelo atualizado"
        values["stationEditResult"] = {"id": "station", "state": "saved"}
        lem.changed.emit()
        QTest.qWait(100)
        assert not js("pcveEditDialog.opened")

        trigger("importPcves")
        assert js("pcveImportDialog.opened")
        values["importInfo"] = {"station_catalogue": True, "batch_id": "local-import", "sheets": [{
            "name": "Parque", "sample": [["ID", "Modelo", "Zona operacional"]]}]}
        lem.changed.emit()
        QTest.qWait(100)
        trigger("previewStationImportButton")
        assert previews == [("Parque", 1, {"external_id": "ID", "model": "Modelo", "extra.Zona operacional": "Zona operacional"})]
        assert not js("contractExcelPanel.visible && contractExcelPanel.matches")
        assert w.grabWindow().save(str(PREVIEW_DIR / "desktop-pcve-import.png"))
        trigger("confirmStationImportButton")
        assert commits and not values["importInfo"]
        js("pcveImportDialog.close()")

        values["desktopStationManagement"] = False
        lem.changed.emit()
        assert not find_visual(w.contentItem(), "importPcves").isVisible()
        trigger("pcveCard_station")
        assert not find_visual(w.contentItem(), "editPcve").isVisible()
        js("pcveDetailsDialog.close()")
        w.setWidth(360)
        w.setHeight(740)
        w.setProperty("androidCamera", True)
        values["authenticated"] = False
        lem.changed.emit()
        js('page="catalog"')
        QTest.qWait(100)
        pointer_click("navigationButton")
        pointer_click("pcveCatalogueMenuItem")
        assert js('page === "pcves" && pcveList.count === 3 && !contractChrome.visible')
        QTest.keyClick(w, Qt.Key.Key_Back)
        QTest.qWait(100)
        assert js('page === "catalog"')
        values["user"] = {}
        values["stations"] = []
        lem.changed.emit()
        assert not js('navMenu.itemAt(1).visible')
        assert not errors, errors
        print("PASS: independent PCVE menu, search, live data, offline navigation, desktop edit and Excel preview")
        print("Previews:", PREVIEW_DIR)
        app.exit(0)
    except Exception:
        import traceback

        traceback.print_exc()
        app.exit(1)


QTimer.singleShot(2400, checks)
rc = app.exec()
delete(engine)
sys.exit(rc)
