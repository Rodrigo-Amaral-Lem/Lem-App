# SPDX-License-Identifier: GPL-3.0-only
# Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

"""Drive to the final card with real swipes which hide/reveal chrome mid-gesture."""

from pathlib import Path

source = Path(__file__).with_name("test_report_tabs.py")
prefix, _, _ = source.read_text().partition("def run_tests():")
exec(compile(prefix, str(source), "exec"))


def geometry():
    assert (
        abs(js("appFooter.height-(touchHeight+appFooter.topPadding+appFooter.bottomPadding)")) < 0.1
    )
    if js("page") == "report":
        assert js("reportScroll.boundsBehavior") == 0
        assert js("reportScroll.boundsMovement") == 0
        panel = js("sectionPanels.itemAt(selectedSection).lastCheckpointBottom")
        if panel > 0:
            assert (
                abs(
                    js(
                        "reportScroll.contentHeight-sectionPanels.itemAt(selectedSection).y-sectionPanels.itemAt(selectedSection).lastCheckpointBottom-pageBottomInset"
                    )
                )
                < 1
            )
    # No frame may put the selected page past its actual final card.
    if js("pageAtRenderedEnd()") and js("activePageScroll.contentHeight>activePageScroll.height"):
        if js("page") == "report":
            last_id = values["sections"][int(js("selectedSection"))]["items"][-1]["id"]
            item = find_visual(w.contentItem(), "checkpoint_" + last_id)
            bottom = item.mapToScene(QPoint(0, int(item.height()))).y()
        else:
            bottom = js(
                "chargerList.mapToItem(null,0,chargerList.itemAtIndex(chargerList.count-1).y+chargerList.itemAtIndex(chargerList.count-1).height-chargerList.contentY).y"
            )
        edge = js("win.contentItem.mapToItem(null,0,win.contentItem.height).y")
        assert edge - bottom <= js("appFooter.height+pageGap") + 2, (
            "empty tail",
            js("page"),
            edge - bottom,
        )


def swipe(direction=-1, reverse=False):
    view = find_visual(w.contentItem(), "reportScroll" if js("page") == "report" else "chargerList")
    start = view.mapToScene(
        QPoint(int(view.width()) - 24, int(view.height()) - 110 if direction < 0 else 120)
    ).toPoint()
    device = QTest.createTouchDevice()
    QTest.touchEvent(w, device).press(0, start, w).commit()
    QTest.qWait(20)
    for distance in [30, 70, 110, 150, 190, 230]:
        QTest.touchEvent(w, device).move(0, start + QPoint(0, direction * distance), w).commit()
        QTest.qWait(18)
        geometry()
    if reverse:
        for distance in [190, 140, 90]:
            QTest.touchEvent(w, device).move(0, start + QPoint(0, direction * distance), w).commit()
            QTest.qWait(20)
            geometry()
        end = start + QPoint(0, direction * 90)
    else:
        end = start + QPoint(0, direction * 230)
    QTest.touchEvent(w, device).release(0, end, w).commit()
    for _ in range(100):
        QTest.qWait(20)
        geometry()
        if not js("activePageScroll.moving"):
            break


def to_end():
    for _ in range(20):
        swipe()
        if js("pageAtRenderedEnd()"):
            break
    assert js("pageAtRenderedEnd()")
    QTest.qWait(250)
    geometry()
    assert js("chromeHidden") and js("chromeProgress") < 0.001
    for _ in range(2):
        swipe()
    swipe(1)
    to_end_no_recurse()


def to_end_no_recurse():
    for _ in range(8):
        swipe()
        if js("pageAtRenderedEnd()"):
            break
    assert js("pageAtRenderedEnd()")
    geometry()


def checks():
    try:
        w.setWidth(390)
        w.setHeight(844)
        w.setProperty("androidCamera", True)
        w.setProperty("autoHideChrome", True)
        js(
            'showFeedback=false;page="report";selectSection(5);reportScroll.contentY=0;resetScrollChrome()'
        )
        QTest.qWait(250)
        to_end()
        lem.answer("CARG.06", "OK", "")
        QTest.qWait(180)
        geometry()
        swipe()
        # Return from a smaller group and a different viewport while chrome is hidden.
        js("selectSection(6)")
        QTest.qWait(200)
        js("selectSection(5)")
        QTest.qWait(220)
        to_end_no_recurse()
        w.setHeight(760)
        QTest.qWait(180)
        to_end_no_recurse()
        w.setHeight(844)
        QTest.qWait(150)
        to_end_no_recurse()
        template = copy.deepcopy(values["chargers"][0])
        values["chargers"] = []
        for i in range(12):
            row = copy.deepcopy(template)
            row.update(id=f"id-{i}", external_id=f"ID-{i}")
            row["details"]["serial"] = "Serial\n" * (i % 3 + 1)
            row["reports"] = []
            values["chargers"].append(row)
        lem.changed.emit()
        js('page="catalog"')
        QTest.qWait(220)
        to_end()
        assert not errors, errors
        print(
            "PASS: natural swipe-to-end hides chrome; no blank tail across reversal, answer/model changes, group switches and resize, on reports and IDs"
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
