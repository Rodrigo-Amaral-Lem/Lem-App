# SPDX-License-Identifier: GPL-3.0-only
# Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

from pathlib import Path
source=Path(__file__).with_name('test_report_tabs.py')
exec(compile(source.read_text().partition('def run_tests():')[0],str(source),'exec'))
js('splashActive=false;page="report";selectSection(0)');QTest.qWait(80)
assert [s['id'] for s in values['sections']]==['SEG','SITE','QVE','EST','MAN','CARG','FIN']
for n in range(2,7):
 key=f'SEG.{n:02d}'
 trigger('answer_'+key+'_NOK');QTest.qWait(10)
 assert not find_visual(w.contentItem(),'addPhoto_'+key).isVisible()
 assert not find_visual(w.contentItem(),'toggleNotes_'+key).isVisible()
 assert not find_visual(w.contentItem(),'notes_'+key).isVisible()
 assert js(f'itemIssues(lem.sections[0].items.find(i=>i.id==="{key}")).length')==0
for status in ['OK','NOK','NA']:
 assert find_visual(w.contentItem(),'answer_SEG.07_'+status) is None or not find_visual(w.contentItem(),'answer_SEG.07_'+status).isVisible()
assert find_visual(w.contentItem(),'notes_SEG.07').isVisible()
assert find_visual(w.contentItem(),'addPhoto_SEG.07').isVisible()
assert js('itemIssues(lem.sections[0].items.find(i=>i.id==="SEG.07")).length')==0
assert not errors,errors
print('PASS: group order, SEG.02-06 without attachments/notes, optional SEG.07 with photos/notes and no statuses')
from shiboken6 import delete
delete(engine)
