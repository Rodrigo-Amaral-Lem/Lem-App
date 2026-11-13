# SPDX-License-Identifier: GPL-3.0-only
# Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

"""Android scroll-direction chrome regression, using the shared isolated QML mock.

Run: python app/tests/test_scroll_chrome.py
No real account, API, report or device storage is used.
"""

from pathlib import Path
import copy

mock_source = Path(__file__).with_name("test_report_tabs.py")
# Reuse the real QML engine/mock/helpers without executing that suite's timers.
mock_prefix, marker, _ = mock_source.read_text().partition("def run_tests():")
assert marker, "Shared QML mock entry point changed; update this harness"
exec(compile(mock_prefix, str(mock_source), "exec"))


def wait_for(condition, timeout=1600):
    for _ in range(timeout // 20):
        if condition():
            return
        QTest.qWait(20)
    assert condition(), "Timed out waiting for chrome/scroll state"


def stop_scroll():
    js("activePageScroll.cancelFlick()")
    QTest.qWait(40)


def check_page(name, panel_names):
    js(f'page="{name}"')
    QTest.qWait(100)
    js("activePageScroll.contentY=pageScrollOrigin()")
    QTest.qWait(240)
    assert js("chromeAtTop")
    assert js("chromeProgress") == 1
    header_height = js("header.height")
    footer_height = js("appFooter.height")
    panel_heights = [find_visual(w.contentItem(), n).height() for n in panel_names]
    assert all(h > 0 for h in panel_heights), (name, panel_heights)

    # The track spans the whole reading screen; elastic overshoot cannot shrink the thumb.
    assert abs(js("pagePositionBar.height") - js("win.contentItem.height")) < 1
    js("activePageScroll.contentY=pageScrollOrigin()+100")
    QTest.qWait(100)
    thumb = js("pagePositionBar.contentItem.height")
    assert thumb >= 47
    js("activePageScroll.contentY=pageScrollOrigin()+activePageScroll.contentHeight-activePageScroll.height+500")
    QTest.qWait(100)
    assert abs(js("pagePositionBar.contentItem.height")-thumb)<1
    assert abs(js("pagePositionBar.position + pagePositionBar.size")-1)<0.001
    js("activePageScroll.contentY=pageScrollOrigin()-500")
    QTest.qWait(100)
    assert abs(js("pagePositionBar.contentItem.height")-thumb)<1
    assert js("pagePositionBar.position")==0

    # Programmatic positioning (group restore, keyboard focus) is not a gesture.
    js("activePageScroll.contentY=180")
    QTest.qWait(240)
    assert not js("chromeHidden"), name

    # Flickable's public flick() produces the same moving/flicking signals as a swipe.
    js("activePageScroll.flick(0,-1500)")
    wait_for(lambda: 0 < js("chromeProgress") < 1)
    fraction = js("chromeProgress")
    assert 0 < fraction < 1, (name, fraction)
    assert js("header.height") == header_height
    wait_for(lambda: js("chromeProgress") < 0.001)
    stop_scroll()
    assert js("appFooter.height") == footer_height
    assert not js("appFooter.visible")
    assert all(find_visual(w.contentItem(), n).property("opacity") == 0 for n in panel_names)
    assert not find_visual(w.contentItem(), "syncButton").property("enabled")
    assert js("header.height") == header_height
    assert find_visual(w.contentItem(), "accountButton").isVisible()

    # Chrome remains hidden at rest; scrolling upward restores it before reaching the top.
    QTest.qWait(240)
    assert js("chromeHidden"), name
    js("activePageScroll.flick(0,1200)")
    wait_for(lambda: not js("chromeHidden"))
    wait_for(lambda: js("chromeProgress") > 0.999)
    stop_scroll()
    assert js("appFooter.height") == footer_height
    assert all(find_visual(w.contentItem(), n).height() > 0 for n in panel_names)

    # Chrome is now an overlay: no animation frame may resize/reposition the reading area.
    js("activePageScroll.contentY=400;chromeReveal=1")
    QTest.qWait(240)

    def geometry():
        return [
            js("activePageScroll.height"),
            js("activePageScroll.contentHeight"),
            js("activePageScroll.contentY"),
            js("activePageScroll.mapToItem(null,0,0).y"),
            js("pagePositionBar.size"),
            js("pagePositionBar.height"),
            js("pagePositionBar.contentItem.height"),
        ]

    question = find_visual(w.contentItem(), "checkpoint_EST.07") if name == "report" else None
    question_y = question.mapToScene(QPoint(0, 0)).y() if question else None
    baseline = geometry()
    for hidden in [True, False, True, False]:
        js("chromeReveal=" + ("0" if hidden else "1"))
        for _ in range(12):
            QTest.qWait(20)
            current = geometry()
            if question:
                assert (
                    abs(question.mapToScene(QPoint(0, 0)).y() - question_y) < 0.01
                ), "Question drifted during toolbar animation"
            assert all(abs(a - b) < 0.01 for a, b in zip(baseline, current)), (
                name,
                baseline,
                current,
            )
    print(
        "PASS:",
        name,
        "stable viewport, content offset and scroll indicator on every animation frame",
    )

    # Top always reveals, even after a non-gesture jump and even if hidden was set.
    js("chromeReveal=0")
    QTest.qWait(220)
    js("activePageScroll.contentY=pageScrollOrigin()")
    wait_for(lambda: js("chromeProgress") > 0.999)
    assert js("chromeAtTop") and not js("chromeHidden")
    assert js("header.height") == header_height
    print("PASS:", name, "direction, animation, fixed header and always-visible top")


def run_checks():
    try:
        w.setWidth(390)
        w.setHeight(844)
        w.setProperty("autoHideChrome", True)
        template = copy.deepcopy(values["chargers"][0])
        values["chargers"] = [
            {**copy.deepcopy(template), "id": f"charger-{i}", "external_id": f"ID-{i:03}"}
            for i in range(30)
        ]
        lem.changed.emit()
        QTest.qWait(80)
        # Exercise real page touches above the fixed footer, plus Flickable velocity.
        js('page="report";activePageScroll.contentY=0')
        QTest.qWait(240)
        touch_device = QTest.createTouchDevice()
        view = find_visual(w.contentItem(), "reportScroll")
        start = view.mapToScene(QPoint(int(view.width()) - 5, int(view.height()) - 100)).toPoint()
        QTest.touchEvent(w, touch_device).press(0, start, w).commit()
        QTest.qWait(30)
        for distance in [40, 80, 120, 160, 200]:
            QTest.touchEvent(w, touch_device).move(0, start - QPoint(0, distance), w).commit()
            QTest.qWait(25)
        QTest.touchEvent(w, touch_device).release(0, start - QPoint(0, 200), w).commit()
        wait_for(lambda: js("chromeHidden"))
        stop_scroll()
        print("PASS: touch swipe hides page actions without hiding the main header")
        js("activePageScroll.contentY=0;resetScrollChrome()")
        QTest.qWait(240)
        start = view.mapToScene(QPoint(int(view.width()) - 5, int(view.height()) - 100)).toPoint()
        QTest.touchEvent(w, touch_device).press(0, start, w).commit()
        QTest.qWait(30)
        for distance in [40, 80, 120]:
            QTest.touchEvent(w, touch_device).move(0, start - QPoint(0, distance), w).commit()
            QTest.qWait(25)
        assert 0 < js("chromeProgress") < 1
        QTest.touchEvent(w, touch_device).move(0, start - QPoint(0, 50), w).commit()
        wait_for(lambda: not js("chromeHidden"))
        QTest.touchEvent(w, touch_device).release(0, start - QPoint(0, 50), w).commit()
        stop_scroll()
        print("PASS: finger reversal smoothly reverses an in-progress collapse")
        check_page("report", ["reportChrome"])
        check_page("catalog", ["contractChrome", "catalogChrome"])
        # The contract tab is now deliberately short. Use the longer accounts
        # workflow to exercise scrolling instead of expecting a short page to hide.
        js("adminArea.currentIndex=2")
        w.setHeight(640)
        check_page("admin", ["contractChrome"])
        w.setHeight(844)
        QTest.qWait(80)

        # A bottom-edge rebound cannot masquerade as a deliberate upward scroll.
        js('page="report"')
        QTest.qWait(100)
        js(
            "activePageScroll.contentY=(pageScrollOrigin())+Math.max(0,activePageScroll.contentHeight-activePageScroll.height);chromeReveal=0"
        )
        QTest.qWait(300)
        stop_scroll()
        js("activePageScroll.contentY+=45;activePageScroll.returnToBounds()")
        QTest.qWait(420)
        assert js("chromeHidden")
        assert find_visual(w.contentItem(), "accountButton").isVisible()
        trigger("accountButton")
        assert js("loginDialog.opened")
        js("loginDialog.close()")
        assert js("chromeHidden"), "Opening a dialog must not reuse its scroll position"

        # A short catalogue has no reason to hide any navigation controls.
        values["chargers"] = [copy.deepcopy(template)]
        lem.changed.emit()
        js('page="catalog"')
        QTest.qWait(300)
        js("activePageScroll.contentY=pageScrollOrigin();activePageScroll.flick(0,-900)")
        QTest.qWait(450)
        stop_scroll()
        assert js("chromeAtTop") and js("chromeProgress") == 1
        print("PASS: bottom overshoot, fixed account action and short catalogue")

        # Navigation/group changes reveal controls without altering any answers.
        js('page="report"')
        QTest.qWait(100)
        js("activePageScroll.contentY=400;chromeReveal=0")
        QTest.qWait(240)
        js("sectionPositions=({1:300});selectSection(4)")
        wait_for(lambda: js("chromeProgress") > 0.999)
        assert not js("chromeHidden")

        # Desktop behavior stays fixed, including deliberate scroll movement.
        w.setProperty("autoHideChrome", False)
        js("activePageScroll.contentY=180;activePageScroll.flick(0,-1000)")
        QTest.qWait(320)
        assert js("chromeProgress") == 1
        stop_scroll()
        assert not errors, errors
        print("PASS: group reset and desktop opt-out; no QML warnings")
        app.exit(0)
    except Exception:
        import traceback

        traceback.print_exc()
        print(
            "Debug:",
            js("page"),
            js("chromeHidden"),
            js("chromeProgress"),
            js("activePageScroll.contentY"),
        )
        app.exit(1)


QTimer.singleShot(2400, run_checks)
rc = app.exec()
del engine
sys.exit(rc)
