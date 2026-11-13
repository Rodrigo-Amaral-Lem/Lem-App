"""Export options are absent for drafts and available for closed reports on desktop/mobile."""

from pathlib import Path

source = Path(__file__).with_name("test_report_tabs.py")
prefix, _, _ = source.read_text().partition("def run_tests():")
exec(compile(prefix, str(source), "exec"))


def checks():
    try:
        for mobile in [False, True]:
            w.setWidth(390 if mobile else 1100)
            w.setHeight(844 if mobile else 760)
            w.setProperty("androidCamera", mobile)
            for status in ["draft", "closed"]:
                values["current"]["status"] = status
                lem.changed.emit()
                QTest.qWait(100)
                js("reportMenu.open()")
                QTest.qWait(150)
                for action in ["exportPdfAction", "exportBundleAction"]:
                    assert js(action + ".visible") == (status == "closed"), (mobile, status, action)
                    if status == "draft":
                        assert js(action + ".height") == 0
                    else:
                        assert js(action + ".enabled")
                js("reportMenu.close()")
                QTest.qWait(70)
        assert not errors, errors
        print(
            "PASS: draft PDF/ZIP options absent; closed report options available on desktop and mobile"
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
