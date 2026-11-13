# SPDX-License-Identifier: GPL-3.0-only
# Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

"""Photo crop preview and media controls at narrow Android and desktop sizes."""
from pathlib import Path
from shiboken6 import delete
source=Path(__file__).with_name("test_report_tabs.py")
prefix=source.read_text().partition("def run_tests():")[0]
prefix=prefix.replace('Lem = type("Lem", (QObject,), attrs)', '''
crops=[]
def cropPhoto(self, photo_id, x, y, width, height): crops.append((photo_id,x,y,width,height))
attrs["cropPhoto"] = Slot(str,float,float,float,float)(cropPhoto)
video_calls=[]
def takeVideo(self,key): video_calls.append(key)
attrs["takeVideo"] = Slot(str)(takeVideo)
attrs["openVideo"] = Slot(str)(lambda self,key:None)
Lem = type("Lem", (QObject,), attrs)''')
exec(compile(prefix,str(source),"exec"),globals())
js('splashActive=false; page="report"; selectSection(1)')
fixture=QImage(640,480,QImage.Format.Format_RGB32);fixture.fill(QColor('#00abc0'))
path=PREVIEW_DIR/'crop.png';assert fixture.save(str(path))
values['current']['photos']=[
    dict(id='picture',checkpoint='EST.01',mime='image/png',path=QUrl.fromLocalFile(str(path)).toString(),uploaded=False),
    dict(id='movie',checkpoint='EST.01',mime='video/mp4',path='file:///tmp/example.mp4',uploaded=False)]
lem.changed.emit();QTest.qWait(200)
assert js('photographsFor("EST.01").length')==1
for width,height in [(320,760),(360,760),(640,360),(1100,820)]:
    w.setWidth(width);w.setHeight(height);js('androidCamera=true');QTest.qWait(100)
    actions=[find_visual(w.contentItem(),name+'_EST.01') for name in ['addPhoto','choosePhoto','addVideo','chooseVideo','toggleNotes']]
    assert all(action.width()>=44 for action in actions)
    assert max(action.mapToScene(QPoint(0,0)).y() for action in actions)-min(action.mapToScene(QPoint(0,0)).y() for action in actions)<1
    assert actions[-1].mapToScene(QPoint(int(actions[-1].width()),0)).x()<=width
    trigger('addVideo_EST.01')
    assert video_calls[-1]=='EST.01'
    trigger('cropPhoto_picture');QTest.qWait(150)
    assert js('cropDialog.opened')
    assert js('cropDialog.width')<=width
    if width==360: w.grabWindow().save(str(PREVIEW_DIR/"crop-preview.png"))
    area=find_visual(w.contentItem(),'cropImageArea');assert area and area.width()>0 and area.height()>0
    touch=find_visual(w.contentItem(),'cropTouchArea')
    # Every square AND its full expanded touch area fit inside the clipped stage.
    stage=area.parentItem()
    for n in range(4):
        handle=find_visual(w.contentItem(),'cropHandle_'+str(n))
        top=handle.mapToItem(stage,QPoint(0,0))
        bottom=handle.mapToItem(stage,QPoint(int(handle.width()),int(handle.height())))
        assert top.x()>=0 and top.y()>=0 and bottom.x()<=stage.width() and bottom.y()<=stage.height()
    assert touch.mapToItem(stage,QPoint(0,0)).x()>=0
    assert touch.mapToItem(stage,QPoint(0,0)).y()>=0
    # Grab the part of the top-left handle OUTSIDE the image and drag inward.
    start=area.mapToScene(QPoint(-8,-8))
    end=area.mapToScene(QPoint(int(area.width()*.1),int(area.height()*.1)))
    QTest.mousePress(w,Qt.MouseButton.LeftButton,Qt.KeyboardModifier.NoModifier,start.toPoint())
    QTest.mouseMove(w,end.toPoint(),20)
    QTest.mouseRelease(w,Qt.MouseButton.LeftButton,Qt.KeyboardModifier.NoModifier,end.toPoint())
    assert js('cropDialog.leftEdge')>0 and js('cropDialog.topEdge')>0
    js('cropDialog.resetSelection()')
    # Real pointer movement of the lower-right selection corner, with a letterboxed image.
    start=area.mapToScene(QPoint(int(area.width()-2),int(area.height()-2)))
    end=area.mapToScene(QPoint(int(area.width()*.7),int(area.height()*.7)))
    QTest.mousePress(w,Qt.MouseButton.LeftButton,Qt.KeyboardModifier.NoModifier,start.toPoint())
    QTest.mouseMove(w,end.toPoint(),20)
    QTest.mouseRelease(w,Qt.MouseButton.LeftButton,Qt.KeyboardModifier.NoModifier,end.toPoint())
    assert .5<js('cropDialog.rightEdge')<.9
    assert .5<js('cropDialog.bottomEdge')<.9
    trigger('applyPhotoCrop');QTest.qWait(100)
    assert not js('cropDialog.opened')
    assert crops[-1][0]=='picture' and 0<crops[-1][3]<1
assert find_visual(w.contentItem(),'cropPhoto_movie').isVisible() is False
# Portrait and panoramic sources also retain a complete, unclipped grab region.
for image_width,image_height in [(480,960),(1600,200)]:
    extra=QImage(image_width,image_height,QImage.Format.Format_RGB32);extra.fill(QColor('#00abc0'))
    extra_path=PREVIEW_DIR/('crop-'+str(image_width)+'.png');assert extra.save(str(extra_path))
    w.setWidth(320);w.setHeight(760)
    count_before=len(crops)
    js('cropDialog.photoSource='+json.dumps(QUrl.fromLocalFile(str(extra_path)).toString())+';cropDialog.open()')
    QTest.qWait(200)
    area=find_visual(w.contentItem(),'cropImageArea');stage=area.parentItem()
    for n in range(4):
        handle=find_visual(w.contentItem(),'cropHandle_'+str(n))
        top=handle.mapToItem(stage,QPoint(0,0));bottom=handle.mapToItem(stage,QPoint(24,24))
        assert top.x()>=0 and top.y()>=0 and bottom.x()<=stage.width() and bottom.y()<=stage.height()
    js('cropDialog.close()');QTest.qWait(50)
    assert len(crops)==count_before  # Cancel/close never applies a crop.
values['current']['status']='closed';lem.changed.emit();QTest.qWait(70)
assert not find_visual(w.contentItem(),'cropPhoto_picture').isVisible()
assert not errors,errors
print('PASS: crop touch geometry/letterboxing, Android360 and desktop, apply callback, image-only/draft-only controls, videos excluded from photo requirements')
delete(engine)
