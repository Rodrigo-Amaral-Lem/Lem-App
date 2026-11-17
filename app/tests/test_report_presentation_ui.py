# SPDX-License-Identifier: GPL-3.0-only
"""Real QML: load existing choices, change issuer/logos, keep unsaved choices, admin only."""
from pathlib import Path
from shiboken6 import delete
source = Path(__file__).with_name('test_report_tabs.py')
prefix = source.read_text().partition('def run_tests():')[0]
prefix = prefix.replace('Lem = type("Lem", (QObject,), attrs)', '''
presentation_calls = []
def saveReportPresentation(self,issuer,primary,secondary,revision):
    presentation_calls.append((issuer,primary,secondary,revision))
attrs["saveReportPresentation"] = Slot(str,str,str,int)(saveReportPresentation)
Lem = type("Lem", (QObject,), attrs)
''')
exec(compile(prefix, str(source), 'exec'), globals())
values['authenticated'] = True
values['contracts'][0]['presentation'] = {'issuer':'CSMTEC', 'revision':3, 'client_logos':[{'id':'APS'}, {'id':'Galp'}]}
lem.changed.emit()
js('splashActive=false;page="admin";adminArea.currentIndex=1')
QTest.qWait(60)
assert js('reportIssuer.currentText') == 'CSMTEC'
assert js('primaryLogo.currentText') == 'APS'
assert js('secondaryLogo.currentText') == 'Galp'
for width, height in [(320,700), (390,844), (1050,850)]:
    w.setWidth(width); w.setHeight(height); QTest.qWait(80)
    for name in ('reportIssuer','primaryReportLogo','secondaryReportLogo','saveReportPresentation','reportPresentationSaveStatus'):
        control = find_visual(w.contentItem(),name)
        assert control.mapToScene(QPoint(int(control.width()),0)).x() <= w.width()+1, (name,width)
    trigger('saveReportPresentation')
    assert presentation_calls[-1] == ('CSMTEC','APS','Galp',3)
js('reportIssuer.currentIndex=0;primaryLogo.currentIndex=primaryLogo.model.indexOf("Transdev")')
lem.changed.emit(); QTest.qWait(50)
assert js('reportIssuer.currentText') == 'Lem'  # Unrelated status signals do not reset edits.
assert js('primaryLogo.currentText') == 'Transdev'
trigger('saveReportPresentation')
assert presentation_calls[-1] == ('Lem','Transdev','',3)
values['busy'] = True
values['reportPresentationSave'] = {'contract_id':'contract','state':'saving','message':'A confirmar o formato guardado no servidor…'}
lem.changed.emit(); QTest.qWait(30)
assert not find_visual(w.contentItem(),'saveReportPresentation').property('enabled')
assert not js('reportIssuer.enabled') and not js('primaryLogo.enabled')
values['busy'] = False
values['reportPresentationSave'] = {'contract_id':'contract','state':'error','message':'O servidor recusou a gravação do formato.'}
values['message'] = 'Catálogo atualizado. Pode trabalhar sem rede.'
lem.changed.emit(); QTest.qWait(30)
assert 'recusou' in find_visual(w.contentItem(),'reportPresentationSaveStatus').property('text')
assert js('reportIssuer.hasUnsavedChanges')
assert js('reportIssuer.confirmedIssuer') == 'CSMTEC'
values['contracts'][0]['presentation'] = {'issuer':'Lem','revision':4,'client_logos':[]}
values['reportPresentationSave'] = {'contract_id':'contract','state':'saved','message':'Guardado no servidor: Lem.'}
lem.changed.emit(); QTest.qWait(50)
assert js('primaryLogo.currentIndex') == 0
assert js('secondaryLogo.currentIndex') == 0
assert js('reportIssuer.presentationRevision') == 4
assert find_visual(w.contentItem(),'reportPresentationSaveStatus').property('text') == 'Guardado no servidor: Lem.'
# Catalogue/progress messages do not erase the save confirmation, and downloads
# of evidence do not block independent contract settings.
values['message'] = 'Catálogo atualizado. Pode trabalhar sem rede.'
values['downloadingPhotos'] = True
lem.changed.emit(); QTest.qWait(30)
assert find_visual(w.contentItem(),'saveReportPresentation').property('enabled')
assert 'Guardado no servidor: Lem.' == find_visual(w.contentItem(),'reportPresentationSaveStatus').property('text')
values['authenticated'] = False
lem.changed.emit(); QTest.qWait(30)
assert not find_visual(w.contentItem(),'saveReportPresentation').property('enabled')
values['authenticated'] = True
values['downloadingPhotos'] = False
values['user']['role']='tech'; lem.changed.emit(); QTest.qWait(50)
assert not find_visual(w.contentItem(),'saveReportPresentation').isVisible()
assert not errors, errors
print('PASS: issuer/logos, saved versus unsaved selection, persistent success/error feedback, in-flight controls, independent downloads, mobile/desktop widths, session/admin gates')
delete(engine)
