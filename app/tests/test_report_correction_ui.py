# SPDX-License-Identifier: GPL-3.0-only
from pathlib import Path
from shiboken6 import delete
source=Path(__file__).with_name('test_report_tabs.py')
prefix=source.read_text(encoding='utf-8').partition('def run_tests():')[0]
prefix=prefix.replace('Lem = type("Lem", (QObject,), attrs)', '''
correction_calls=[]
date_calls=[]
def beginReportCorrection(self): correction_calls.append(True)
def setCorrectionTimes(self,started,finished): date_calls.append((started,finished)); return True
attrs['beginReportCorrection']=Slot()(beginReportCorrection)
attrs['setCorrectionTimes']=Slot(str,str,result=bool)(setCorrectionTimes)
Lem = type("Lem", (QObject,), attrs)
''')
exec(compile(prefix,str(source),'exec'),globals())
values['authenticated']=True
values['current'].update(status='closed',dirty=False,revision=3)
lem.changed.emit();js('splashActive=false;page="report";reportMenu.open()');QTest.qWait(80)
action=find_visual(w.contentItem(),'correctClosedReportAction')
assert action.isVisible() and action.property('enabled')
trigger('correctClosedReportAction'); QTest.qWait(40)
assert js('beginCorrectionDialog.opened')
js('beginCorrectionDialog.accept()');assert correction_calls==[True]
for platform,role in [(False,'admin'),(True,'tech')]:
    values['desktopCorrections']=platform; values['user']['role']=role;lem.changed.emit();QTest.qWait(30)
    assert not action.isVisible()
values['desktopCorrections']=True;values['user']['role']='admin'
values['current'].update(status='draft',correction={'state':'editing','editor_id':'demo','source_id':'old'})
values['current']['payload'].update(started='2026-10-01T09:00:00+01:00',finished='2026-10-01T11:30:12+01:00')
lem.changed.emit();QTest.qWait(40)
assert js('editing') and js('correctingReport')
js('reportMenu.open()');trigger('correctionDatesAction');QTest.qWait(40)
assert js('correctionStart.text')=='2026-10-01T09:00:00+01:00'
trigger('saveCorrectionDates'); assert len(date_calls)==1
assert not js('correctionDatesDialog.opened')
values['desktopCorrections']=False;lem.changed.emit();QTest.qWait(40)
assert not js('editing')
assert not errors,errors
print('PASS: desktop/admin correction actions, date editing, confirmation and mobile/read-only guards')
delete(engine)
