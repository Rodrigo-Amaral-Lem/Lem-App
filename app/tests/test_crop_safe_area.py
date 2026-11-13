"""Exercise the real popup and Qt SafeArea propagation, including rotation."""
from pathlib import Path
source=Path(__file__).with_name('test_report_tabs.py')
exec(compile(source.read_text().partition('def run_tests():')[0],str(source),'exec'))
js('splashActive=false; page="report"')
fixture=QImage(640,480,QImage.Format.Format_RGB32);fixture.fill(QColor('cyan'))
path=PREVIEW_DIR/'safe-crop.png';assert fixture.save(str(path))
for width,height,top,bottom,left,right in [(360,800,32,48,0,0),(320,760,40,56,0,0),(800,360,24,24,40,0),(800,360,24,24,0,40),(1100,820,0,0,0,0)]:
    w.setWidth(width);w.setHeight(height)
    for side,value in [('top',top),('bottom',bottom),('left',left),('right',right)]:js(f'win.SafeArea.additionalMargins.{side}={value}')
    js('cropDialog.photoSource='+json.dumps(QUrl.fromLocalFile(str(path)).toString())+';cropDialog.open()')
    QTest.qWait(200)
    assert js('cropDialog.safeTop')==top
    assert js('cropDialog.y')>=top+8
    assert js('cropDialog.y+cropDialog.height')<=height-bottom-8
    for name in ['resetPhotoCrop','applyPhotoCrop','cancelPhotoCrop']:
        button=find_visual(w.contentItem(),name);assert button and button.isVisible()
        a=button.mapToScene(QPoint(0,0));b=button.mapToScene(QPoint(int(button.width()),int(button.height())))
        assert a.x()>=left and b.x()<=width-right,(name,a,b)
        assert a.y()>=top and b.y()<=height-bottom,(name,a,b)
        assert button.height()>=44
    trigger('resetPhotoCrop');assert js('cropDialog.rightEdge')==1
    area=find_visual(w.contentItem(),'cropImageArea');assert area.width()>0 and area.height()>0
    if width==360:w.grabWindow().save(str(PREVIEW_DIR/'crop-safe-area.png'))
    trigger('cancelPhotoCrop');QTest.qWait(50);assert not js('cropDialog.opened')
assert not errors,errors
print('PASS: title/popup and all three buttons inside Android safe areas, portrait/landscape, left/right cutouts, reset and cancel')
from shiboken6 import delete
delete(engine)
