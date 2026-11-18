# SPDX-License-Identifier: GPL-3.0-only
# Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

"""Exercise checkpoint feedback and retry in Main.qml with a mock controller."""

import os
from pathlib import Path
from types import SimpleNamespace

from shiboken6 import delete


DRAFT_ERROR = "O novo rascunho não foi criado. Tente novamente após verificar o armazenamento."
DATE_ERROR = "Não foi possível guardar as datas. As datas anteriores foram conservadas."


def load_fixture(install_checkpoint_stubs):
    # Reuse the navigation runner's real QML and demo data. Install bool-returning
    # slots before PySide builds the controller's metaobject.
    source = Path(__file__).with_name("test_report_tabs.py")
    prefix, separator, _ = source.read_text(encoding="utf-8").partition("def run_tests():")
    marker = 'Lem = type("Lem", (QObject,), attrs)'
    assert separator and prefix.count(marker) == 1, "Navigation fixture setup has changed"
    prefix = prefix.replace(marker, "install_checkpoint_stubs(attrs, values, Slot)\n" + marker)
    scope = {"__file__": str(source), "install_checkpoint_stubs": install_checkpoint_stubs}
    exec(compile(prefix, str(source), "exec"), scope)
    return SimpleNamespace(**scope)


def visible_text(item, text, *, button=False):
    if item.isVisible() and item.property("text") == text:
        if not button or item.metaObject().indexOfSignal("clicked()") >= 0:
            return item
    for child in item.childItems():
        match = visible_text(child, text, button=button)
        if match is not None:
            return match
    return None


def click_text(ui, text):
    # The catalogue and return buttons have labels but no object names.
    button = visible_text(ui.w.contentItem(), text, button=True)
    assert button is not None and button.isEnabled(), text
    point = button.mapToScene(ui.QPoint(int(button.width() / 2), int(button.height() / 2)))
    ui.QTest.mouseClick(
        ui.w, ui.Qt.MouseButton.LeftButton, ui.Qt.KeyboardModifier.NoModifier, point.toPoint()
    )
    ui.QTest.qWait(200)


def main():
    os.environ["QML_DISABLE_DISK_CACHE"] = "1"
    draft_saved = False
    dates_saved = False
    draft_attempts = []
    date_attempts = []

    def install_checkpoint_stubs(attrs, values, Slot):
        def openReport(self, charger, report):
            draft_attempts.append((charger, report))
            if draft_saved:
                values["current"]["id"] = f"new-draft-{len(draft_attempts)}"
                values["message"] = "Novo rascunho guardado no dispositivo."
            else:
                values["message"] = DRAFT_ERROR
            self.changed.emit()
            return draft_saved

        def setCorrectionTimes(self, started, finished):
            date_attempts.append((started, finished))
            if dates_saved:
                values["current"]["payload"].update(started=started, finished=finished)
                values["message"] = "Datas da correção guardadas."
            else:
                values["message"] = DATE_ERROR
            self.changed.emit()
            return dates_saved

        attrs["openReport"] = Slot(str, str, result=bool)(openReport)
        attrs["setCorrectionTimes"] = Slot(str, str, result=bool)(setCorrectionTimes)

    ui = load_fixture(install_checkpoint_stubs)
    try:
        ui.js('splashActive = false; page = "catalog"')
        ui.QTest.qWait(200)
        click_text(ui, "Nova intervenção")
        assert draft_attempts == [("charger", "")]
        assert ui.js('page === "catalog" && draftSaveErrorDialog.opened')
        assert visible_text(ui.w.contentItem(), DRAFT_ERROR) is not None
        click_text(ui, "Voltar à lista")
        assert ui.js('page === "catalog" && !draftSaveErrorDialog.visible')

        draft_saved = True
        click_text(ui, "Nova intervenção")
        assert draft_attempts == [("charger", "")] * 2
        assert ui.js('page === "report" && win.reportData.id === "new-draft-2"')
        assert not ui.js("draftSaveErrorDialog.visible")

        # A first-attempt success still opens the newly saved report without a dialog.
        ui.js('page = "catalog"')
        ui.QTest.qWait(200)
        click_text(ui, "Nova intervenção")
        assert draft_attempts == [("charger", "")] * 3
        assert ui.js('page === "report" && win.reportData.id === "new-draft-3"')
        assert not ui.js("draftSaveErrorDialog.visible")

        original_dates = ("2026-10-01T09:00:00+01:00", "2026-10-01T11:30:12+01:00")
        entered_dates = (" 2026-11-17T09:30:00+01:00", "2026-11-17T11:00:00Z ")
        ui.values["authenticated"] = True
        report = ui.values["current"]
        report["correction"] = {"state": "editing", "editor_id": "demo", "source_id": "original"}
        report["payload"].update(started=original_dates[0], finished=original_dates[1])
        ui.lem.changed.emit()
        ui.QTest.qWait(80)
        ui.js("reportMenu.open()")
        ui.trigger("correctionDatesAction")
        assert ui.js("correctionStart.text") == original_dates[0]
        assert ui.js("correctionEnd.text") == original_dates[1]
        for name, value in zip(("correctionStart", "correctionEnd"), entered_dates):
            field = ui.find_visual(ui.w.contentItem(), name)
            assert field is not None and field.setProperty("text", value), name

        ui.trigger("saveCorrectionDates")
        assert date_attempts == [entered_dates]
        assert ui.js("correctionDatesDialog.opened")
        error = ui.find_visual(ui.w.contentItem(), "correctionDateSaveError")
        assert error is not None and error.isVisible() and error.property("text") == DATE_ERROR
        assert ui.js("correctionStart.text") == entered_dates[0]
        assert ui.js("correctionEnd.text") == entered_dates[1]

        dates_saved = True
        ui.trigger("saveCorrectionDates")
        assert date_attempts == [entered_dates, entered_dates]
        assert not ui.js("correctionDatesDialog.opened")

        ui.js("reportMenu.open()")
        ui.trigger("correctionDatesAction")
        assert ui.js("correctionDatesDialog.opened && correctionDatesDialog.saveError === ''")
        ui.trigger("saveCorrectionDates")
        assert date_attempts == [entered_dates] * 3
        assert not ui.js("correctionDatesDialog.opened")
        assert not ui.errors, ui.errors
    finally:
        delete(ui.engine)
    print("PASS: draft and correction date failures, visible feedback, exact input retention, retry and success")


if __name__ == "__main__":
    main()
