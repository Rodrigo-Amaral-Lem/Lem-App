"""Real QML manual assessments, N/A notes, scoped copy locks and multi-file pickers."""
from pathlib import Path
source=Path(__file__).with_name('test_report_tabs.py')
exec(compile(source.read_text().partition('def run_tests():')[0],str(source),'exec'))

def checks():
 try:
    w.setProperty('splashActive',False);w.setWidth(390);w.setHeight(844)
    js('selectSection(4)');QTest.qWait(50)
    for number in range(14,19):
        key=f'MAN.{number:02d}'
        trigger('answer_'+key+'_OK');assert values['current']['payload']['answers'][key].get('assessment')=='OK'
        trigger('answer_'+key+'_NOK');assert values['current']['payload']['answers'][key].get('assessment')=='NOK'
        assert find_visual(w.contentItem(),'notes_'+key).isVisible()
    assert js('Measurements.format("MAN.18","OK")')=='OK'
    assert js('Measurements.format("MAN.18","NOK")')=='NOK'
    assert js('Measurements.format("MAN.18","124,4")')=='124,4 Ω'
    trigger('copyMeasurementsGroup');QTest.qWait(30)
    assert js('qveCopyDialog.scope')=='MAN'
    js('qveCopyDialog.accept()');assert measurement_copy_calls==['other-report']
    values['copyingGroup']='MAN';lem.changed.emit();QTest.qWait(20)
    assert not find_visual(w.contentItem(),'checkpoint_MAN.04').property('enabled')
    assert find_visual(w.contentItem(),'checkpoint_MAN.18').property('enabled')
    js('selectSection(3)');QTest.qWait(20)
    assert find_visual(w.contentItem(),'checkpoint_EST.01').property('enabled')
    js('selectSection(2)');QTest.qWait(20)
    assert find_visual(w.contentItem(),'copyQveGroup').property('enabled')
    trigger('copyQveGroup');js('qveCopyDialog.accept()')
    assert qve_copy_calls==['other-report']
    values['copyingGroup']='QVE';lem.changed.emit();js('selectSection(4)');QTest.qWait(20)
    assert find_visual(w.contentItem(),'copyMeasurementsGroup').property('enabled')
    trigger('copyMeasurementsGroup');js('qveCopyDialog.accept()')
    assert measurement_copy_calls==['other-report','other-report']
    values['copyingGroup']='';lem.changed.emit()
    js('selectSection(5)');QTest.qWait(30)
    for key in ('CARG.01.01','CARG.05'):
        trigger('answer_'+key+'_NA');QTest.qWait(20)
        assert find_visual(w.contentItem(),'notes_'+key).isVisible()
        assert js(f'itemIssues(lem.sections[5].items.find(i=>i.id==="{key}")).join()').find('N/A')>=0
        lem.answer(key,'NA','Sem veículo compatível');QTest.qWait(20)
        assert js(f'itemIssues(lem.sections[5].items.find(i=>i.id==="{key}")).length')==0
        lem.answer(key,'OK','');QTest.qWait(20)
        assert 'fotografia' in js(f'itemIssues(lem.sections[5].items.find(i=>i.id==="{key}")).join()')
    assert js('photoDialog.fileMode === FileDialog.OpenFiles')
    assert js('videoDialog.fileMode === FileDialog.OpenFiles')
    assert not errors,errors
    print('PASS: MAN.14-18 manual OK/NOK, N/A observation/photo rules, measurement copy action, scoped editing and multi-file picker modes')
    from shiboken6 import delete
    delete(engine);app.exit(0)
 except Exception:
    import traceback;traceback.print_exc();app.exit(1)
QTimer.singleShot(100,checks)
sys.exit(app.exec())
