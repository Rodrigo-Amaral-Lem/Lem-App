# SPDX-License-Identifier: GPL-3.0-only
# Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

"""Real touch displacement drives chrome; chrome never resizes the document."""
from pathlib import Path
from shiboken6 import delete
source=Path(__file__).with_name("test_report_tabs.py")
exec(compile(source.read_text().partition("def run_tests():")[0], str(source), "exec"), globals())
w.setWidth(390);w.setHeight(844)
js('splashActive=false; showFeedback=false; androidCamera=true; autoHideChrome=true; page="report"')
QTest.qWait(100)
js('reportScroll.contentY=400; resetScrollChrome()')
QTest.qWait(250)
view=find_visual(w.contentItem(),"reportScroll")
baseline=[view.height(),view.property("contentHeight"),js("pagePositionBar.size"),js("header.height")]
assert abs(view.mapToScene(QPoint(0,0)).y()-js("win.contentItem.mapToItem(null,0,0).y"))<1
assert abs(view.height()-js("win.contentItem.height"))<1, "No fixed blank vertical margins"
device=QTest.createTouchDevice()
start=view.mapToScene(QPoint(int(view.width())-24,int(view.height())-120)).toPoint()
QTest.touchEvent(w,device).press(0,start,w).commit();QTest.qWait(25)
travel = js("chromeTravel")
step = max(1, round(travel / 10))
initial_y = js("pageScrollOffset()")
samples = [(initial_y, js("chromeProgress"))]


def move_and_check(distance):
    point = start - QPoint(0, distance)
    QTest.touchEvent(w, device).move(0, point, w).commit()
    QTest.qWait(25)
    y = js("pageScrollOffset()")
    progress = js("chromeProgress")
    previous_y, previous_progress = samples[-1]
    expected = max(0, min(1, previous_progress - (y - previous_y) / travel))
    assert abs(progress - expected) < .015, (distance, y, progress, expected)
    geometry = [view.height(), view.property("contentHeight"), js("pagePositionBar.size"), js("header.height")]
    assert all(abs(a - b) < .01 for a, b in zip(baseline, geometry)), (baseline, geometry)
    samples.append((y, progress))
    return y


# Touch recognition takes up part of the finger's travel. Measure the page
# displacement so each direction exercises the intended part of the animation.
for distance in range(step, start.y(), step):
    bottom_y = move_and_check(distance)
    if bottom_y - initial_y > travel:
        break
else:
    raise AssertionError(("Page did not scroll a toolbar height", travel, samples))
assert samples[-1][1] == 0, samples
assert len({round(p, 2) for _, p in samples if 0 < p < 1}) >= 5, samples
for reverse in range(distance - step, -1, -step):
    if bottom_y - move_and_check(reverse) >= travel / 2:
        break
else:
    raise AssertionError(("Reverse gesture did not scroll half a toolbar height", samples))
assert 0 < samples[-1][1] < 1, samples
# Leave a partially revealed state at rest: no queued animation snaps it open.
QTest.touchEvent(w, device).release(0, start - QPoint(0, reverse), w).commit()
js('reportScroll.cancelFlick()');QTest.qWait(30)
p=js("chromeProgress");QTest.qWait(250)
assert abs(js("chromeProgress")-p)<.001
# Fully hidden panels have no residual fixed band at either viewport edge.
js('chromeReveal=0');QTest.qWait(250)
assert js('appFooter.opacity')==0
assert js('reportChrome.opacity')==0
assert not js('appFooter.visible')
assert not find_visual(w.contentItem(),"syncButton").property("enabled")
js('reportScroll.contentY=0');QTest.qWait(250)
assert js('chromeProgress')==1
# Save transient QA captures to inspect overlap/clipping at intermediate states.
js('reportScroll.contentY=400')
for fraction in [1, .5, 0]:
    js('chromeReveal='+str(fraction));QTest.qWait(250)
    path=str(PREVIEW_DIR / ('chrome-'+str(fraction)+'.png'))
    assert w.grabWindow().save(path)
    print('Preview:',path)
assert not errors, errors
print('PASS: touch-proportional reveal, immediate reversal, partial rest, fixed document/scrollbar, flush viewport edges and always-visible top')
delete(engine)
