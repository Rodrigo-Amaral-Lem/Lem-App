# SPDX-License-Identifier: GPL-3.0-only
# Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

"""Render all seven groups and exercise QVE confirmation and signature canvas."""
from pathlib import Path
source=Path(__file__).with_name('test_report_tabs.py')
exec(compile(source.read_text().partition('def run_tests():')[0],str(source),'exec'))
import base64

def checks():
    try:
        w.setProperty("splashActive", False)
        w.setWidth(390 if 'mobile' in sys.argv else 1100); w.setHeight(844 if 'mobile' in sys.argv else 820)
        assert [s['id'] for s in values['sections']]==['SEG','SITE','QVE','EST','MAN','CARG','FIN']
        for index,section in enumerate(values['sections']):
            js(f'selectSection({index})');QTest.qWait(30)
            assert js(f'sectionPanels.itemAt({index}).checkpoints.count')==len(section['items'])
        js('selectSection(2)');QTest.qWait(30)
        trigger('copyQveGroup');QTest.qWait(30)
        assert js('qveCopyDialog.visible')
        assert js('qveCopyDialog.sources.length')==1
        js('qveCopyDialog.accept()');QTest.qWait(30)
        assert qve_copy_calls==['other-report']
        js('selectSection(6)');QTest.qWait(30)
        lem.answer('FIN.02','Técnico de teste','')
        trigger('sign_FIN.02');QTest.qWait(80)
        assert js('signatureDialog.visible')
        canvas=find_visual(w.contentItem(),'signatureCanvas')
        assert canvas is not None
        # Paint normalized strokes through the Canvas's real 2D renderer.
        canvas.setProperty('points',[{'x':0.12,'y':0.7,'start':True},{'x':0.3,'y':0.2},{'x':0.38,'y':0.8},{'x':0.6,'y':0.25},{'x':0.8,'y':0.55}])
        canvas.setProperty('marks',4)
        QMetaObject.invokeMethod(canvas,'requestPaint');QTest.qWait(100)
        w.grabWindow().save('/tmp/lem-signature-'+('mobile' if 'mobile' in sys.argv else 'desktop')+'.png')
        trigger('saveSignature');QTest.qWait(30)
        assert signature_calls and signature_calls[-1][0]=='FIN.02'
        png=base64.b64decode(signature_calls[-1][1].split(',',1)[1])
        image=QImage.fromData(png)
        assert not image.isNull()
        dark=sum(image.pixelColor(x,y).lightness()<150 for y in range(image.height()) for x in range(image.width()))
        assert dark>30
        assert not js('signatureDialog.visible')
        assert not errors, errors
        print('PASS: seven groups, QVE copy confirmation, FIN.02 signature PNG (mobile)')
        from shiboken6 import delete
        delete(engine)
        app.exit(0)
    except Exception:
        import traceback;traceback.print_exc();app.exit(1)
QTimer.singleShot(100,checks)
sys.exit(app.exec())
