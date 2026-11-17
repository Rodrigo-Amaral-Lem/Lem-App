# SPDX-License-Identifier: GPL-3.0-only
# Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.
"""Public report references fit narrow catalogues and remain readable in the report."""
from pathlib import Path
from shiboken6 import delete
source = Path(__file__).with_name("test_report_tabs.py")
exec(compile(source.read_text(encoding="utf-8").partition("def run_tests():")[0], str(source), "exec"), globals())
identifier = "ESTAÇÃO-001_2026-11-16_17-35-08_Contrato-Norte-" + "Longo-" * 12
values["current"]["identifier"] = identifier
values["chargers"][0]["reports"] = [{"id": "report-demo", "status": "closed", "identifier": identifier}]
lem.changed.emit()
js('splashActive=false')
for width in (320, 390, 1100):
    w.setWidth(width)
    w.setHeight(844)
    js('page="catalog"')
    QTest.qWait(180)
    button = find_visual(w.contentItem(), "catalogReport_report-demo")
    assert button is not None and button.isVisible()
    assert identifier in button.property("text")
    assert button.mapToScene(QPoint(int(button.width()), 0)).x() <= width, (width, button.width(), button.x(), button.parentItem().width())
    js('page="report"')
    QTest.qWait(100)
    reference = find_visual(w.contentItem(), "reportIdentifier")
    assert reference.isVisible() and reference.property("text") == identifier
    assert reference.mapToScene(QPoint(int(reference.width()), 0)).x() <= width
assert not errors, errors
print("PASS: public report identifiers in catalogue and report, including long mobile names")
delete(engine)
