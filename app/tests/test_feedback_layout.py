"""Notification/footer hit areas stay separate on narrow Android and desktop windows."""

from pathlib import Path

source = Path(__file__).with_name("test_report_tabs.py")
prefix, _, _ = source.read_text().partition("def run_tests():")
exec(compile(prefix, str(source), "exec"))


def checks():
    try:
        for width, height, android in [
            (390, 844, True),
            (800, 650, False),
            (1280, 800, False),
            (500, 700, False),
        ]:
            w.setWidth(width)
            w.setHeight(height)
            w.setProperty("androidCamera", android)
            w.setProperty("autoHideChrome", android)
            js('page="report";reportScroll.contentY=300;chromeReveal=1')
            values["message"] = "Sessão terminada. Os rascunhos permanecem no dispositivo."
            lem.changed.emit()
            js("showFeedback=true")
            QTest.qWait(260)
            footer_height = js("appFooter.height")
            for progress in [1, 0.75, 0.25, 0]:
                js(f"chromeReveal={progress}")
                QTest.qWait(30)
                assert js("feedbackToast.visible")
                toast = js("feedbackToast.y+feedbackToast.height")
                footer = js("appFooter.y+appFooter.height*(1-chromeProgress)")
                assert toast <= footer - 7, (width, progress, toast, footer)
                assert js("feedbackToast.y") >= 0, (width, progress)
                assert js("appFooter.height") == footer_height
            js("chromeReveal=1")
            QTest.qWait(250)
            values["message"] = "Mensagem comprida. " * 80
            lem.changed.emit()
            js("showFeedback=true")
            QTest.qWait(100)
            assert js("feedbackToast.y+feedbackToast.height<=appFooter.y-7")
            assert js("feedbackToast.height") < 150, (width, js("feedbackToast.height"))
            assert find_visual(w.contentItem(), "syncButton").isVisible()
            pointer_click("dismissFeedbackButton")
            assert not js("feedbackToast.visible")
            js("loginDialog.open()")
            QTest.qWait(150)
            assert find_visual(w.contentItem(), "defaultServerButton") is not None
            js("loginDialog.close()")
            QTest.qWait(60)
        assert not errors, errors
        print(
            "PASS: toast above visible footer at every animation position; multiline bounds, dismiss target, desktop/narrow windows, server reset affordance"
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
