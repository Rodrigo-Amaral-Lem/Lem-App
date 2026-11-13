# SPDX-License-Identifier: GPL-3.0-only
# Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

"""Regression for moving ToolBar safe-area padding feeding back into page height.

A desktop offscreen window normally has zero safe margins, masking this Android
failure. Inject a bottom exclusion into the stationary parent so the real Qt
SafeArea propagation, style padding and transform bindings all run.
"""

from pathlib import Path

source = Path(__file__).with_name("test_report_tabs.py")
exec(compile(source.read_text().partition("def run_tests():")[0], str(source), "exec"))


def geometry():
    return [
        js(s)
        for s in [
            "appFooter.height",
            "pageBottomInset",
            "activePageScroll.height",
            "activePageScroll.contentHeight",
            "activePageScroll.contentY",
            "pagePositionBar.size",
            "pagePositionBar.height",
        ]
    ]


def checks():
    try:
        js("adminArea.currentIndex=2")
        w.setWidth(390)
        w.setHeight(844)
        js("autoHideChrome=true;androidCamera=true")
        template = copy.deepcopy(values["chargers"][0])
        values["chargers"] = [
            {**copy.deepcopy(template), "id": f"safe-{i}", "external_id": f"ID-{i}"}
            for i in range(30)
        ]
        lem.changed.emit()
        for margin in [24, 48, 0]:
            js(f"win.contentItem.SafeArea.additionalMargins.bottom={margin}")
            for page in ["report", "catalog", "admin"]:
                js(f'page="{page}";chromeReveal=1')
                QTest.qWait(240)
                js("activePageScroll.cancelFlick();activePageScroll.contentY=200")
                QTest.qWait(60)
                baseline = geometry()
                for hidden in [True, False, True, False]:
                    js("chromeReveal=" + ("0" if hidden else "1"))
                    for _ in range(12):
                        QTest.qWait(20)
                        current = geometry()
                        assert all(abs(a - b) < 0.1 for a, b in zip(current, baseline)), (
                            margin,
                            page,
                            baseline,
                            current,
                        )
                    assert abs(js("appFooter.height") - js("touchHeight+8")) < 0.1
                    assert js("appFooter.bottomPadding") == 4
                    if hidden and margin:
                        assert js("appFooter.SafeArea.margins.bottom") > margin
                assert not errors, errors
        print(
            "PASS: nonzero Android safe areas; report/catalog/admin; stable footer, page height, position and scrollbar through every animation frame; no binding loops"
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
