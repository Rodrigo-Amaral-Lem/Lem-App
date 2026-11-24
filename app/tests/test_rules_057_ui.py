# SPDX-License-Identifier: GPL-3.0-only
# Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

"""Real QML buttons never insert OK/NOK into a mandatory insulation reading."""
from pathlib import Path
source=Path(__file__).with_name('test_report_tabs.py')
exec(compile(source.read_text().partition('def run_tests():')[0],str(source),'exec'))
js('splashActive=false;page="report";selectSection(4)');QTest.qWait(100)
lem.answer('MAN.02','Texto de um rascunho antigo','');QTest.qWait(20)
assert 'Escolha OK, NOK ou N/A' in js('itemIssues(lem.sections[4].items.find(i=>i.id==="MAN.02")).join()')
for status in ['OK','NOK','NA']:
 trigger('answer_MAN.02_'+status);QTest.qWait(20)
 assert not find_visual(w.contentItem(),'value_MAN.02').isVisible()
 assert find_visual(w.contentItem(),'notes_MAN.02').isVisible() == (status == 'NOK')
for n in range(16,20):
 key=f'MAN.{n}'
 trigger('answer_'+key+'_OK');QTest.qWait(20)
 a=values['current']['payload']['answers'][key]
 assert a['value']=='' and a['assessment']=='OK'
 assert js(f'itemIssues(lem.sections[4].items.find(i=>i.id==="{key}")).length')>0
 field=find_visual(w.contentItem(),'value_'+key)
 assert field.property('placeholderText') == 'Valor medido (ou NA)'
 QMetaObject.invokeMethod(field,'forceActiveFocus',Qt.ConnectionType.DirectConnection)
 for char in '600,5':QTest.keyClick(w,Qt.Key(ord(char)))
 assert a['value']=='600,5',a
 trigger('answer_'+key+'_NOK');QTest.qWait(20)
 assert a['value']=='600,5' and a['assessment']=='NOK'
 assert find_visual(w.contentItem(),'notes_'+key).isVisible()
 assert 'observação' in js(f'itemIssues(lem.sections[4].items.find(i=>i.id==="{key}")).join()')
 lem.answer(key,'600,5','Valor registado');QTest.qWait(20)
 assert js(f'itemIssues(lem.sections[4].items.find(i=>i.id==="{key}")).join()') == 'Falta fotografia'
 values['current']['photos'].append({'id':'photo-'+key, 'checkpoint':key, 'mime':'image/png', 'uploaded':True})
 lem.changed.emit();QTest.qWait(20)
 assert js(f'itemIssues(lem.sections[4].items.find(i=>i.id==="{key}")).length')==0
 trigger('answer_'+key+'_NA');QTest.qWait(20)
 assert a['value']=='NA' and not a.get('assessment')
 assert js(f'itemIssues(lem.sections[4].items.find(i=>i.id==="{key}")).length')==0
for key,reading in [('MAN.13','30 mA'),('MAN.20','120')]:
 trigger('answer_'+key+'_OK')
 a=values['current']['payload']['answers'][key]
 assert a['value']=='' and a['assessment']=='OK',a
 lem.answer(key,reading,'Teste efetuado')
 trigger('answer_'+key+'_NOK')
 assert a['value']==reading and a['assessment']=='NOK',a
 lem.answer(key,reading+'1','Teste efetuado')
 assert a['assessment']=='NOK',a
 trigger('answer_'+key+'_NA')
 assert a['value']=='NA' and not a.get('assessment'),a
assert find_visual(w.contentItem(),'value_MAN.20').property('text') == 'NA'
for status in ['OK','NOK','NA']:
 trigger('answer_MAN.21_'+status)
 assert values['current']['payload']['answers']['MAN.21']['value']==status
assert 'fotografia' not in js('itemIssues(lem.sections[4].items.find(i=>i.id==="MAN.13")).join()')
# Required notes can be cancelled or cleared without changing their requirement.
for key, value, assessment in [('MAN.02','NOK',None), ('MAN.13','NA',None), ('MAN.16','600','NOK')]:
 lem.answer(key,value,'Observação registada')
 if assessment: lem.setAssessment(key,assessment)
 QTest.qWait(30)
 before=values['current']['payload']['answers'][key].copy()
 trigger('toggleNotes_'+key)
 assert js('removeNotesDialog.opened')
 trigger('cancelRemoveNotes')
 assert values['current']['payload']['answers'][key]==before
 trigger('toggleNotes_'+key)
 trigger('confirmRemoveNotes')
 after=values['current']['payload']['answers'][key]
 assert after['notes']=='' and after['value']==before['value'],after
 assert after.get('assessment')==before.get('assessment')
 assert find_visual(w.contentItem(),'notes_'+key).isVisible()
 assert 'observação' in js(f'itemIssues(lem.sections[4].items.find(i=>i.id==="{key}")).join()')
assert not errors,errors
print('PASS: real typed readings survive OK/NOK, incomplete values stay pending, NOK notes, NA exemption and MAN.13 choices')
from shiboken6 import delete
delete(engine)
