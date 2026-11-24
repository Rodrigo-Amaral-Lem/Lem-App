# SPDX-License-Identifier: GPL-3.0-only
# Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

from pathlib import Path
source=Path(__file__).with_name('test_report_tabs.py')
exec(compile(source.read_text().partition('def run_tests():')[0],str(source),'exec'))
js('splashActive=false;page="report"');w.setWidth(390);w.setHeight(844)
for index,key in [(1,'SITE.06'),(2,'QVE.14'),(3,'EST.18'),(4,'MAN.24'),(5,'CARG.07'),(6,'FIN.03')]:
 js(f'selectSection({index})');QTest.qWait(30)
 assert find_visual(w.contentItem(),'notes_'+key).isVisible()
 assert find_visual(w.contentItem(),'addPhoto_'+key).isVisible()
 assert find_visual(w.contentItem(),'otherAttachments_'+key).isVisible()
 assert not find_visual(w.contentItem(),'answer_'+key+'_OK').isVisible()
 assert js(f'itemIssues(lem.sections[{index}].items.find(i=>i.id==="{key}")).length')==0
js('selectSection(6)');lem.answer('FIN.02','Tecnico','');QTest.qWait(30)
assert 'Falta assinatura do técnico' in js('itemIssues(lem.sections[6].items.find(i=>i.id==="FIN.02")).join()')
assert find_visual(w.contentItem(),'notes_FIN.02').isVisible()
assert not find_visual(w.contentItem(),'addPhoto_FIN.02').isVisible()
assert not find_visual(w.contentItem(),'otherAttachments_FIN.02').isVisible()
button=find_visual(w.contentItem(),'importSignature_FIN.02')
assert button.isVisible() and button.property('enabled')
trigger('importSignature_FIN.02');assert js('signaturePhotoDialog.visible')
js('signaturePhotoDialog.reject();selectSection(3)')
fixture=PREVIEW_DIR/'row-photo.png';im=QImage(60,40,QImage.Format.Format_RGB32);im.fill(QColor('cyan'));assert im.save(str(fixture))
values['current']['photos']=[{'id':'row-photo','checkpoint':'EST.01','mime':'image/png','uploaded':False,'path':QUrl.fromLocalFile(str(fixture)).toString()}];lem.changed.emit();QTest.qWait(150)
buttons=[find_visual(w.contentItem(),p+'_row-photo') for p in ['enlargePhoto','removePhoto','editPhoto']]
assert all(b.isVisible() for b in buttons)
assert len({b.property('y') for b in buttons})==1
assert all(b.width() >= 44 and b.height() >= 44 for b in buttons)
assert min(b.x() for b in buttons) >= 0
assert max(b.x() + b.width() for b in buttons) <= 140
assert not errors,errors
print('PASS: optional general notes/media, desktop signature photo selector and three photo buttons on one line')
from shiboken6 import delete
delete(engine)
