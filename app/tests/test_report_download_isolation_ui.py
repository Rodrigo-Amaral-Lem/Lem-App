# SPDX-License-Identifier: GPL-3.0-only
"""Report actions use the selected report's download state, on desktop and mobile."""
from pathlib import Path
from shiboken6 import delete
source=Path(__file__).with_name('test_report_tabs.py')
exec(compile(source.read_text(encoding='utf-8').partition('def run_tests():')[0],str(source),'exec'))
for width,height in [(1100,800),(390,844)]:
    w.setWidth(width); w.setHeight(height)
    js('splashActive=false; page="report"')
    values['downloadingPhotos']=True
    values['currentReportDownloadingPhotos']=False
    lem.changed.emit(); QTest.qWait(100)
    assert js('networkActivity') and not js('reportNetworkActivity')
    js('reportMenu.open()'); QTest.qWait(30)
    assert find_visual(w.contentItem(),'deleteReportAction').property('enabled')
    trigger('deleteReportAction')
    assert find_visual(w.contentItem(),'confirmDeleteReport').property('enabled')
    values['currentReportDownloadingPhotos']=True
    lem.changed.emit(); QTest.qWait(30)
    assert not find_visual(w.contentItem(),'confirmDeleteReport').property('enabled')
    trigger('cancelDeleteReport')
    js('reportMenu.open()'); QTest.qWait(30)
    assert not find_visual(w.contentItem(),'deleteReportAction').property('enabled')
    js('reportMenu.close()')
    values['currentReportDownloadingPhotos']=False
    values['current']['_contract_unavailable']='Contrato em espera de eliminação. Dados locais conservados.'
    lem.changed.emit(); QTest.qWait(30)
    assert 'Sincronização suspensa' in find_visual(w.contentItem(),'reportSaveState').property('text')
    assert find_visual(w.contentItem(),'contractSyncNotice').isVisible()
    del values['current']['_contract_unavailable']
assert not errors,errors
print('PASS: other-report downloads permit deletion; same-report downloads protect open confirmation; retention notice; desktop/mobile')
delete(engine)
