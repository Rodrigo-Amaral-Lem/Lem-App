# SPDX-License-Identifier: GPL-3.0-only
# Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

"""Real shared QML renders/edits independent per-connector load-test rows."""

from pathlib import Path

source = Path(__file__).with_name("test_report_tabs.py")
prefix = source.read_text().partition("def run_tests():")[0]
exec(compile(prefix, str(source), "exec"))


def checks():
    try:
        import copy

        sections = copy.deepcopy(values["sections"])
        for section in sections:
            if section["id"] == "CARG":
                section["items"] = [
                    dict(
                        id="CARG.01.01",
                        label="Teste de carga do conector CCS2 #1",
                        kind="status",
                        photo_required=False,
                    ),
                    dict(
                        id="CARG.01.02",
                        label="Teste de carga do conector CCS2 #2",
                        kind="status",
                        photo_required=False,
                    ),
                ] + section["items"][1:]
        values["sections"] = sections
        lem.changed.emit()
        w.setWidth(390)
        w.setHeight(844)
        w.setProperty("androidCamera", True)
        w.setProperty("autoHideChrome", True)
        js('page="report";selectSection(5);showFeedback=false')
        QTest.qWait(180)
        initially_answered = js("sectionAnswered(lem.sections[5])")
        trigger("answer_CARG.01.01_OK")
        trigger("answer_CARG.01.02_NOK")
        assert values["current"]["payload"]["answers"]["CARG.01.01"]["value"] == "OK"
        assert values["current"]["payload"]["answers"]["CARG.01.02"]["value"] == "NOK"
        assert find_visual(w.contentItem(), "notes_CARG.01.02").isVisible()
        assert not find_visual(w.contentItem(), "notes_CARG.01.01").isVisible()
        assert js("sectionAnswered(lem.sections[5])") == initially_answered + 2
        assert not errors, errors
        print("PASS: dynamic PC03 rows bind independent answer/notes and progress in real QML")
        app.exit(0)
    except Exception:
        import traceback

        traceback.print_exc()
        app.exit(1)


QTimer.singleShot(2400, checks)
rc = app.exec()
del engine
sys.exit(rc)
