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
samples=[]
previous_y=js("pageScrollOffset()"); previous_p=js("chromeProgress")
for distance in [20,40,60,80,100,120,140,160,180,200,180,160,140,120,100]:
    QTest.touchEvent(w,device).move(0,start-QPoint(0,distance),w).commit()
    QTest.qWait(25)
    y=js("pageScrollOffset()");p=js("chromeProgress")
    expected=max(0,min(1,previous_p-(y-previous_y)/js("chromeTravel")))
    assert abs(p-expected)<.015,(distance,y,p,expected)
    assert all(abs(a-b)<.01 for a,b in zip(baseline,[view.height(),view.property("contentHeight"),js("pagePositionBar.size"),js("header.height")]))
    samples.append(p);previous_y=y;previous_p=p
assert len({round(p,2) for p in samples if 0<p<1})>=5,samples
assert min(samples)==0 and samples[-1]>0,samples
# Leave a partially revealed state at rest: no queued animation snaps it open.
QTest.touchEvent(w,device).release(0,start-QPoint(0,100),w).commit()
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
