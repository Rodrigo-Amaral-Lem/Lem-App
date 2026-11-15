# SPDX-License-Identifier: GPL-3.0-only
from pathlib import Path
source=Path(__file__).with_name('test_report_tabs.py')
exec(compile(source.read_text().partition('def run_tests():')[0],str(source),'exec'))
js('splashActive=false;page="report";statusDialog.open()');QTest.qWait(50)
for count in [0,2,1,0]:
    values['pendingReports']=count;lem.changed.emit();QTest.qWait(30)
    assert find_visual(w.contentItem(),'syncButton').property('text')==f'Sinc. ({count})'
    assert find_visual(w.contentItem(),'pendingReportsLabel').property('text')==f'{count} report(s) por sincronizar'
assert not errors,errors
print('PASS: pending report count updates in footer and operation dialog')
from shiboken6 import delete
delete(engine)
