# SPDX-License-Identifier: GPL-3.0-only
# Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

"""Review jumps respect overlay groups and the real end of the question list.

Offscreen QML geometry regression, with a mock backend (no server needed).
"""
from pathlib import Path
from shiboken6 import delete

source = Path(__file__).with_name("test_report_tabs.py")
exec(compile(source.read_text().partition("def run_tests():")[0], str(source), "exec"), globals())
js('splashActive=false; page="report"; showFeedback=false')

def jump_and_check(section, checkpoint, via_review=False):
    if via_review:
        js("openReview()")
        QTest.qWait(100)
        trigger("reviewJump_" + checkpoint)
    else:
        js(f'jumpTo({section}, {json.dumps(checkpoint)})')
    QTest.qWait(300)  # Include chrome animation and the next layout pass.
    assert js("selectedSection") == section
    card = find_visual(w.contentItem(), "checkpoint_" + checkpoint)
    assert card is not None
    top = card.mapToScene(QPoint(0, 0)).y()
    scroll = find_visual(w.contentItem(), "reportScroll")
    viewport_top = scroll.mapToScene(QPoint(0, 0)).y()
    chrome_bottom = viewport_top + js("pageTopInset")
    # Questions at the end cannot be moved to the top without blank tail space.
    maximum = max(0, scroll.property("contentHeight") - scroll.height())
    actual = scroll.property("contentY")
    content_top = top - viewport_top + actual
    expected = max(0, min(content_top - js("pageTopInset"), maximum))
    assert abs(actual - expected) < 1, (checkpoint, actual, expected, top, chrome_bottom)
    assert top >= chrome_bottom - 1, (checkpoint, top, chrome_bottom)
    assert not js("chromeHidden")
    assert actual <= maximum + 1
    return actual, maximum

for width, height, auto_hide in [(1050, 850, False), (390, 844, True), (640, 390, True)]:
    w.setWidth(width); w.setHeight(height)
    w.setProperty("autoHideChrome", auto_hide)
    js("selectSection(1)")
    QTest.qWait(100)
    required = [item["id"] for item in values["sections"][0]["items"] if not item.get("optional")]
    for checkpoint in [required[0], required[len(required) // 2], required[-1]]:
        # Reproduce returning from review with the scrolling menus hidden.
        js("reportScroll.contentY = Math.max(0, reportScroll.contentHeight-reportScroll.height); chromeReveal=0")
        QTest.qWait(250)
        jump_and_check(0, checkpoint, via_review=True)
    # Cross-group jump: lazy creation and saved scroll restoration must finish first.
    maintenance = next(i for i, section in enumerate(values["sections"]) if section["id"] == "MAN")
    jump_and_check(maintenance, "MAN.07", via_review=True)
    # A last question with enough viewport space must stop at the real content end.
    last = values["sections"][0]["items"][-1]["id"]
    actual, maximum = jump_and_check(0, last)
    card = find_visual(w.contentItem(), "checkpoint_" + last)
    available = find_visual(w.contentItem(), "reportScroll").height() - js("pageTopInset + pageBottomInset")
    if card.height() < available:
        assert abs(actual - maximum) < 1, (actual, maximum)
        bottom = card.mapToScene(QPoint(0, int(card.height()))).y()
        view = find_visual(w.contentItem(), "reportScroll")
        visible_bottom = view.mapToScene(QPoint(0, int(view.height()))).y() - js("pageBottomInset")
        assert bottom <= visible_bottom + 1, (bottom, visible_bottom)
    # No delayed second jump or content-size drift after the transition.
    before = js("reportScroll.contentY")
    extent = js("reportScroll.contentHeight")
    QTest.qWait(300)
    assert js("reportScroll.contentY") == before
    assert js("reportScroll.contentHeight") == extent

# A one-question group also remains bounded when all content fits in the view.
single = len(values["sections"]) - 1
values["sections"][single]["items"] = values["sections"][single]["items"][:1]
lem.changed.emit()
w.setWidth(1050); w.setHeight(1000)
QTest.qWait(100)
jump_and_check(single, values["sections"][single]["items"][0]["id"])
assert not errors, errors
print("PASS: review first/middle/last, same/cross group, desktop/mobile/landscape, overlay inset and bounded list end")
delete(engine)
