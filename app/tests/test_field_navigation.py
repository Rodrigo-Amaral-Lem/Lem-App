# SPDX-License-Identifier: GPL-3.0-only
# Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

"""Touch group navigation, persistent position indicator and catalogue anchor regression."""

from pathlib import Path

source = Path(__file__).with_name("test_report_tabs.py")
prefix, _, _ = source.read_text().partition("def run_tests():")
# The backend emits a catalogue update synchronously when creating a draft.
prefix = prefix.replace(
    'Lem = type("Lem", (QObject,), attrs)',
    """
def openReport(self, charger, report):
    values["chargers"][15]["reports"].append({"id":"new-report","status":"draft","dirty":True})
    self.changed.emit()
attrs["openReport"] = Slot(str,str)(openReport)
Lem = type("Lem", (QObject,), attrs)""",
)
exec(compile(prefix, str(source), "exec"))


def checks():
    try:
        w.resize(400, 800)
        w.setProperty("androidCamera", True)
        w.setProperty("autoHideChrome", True)
        js('page="report";selectedSection=0;reportScroll.contentY=400')
        QTest.qWait(300)
        device = QTest.createTouchDevice()

        def swipe(dx, dy=0):
            start = QPoint(280 if dx < 0 else 100, 500)
            QTest.touchEvent(w, device).press(0, start, w).commit()
            QTest.qWait(30)
            for i in range(1, 7):
                QTest.touchEvent(w, device).move(
                    0, start + QPoint(dx * i // 6, dy * i // 6), w
                ).commit()
                QTest.qWait(25)
            QTest.touchEvent(w, device).release(0, start + QPoint(dx, dy), w).commit()
            QTest.qWait(300)

        swipe(-160)
        assert js("selectedSection") == 1, js("selectedSection")
        swipe(160)
        assert js("selectedSection") == 0
        swipe(160)
        assert js("selectedSection") == 0
        swipe(-10, -130)
        assert js("selectedSection") == 0
        js("chromeReveal=0")
        QTest.qWait(300)
        assert js("pagePositionBar.visible && pagePositionBar.active")
        js("chromeReveal=1")
        QTest.qWait(300)
        assert js("pagePositionBar.visible && pagePositionBar.active")
        template = copy.deepcopy(values["chargers"][0])
        values["chargers"] = [
            dict(copy.deepcopy(template), id=f"c-{i}", external_id=f"ID-{i:02d}") for i in range(30)
        ]
        lem.changed.emit()
        js('page="catalog"')
        QTest.qWait(400)
        js(
            "chargerList.positionViewAtIndex(15,ListView.Beginning);chargerList.contentY+=30;rememberCatalogPosition()"
        )
        QTest.qWait(100)
        anchor = js("catalogAnchor.id")
        offset = js("catalogAnchor.offset")
        js('openCatalogReport("c-15","")')
        QTest.qWait(150)
        assert js("page") == "report"
        js('page="catalog"')
        QTest.qWait(400)
        js("rememberCatalogPosition()")
        assert js("catalogAnchor.id") == anchor, (js("catalogAnchor.id"), anchor)
        assert abs(js("catalogAnchor.offset") - offset) < 2, (js("catalogAnchor.offset"), offset)
        assert not errors, errors
        print(
            "PASS: real horizontal swipes, bounds and vertical rejection; persistent indicator; draft creation retains catalogue card/offset"
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
