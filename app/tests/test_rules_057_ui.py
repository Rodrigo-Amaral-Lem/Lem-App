"""Real QML buttons never insert OK/NOK into a mandatory insulation reading."""
from pathlib import Path
source=Path(__file__).with_name('test_report_tabs.py')
exec(compile(source.read_text().partition('def run_tests():')[0],str(source),'exec'))
js('splashActive=false;page="report";selectSection(4)');QTest.qWait(100)
for n in range(14,18):
 key=f'MAN.{n}'
 trigger('answer_'+key+'_OK');QTest.qWait(20)
 a=values['current']['payload']['answers'][key]
 assert a['value']=='' and a['assessment']=='OK'
 assert js(f'itemIssues(lem.sections[4].items.find(i=>i.id==="{key}")).length')>0
 field=find_visual(w.contentItem(),'value_'+key)
 QMetaObject.invokeMethod(field,'forceActiveFocus',Qt.ConnectionType.DirectConnection)
 for char in '600,5':QTest.keyClick(w,Qt.Key(ord(char)))
 assert a['value']=='600,5',a
 trigger('answer_'+key+'_NOK');QTest.qWait(20)
 assert a['value']=='600,5' and a['assessment']=='NOK'
 assert find_visual(w.contentItem(),'notes_'+key).isVisible()
 assert 'observação' in js(f'itemIssues(lem.sections[4].items.find(i=>i.id==="{key}")).join()')
 lem.answer(key,'600,5','Valor registado');QTest.qWait(20)
 assert js(f'itemIssues(lem.sections[4].items.find(i=>i.id==="{key}")).length')==0
 trigger('answer_'+key+'_NA');QTest.qWait(20)
 assert a['value']=='NA' and not a.get('assessment')
 assert js(f'itemIssues(lem.sections[4].items.find(i=>i.id==="{key}")).length')==0
for key,reading in [('MAN.12','30 mA'),('MAN.18','120')]:
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
for status in ['OK','NOK','NA']:
 trigger('answer_MAN.19_'+status)
 assert values['current']['payload']['answers']['MAN.19']['value']==status
assert 'fotografia' in js('itemIssues(lem.sections[4].items.find(i=>i.id==="MAN.12")).join()')
assert not errors,errors
print('PASS: real typed readings survive OK/NOK, incomplete values stay pending, NOK notes, NA exemption and MAN.12 choices')
from shiboken6 import delete
delete(engine)
