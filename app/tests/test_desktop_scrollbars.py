# SPDX-License-Identifier: GPL-3.0-only
# Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

"""Page scrollbars remain visible outside content and support desktop mouse dragging."""
from pathlib import Path
import copy

source=Path(__file__).with_name('test_report_tabs.py')
prefix=source.read_text().partition('def run_tests():')[0]
exec(compile(prefix,str(source),'exec'))

def checks():
    try:
        js("adminArea.currentIndex=2")
        w.setWidth(1100);w.setHeight(820)
        w.setProperty('androidCamera',False)
        w.setProperty('autoHideChrome',False)
        template=copy.deepcopy(values['chargers'][0])
        values['chargers']=[{**copy.deepcopy(template),'id':f'charger-{i}','external_id':f'ID-{i:03d}'} for i in range(30)]
        lem.chargersChanged.emit()
        for page,view_name in [('report','reportScroll'),('catalog','chargerList'),('admin','adminScroll')]:
            js(f'page="{page}";showFeedback=false;{view_name}.contentY=0')
            QTest.qWait(220)
            view=find_visual(w.contentItem(),view_name)
            bar=find_visual(w.contentItem(),view_name+'Bar')
            assert bar is not None and bar.isVisible(),page
            assert bar.property('interactive')
            p=bar.mapToScene(QPoint(0,0))
            edge=view.mapToScene(QPoint(int(view.width()),0))
            assert p.x()>=edge.x(),('bar overlaps cards',page,p.x(),edge.x())
            assert p.y()>=w.contentItem().mapToScene(QPoint(0,0)).y()
            footer=find_visual(w.contentItem(),'appFooter')
            assert p.y()+bar.height()<=footer.mapToScene(QPoint(0,0)).y()
            start=QPoint(int(p.x()+bar.width()/2),int(p.y()+8))
            end=QPoint(start.x(),int(p.y()+bar.height()*.7))
            before=js(f'{view_name}.contentY')
            QTest.mousePress(w,Qt.MouseButton.LeftButton,Qt.KeyboardModifier.NoModifier,start)
            QTest.mouseMove(w,end,100)
            QTest.qWait(60)
            QTest.mouseRelease(w,Qt.MouseButton.LeftButton,Qt.KeyboardModifier.NoModifier,end)
            QTest.qWait(80)
            assert js(f'{view_name}.contentY')>before,('drag did not scroll',page)
            js('chargerList.positionViewAtBeginning()' if page=='catalog' else f'{view_name}.contentY=0');QTest.qWait(80)
            assert bar.property('position')<.01,(page,bar.property('position'),js(f'{view_name}.contentY'),js(f'{view_name}.originY'))
            for other in ['reportScroll','chargerList','adminScroll']:
                if other!=view_name:assert not find_visual(w.contentItem(),other+'Bar').isVisible()
        values['chargers']=[];lem.chargersChanged.emit();js('page="catalog"');QTest.qWait(150)
        assert not find_visual(w.contentItem(),'chargerListBar').isVisible()
        w.setWidth(390);w.setProperty('androidCamera',True);w.setProperty('autoHideChrome',True)
        js('page="report"');QTest.qWait(150)
        assert not find_visual(w.contentItem(),'reportScrollBar').isVisible()
        assert find_visual(w.contentItem(),'pagePositionBar').isVisible()
        assert not errors,errors
        print('PASS: report/catalogue/management desktop bars outside cards, mouse dragging, active-page visibility, empty catalogue and Android indicator')
        app.exit(0)
    except Exception:
        import traceback;traceback.print_exc();app.exit(1)
QTimer.singleShot(2400,checks)
rc=app.exec()
del engine
sys.exit(rc)
