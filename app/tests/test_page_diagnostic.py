# SPDX-License-Identifier: GPL-3.0-only
# Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

"""Read-only, privacy-limited page snapshot and clipboard interaction."""

from pathlib import Path

source = Path(__file__).with_name("test_report_tabs.py")
prefix, _, _ = source.read_text().partition("def run_tests():")
exec(compile(prefix, str(source), "exec"))


def checks():
    try:
        w.setWidth(390)
        w.setHeight(844)
        w.setProperty("androidCamera", True)
        w.setProperty("autoHideChrome", True)
        js('page="report";selectSection(5)')
        QTest.qWait(200)
        js("reportScroll.contentY=reportScroll.contentHeight-reportScroll.height;chromeReveal=0")
        QTest.qWait(250)
        before = js("reportScroll.contentY")
        original_answers = copy.deepcopy(values["current"]["payload"]["answers"])
        trigger("accountButton")
        QTest.qWait(120)
        data = json.loads(js("capturedPageDiagnostic"))
        assert data["interfaceRevision"] == "bounds032-safearea1"
        assert data["page"] == "report" and data["chrome"]["progress"] < 0.001
        assert data["lastCard"]["objectName"] == "checkpoint_CARG.06"
        assert abs(data["lastCard"]["mappedContentEnd"] - data["scroll"]["contentHeight"]) < 1
        assert abs(js("reportScroll.contentY") - before) < 1
        assert values["current"]["payload"]["answers"] == original_answers
        payload = js("capturedPageDiagnostic")
        for private in [
            "PCVE-DEMO-001",
            "Técnico de demonstração",
            "demo@",
            "payload",
            "answers",
            "photos",
        ]:
            assert private not in payload, private
        js("debugToolsEnabled=true"); trigger("openScrollDiagnosticButton")
        QTest.qWait(100)
        assert js("scrollDiagnosticDialog.opened")
        trigger("copyScrollDiagnosticButton")
        assert app.clipboard().text() == payload
        assert js("diagnosticCopied")
        js("scrollDiagnosticDialog.close();loginDialog.close()")
        QTest.qWait(120)
        values["chargers"] = []
        lem.changed.emit()
        js('page="catalog"')
        QTest.qWait(160)
        js("openAccount()")
        QTest.qWait(80)
        empty = json.loads(js("capturedPageDiagnostic"))
        assert empty["itemCount"] == 0 and empty["lastCard"] is None
        js('loginDialog.close();page="admin"')
        QTest.qWait(150)
        js("capturePageDiagnostic()")
        assert json.loads(js("capturedPageDiagnostic"))["adminTail"] is not None
        assert not errors, errors
        print(
            "PASS: snapshot before modal, hidden footer, actual mapped end, no answer/user data, exact clipboard, empty catalogue/admin"
        )
        app.exit(0)
    except Exception:
        import traceback

        traceback.print_exc()
        app.exit(1)


QTimer.singleShot(2400, checks)
rc = app.exec()
del engine
sys.exit(rc)
