"""Client logo controls: optional brands, separate issuer and duplicate prevention."""
from pathlib import Path
source = Path(__file__).with_name('test_report_tabs.py')
exec(compile(source.read_text(encoding='utf-8').partition('def run_tests():')[0], str(source), 'exec'), globals())
js('splashActive=false; page="admin"')
QTest.qWait(40)  # Let the initial contract presentation populate the controls.
primary = w.findChild(QObject, 'primaryReportLogo')
secondary = w.findChild(QObject, 'secondaryReportLogo')
assert primary.property('count') == 12
assert primary.property('currentText') == 'Nenhum'
assert 'Lem' not in primary.property('model')
assert not secondary.property('enabled')
assert secondary.property('currentText') == 'Nenhum'
primary.setProperty('currentIndex', primary.property('model').index('APS'))
QTest.qWait(30)
assert primary.property('currentText') == 'APS'
model = secondary.property('model')
assert 'APS' not in model and 'Galp' in model
secondary.setProperty('currentIndex', model.index('Galp'))
QTest.qWait(30)
assert secondary.property('currentText') == 'Galp'
primary.setProperty('currentIndex', primary.property('model').index('Galp'))
QTest.qWait(30)
assert primary.property('currentText') == 'Galp'
assert secondary.property('currentIndex') == 0
primary.setProperty('currentIndex', 0)
QTest.qWait(30)
assert secondary.property('currentIndex') == 0
assert not secondary.property('enabled')
assert not errors, errors
print('PASS: separate issuer, 11 optional client brands, duplicate prevention and selection reset')
from shiboken6 import delete
delete(engine)
