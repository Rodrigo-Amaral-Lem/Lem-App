# SPDX-License-Identifier: GPL-3.0-only
# Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

"""Check the independent PCVE menu and desktop administration with real QML."""

from pathlib import Path

from PySide6.QtCore import QSizeF
from shiboken6 import delete


def install_catalogue_fixture(attrs, values, Slot):
    details = {
        "serial": "000071", "manufacturer": "Fabricante verificado", "model": "Modelo X",
        "power": "60", "power_unit": "kVA", "latitude": 0, "connectors": "Mennekes Tipo 2",
        "source": {"Fabricante": "Fabricante importado", "Núcleo": "Parque Norte"},
        "extra.Zona": "Parque Norte", "site_id": "IGNORAR", "location": "IGNORAR",
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

    selected_contract = ""
    def select_contract(self, contract):
        nonlocal selected_contract
        selected_contract = contract
        self.changed.emit()

    def open_report(self, charger, report):
        opened.append((selected_contract, charger, report))
        if report == "report-deleted":
            values["busy"] = True
            def failed():
                values["busy"] = False
                self.changed.emit()
            QTimer.singleShot(30, failed)
        else:
            values["current"] = dict(values["current"], id=report)
            self.changed.emit()
        return True

    attrs["contractId"] = Slot(result=str, name="contractId")(lambda self: selected_contract)
    attrs["selectContract"] = Slot(str, name="selectContract")(select_contract)
    attrs["openReport"] = Slot(str, str, result=bool, name="openReport")(open_report)

    def load_reports(self, station):
        values["stationReports"] = {"id": station}
        self.changed.emit()

        def deliver():
            if not station or values["stationReports"]["id"] != station:
                return
            values["stationReports"]["rows"] = [
                {"id": f"report-{i}", "charger_id": "membership", "contract_id": "cid", "contract": "Contrato A",
                 "identifier": "PCVE-071_2026_TRE", "status": "closed", "started": "2026-11-01"}
                for i in range(1, 16 if station.startswith("scroll-") else 2)]
            self.changed.emit()

        QTimer.singleShot(20, deliver)

    attrs["loadStationReports"] = Slot(str, name="loadStationReports")(load_reports)
    attrs["previewContractIds"] = Slot(str, str, int, "QVariantMap", name="previewContractIds")(
        lambda self, cid, sheet, row, fields: previews.append((cid, sheet, row, fields)))
    attrs["refreshStations"] = Slot(name="refreshStations")(refresh_stations)
    attrs["saveStation"] = Slot(str, str, "QVariantMap", name="saveStation")(save_station)
    attrs["previewStationExcel"] = Slot(str, int, "QVariantMap", name="previewStationExcel")(preview_stations)
    attrs["commitExcel"] = Slot(name="commitExcel")(commit_excel)


refreshes, writes, previews, commits, opened = [], [], [], [], []
source = Path(__file__).with_name("test_report_tabs.py")
prefix, separator, _ = source.read_text(encoding="utf-8").partition("def run_tests():")
marker = 'Lem = type("Lem", (QObject,), attrs)'
assert separator and prefix.count(marker) == 1
prefix = prefix.replace(marker, "install_catalogue_fixture(attrs, values, Slot)\n" + marker)
exec(compile(prefix, str(source), "exec"))


def wait_for_history_button(name):
    # A sheet can be visible before its asynchronous report list is ready.
    for _ in range(100):
        item = find_visual(w.contentItem(), name)
        if item is not None and item.isVisible() and item.property("enabled") and not js("!!pendingReportOpen"):
            return
        QTest.qWait(20)
    raise AssertionError("History button did not become available: " + name)


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
        for expected in ("000071", "60 kVA", "0", "AC Type 2", "Fabricante verificado", "Parque Norte"):
            assert js(f'pcveDetailsDialog.currentFields.some(row => row.value === "{expected}")'), expected
        assert not js('pcveDetailsDialog.currentFields.some(row => ["Fabricante importado", "IGNORAR"].includes(row.value))')
        assert find_visual(w.contentItem(), "pcveReport_report-1") is not None
        assert not js('win.pcveEditFields(pcveDetailsDialog.details).some(f => ["external_id", "location", "site_id"].includes(f.key))')

        values["stations"][0]["details"]["serial"] = "000072"
        lem.changed.emit()
        assert js('pcveDetailsDialog.currentFields.some(row => row.value === "000072")')
        values["chargers"] = [{"id": "membership", "external_id": "PCVE-PROJECTED", "details": {}, "reports": []}]
        lem.changed.emit()
        assert js("pcveDetailsDialog.opened && pcveList.count === 1")
        previous = values["current"]["id"]
        js('win.openStationReport("cid", "membership", "report-deleted")')
        assert js('page === "pcves" && pcveDetailsDialog.opened && pendingReportOpen.id === "report-deleted"')
        wait_for_history_button("pcveReport_report-1")
        assert js('page === "pcves" && pcveDetailsDialog.opened && !pendingReportOpen')
        assert values["current"]["id"] == previous
        trigger("pcveReport_report-1")
        assert opened[-1] == ("cid", "membership", "report-1")
        assert js('page === "report" && !pcveDetailsDialog.opened')
        pointer_click("reportBackButton")
        assert js('page === "pcves" && pcveDetailsDialog.opened && pcveDetailsDialog.stationId === "station"')
        assert js('pcveSearch.text === " parque NORTE "')
        # Android can keep the popup overlay at its previous logical size
        # after the actual window has switched density with the sheet open.
        w.resize(384, 725)
        QTest.qWait(100)
        overlay = js("win.Overlay.overlay")
        overlay.setSize(QSizeF(576, 1088))
        try:
            QTest.qWait(100)
            button = find_visual(w.contentItem(), "closePcveDetails")
            corner = button.mapToScene(QPoint(0, 0))
            assert 0 <= corner.x() <= w.width() - button.width()
            assert 0 <= corner.y() <= w.height() - button.height()
            pointer_click("closePcveDetails")
            assert js('!pcveDetailsDialog.visible && pcveSearch.text === " parque NORTE "')
        finally:
            w.resize(1100, 770)
            QTest.qWait(100)

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
        assert find_visual(w.contentItem(), "pcveEdit_extra.Zona").property("text") == "Parque Norte"
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

        values["contracts"] = [{"id": "cid", "name": "Contrato A"}]
        values["chargers"][0].update(station_id="station", external_id="PCVE-071")
        lem.changed.emit()
        js('page="admin"')
        trigger("manageContractIds")
        assert js('contractIdsDialog.opened && managedIds.count === 1')
        trigger("editContractPcve_membership")
        assert js('pcveEditDialog.opened && pcveEditDialog.stationId === "station"')
        js("pcveEditDialog.close()")
        # Android administrators can manage contract imports even when the
        # global desktop-only editor is unavailable. Analysis emits changed().
        values["desktopStationManagement"] = False
        lem.changed.emit()
        trigger("importContractIds")
        values["importInfo"] = {"ids_contract": "cid", "batch_id": "contract-import",
                                "sheets": [{"name": "Parque", "sample": [["ID", "Modelo"]]}]}
        lem.changed.emit()
        QTest.qWait(100)
        assert js("pcveImportDialog.opened")
        trigger("contractIdsPreviewImportButton")
        assert previews[-1] == ("cid", "Parque", 1, {"external_id": "ID", "model": "Modelo"})
        assert not js("contractExcelPanel.matches")
        js("pcveImportDialog.close(); contractIdsDialog.close()")
        js('page="pcves"')
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
        nav_button = find_visual(w.contentItem(), "navigationButton")
        nav_point = nav_button.mapToScene(QPoint(int(nav_button.width() / 2), int(nav_button.height() / 2)))
        QTest.mouseMove(w, nav_point.toPoint())
        QTest.qWait(100)
        assert js('navigationButton.hovered && !navigationButton.ToolTip.visible')
        pointer_click("navigationButton")
        assert js('navMenu.itemAt(1).text === "PCVEs" && !navigationButton.ToolTip.visible')
        pointer_click("pcveCatalogueMenuItem")
        assert js('page === "pcves" && pcveList.count === 3 && !contractChrome.visible')
        # Preserve the selected card's screen position and the history scroll,
        # even with many results and a contract selection behind the report.
        values["stations"] = [
            dict(values["stations"][0], id=f"scroll-{i}", external_id=f"PCVE-LISTA-{i:02}")
            for i in range(40)]
        lem.changed.emit()
        js('pcveSearch.text = "LISTA"')
        QTest.qWait(250)
        js('pcveList.positionViewAtIndex(19, ListView.Center)')
        QTest.qWait(100)
        card_y = js('pcveList.itemAtIndex(19).y - pcveList.contentY')
        trigger("pcveCard_scroll-19")
        QTest.qWait(150)
        assert js('pcveDetailsDialog.opened && pcveDetailsDialog.history.rows.length === 15')
        js('pcveDetailsScroll.contentItem.contentY = pcveDetailsScroll.contentItem.contentHeight - pcveDetailsScroll.contentItem.height')
        QTest.qWait(100)
        details_y = js('pcveDetailsScroll.contentItem.contentY')
        assert details_y > 0, (details_y, js('pcveDetailsScroll.contentItem.contentHeight'), js('pcveDetailsScroll.contentItem.height'))
        trigger("pcveReport_report-15")
        assert abs(js('stationReportOrigin.detailsY') - details_y) < 2
        QTest.keyClick(w, Qt.Key.Key_Back)
        QTest.qWait(250)
        assert js('pcveDetailsDialog.opened && pcveDetailsDialog.stationId === "scroll-19"')
        assert js('pcveSearch.text === "LISTA"')
        restored_y = js('pcveDetailsScroll.contentItem.contentY')
        assert abs(restored_y - details_y) < 2, (restored_y, details_y)
        trigger("closePcveDetails")
        assert abs(js('pcveList.itemAtIndex(19).y - pcveList.contentY') - card_y) < 2
        trigger("pcveCard_scroll-19")
        wait_for_history_button("pcveReport_report-1")
        trigger("pcveReport_report-1")
        values["stations"].pop(19)
        lem.changed.emit()
        QTest.keyClick(w, Qt.Key.Key_Back)
        QTest.qWait(100)
        assert js('page === "pcves" && !pcveDetailsDialog.visible && pcveSearch.text === "LISTA"')
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
