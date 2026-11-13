# SPDX-License-Identifier: GPL-3.0-only
# Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

"""Real end-of-page gestures cannot expose a large blank tail on reports/catalogues."""

from pathlib import Path

source = Path(__file__).with_name("test_report_tabs.py")
prefix, _, _ = source.read_text().partition("def run_tests():")
exec(compile(prefix, str(source), "exec"))


def drag_past_end(view):
    start = view.mapToScene(QPoint(int(view.width()) - 24, int(view.height()) - 80)).toPoint()
    initial = js("activePageScroll.contentY")
    device = QTest.createTouchDevice()
    QTest.touchEvent(w, device).press(0, start, w).commit()
    QTest.qWait(20)
    for distance in [35, 70, 105, 140, 175, 210]:
        QTest.touchEvent(w, device).move(0, start - QPoint(0, distance), w).commit()
        QTest.qWait(25)
        assert abs(js("activePageScroll.contentY") - initial) < 1, (
            "drag moved beyond end",
            initial,
            js("activePageScroll.contentY"),
        )
    QTest.touchEvent(w, device).release(0, start - QPoint(0, 210), w).commit()
    QTest.qWait(200)
    assert abs(js("activePageScroll.contentY") - initial) < 1


def check_end(page, last_name=None):
    js(f'page="{page}"')
    QTest.qWait(160)
    if page == "catalog":
        js("chargerList.forceLayout();chargerList.positionViewAtEnd()")
    else:
        js(
            "activePageScroll.contentY=activePageScroll.contentHeight-activePageScroll.height;activePageScroll.returnToBounds()"
        )
    QTest.qWait(250)
    # Wrapped Flow buttons change delegate heights after first instantiation.
    # Let ListView polish these before exercising a gesture at its settled end.
    if page == "catalog":
        for _ in range(8):
            js("chargerList.forceLayout();chargerList.positionViewAtEnd()")
            QTest.qWait(80)
    view = find_visual(w.contentItem(), "chargerList" if page == "catalog" else "reportScroll")
    assert js("pageAtRenderedEnd()"), (
        page,
        js("activePageScroll.contentY"),
        js("activePageScroll.contentHeight"),
        js("activePageScroll.originY"),
        js("activePageScroll.height"),
    )
    for hidden in [False, True, False]:
        js("chromeReveal=" + ("0" if hidden else "1"))
        QTest.qWait(240)
        assert js("pageAtRenderedEnd()")
        if page == "report":
            last = find_visual(w.contentItem(), last_name)
            bottom = last.mapToScene(QPoint(0, int(last.height()))).y()
        else:
            bottom = js(
                "chargerList.mapToItem(null,0,chargerList.itemAtIndex(chargerList.count-1).y+chargerList.itemAtIndex(chargerList.count-1).height-chargerList.contentY).y"
            )
        footer = find_visual(w.contentItem(), "appFooter")
        if footer.isVisible():
            gap = footer.mapToScene(QPoint(0, 0)).y() - bottom
            assert -1 <= gap <= js("pageGap") + 2, ("extra tail or covered last item", page, gap)
        else:
            screen_bottom = js("win.contentItem.mapToItem(null,0,win.contentItem.height).y")
            assert screen_bottom - bottom <= js("appFooter.height+pageGap") + 2, (
                page,
                bottom,
                screen_bottom,
            )
        drag_past_end(view)
    print(
        "PASS:",
        page,
        "bounded drag, accessible final item, visible/hidden footer, small stable end inset",
    )


def checks():
    try:
        w.setWidth(390)
        w.setHeight(844)
        w.setProperty("androidCamera", True)
        w.setProperty("autoHideChrome", True)
        js('showFeedback=false;page="report";selectSection(5)')
        QTest.qWait(100)
        check_end("report", "checkpoint_CARG.06")
        template = copy.deepcopy(values["chargers"][0])
        values["chargers"] = []
        for i in range(30):
            row = copy.deepcopy(template)
            row.update(id=f"id-{i}", external_id=f"ID-{i}")
            row["details"]["serial"] = "Serial line\n" * (i % 4 + 1)
            row["reports"] = [
                dict(id=f"report-{i}-{n}", status="draft", dirty=False) for n in range(i % 5)
            ]
            values["chargers"].append(row)
        lem.changed.emit()
        check_end("catalog")
        # A single short group, a single ID and an empty catalogue do not scroll at all.
        index = next(i for i, s in enumerate(values["sections"]) if len(s["items"]) == 1)
        js(f'page="report";selectSection({index})')
        QTest.qWait(250)
        js("activePageScroll.returnToBounds()")
        QTest.qWait(200)
        assert js("activePageScroll.atYBeginning && activePageScroll.atYEnd")
        drag_past_end(find_visual(w.contentItem(), "reportScroll"))
        for rows in [[template], []]:
            values["chargers"] = rows
            lem.changed.emit()
            js('page="catalog"')
            QTest.qWait(300)
            assert js("chargerList.atYBeginning && chargerList.atYEnd")
            drag_past_end(find_visual(w.contentItem(), "chargerList"))
        # Desktop keeps its fixed navigation and the same real end limit.
        w.setWidth(1100)
        w.setHeight(760)
        w.setProperty("androidCamera", False)
        w.setProperty("autoHideChrome", False)
        js('page="report";selectSection(5)')
        QTest.qWait(200)
        check_end("report", "checkpoint_CARG.06")
        assert not errors, errors
        print("PASS: short/empty pages and desktop; no QML warnings")
        app.exit(0)
    except Exception:
        import traceback

        traceback.print_exc()
        app.exit(1)


QTimer.singleShot(2400, checks)
rc = app.exec()
del engine
sys.exit(rc)
