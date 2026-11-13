"""Calendar years must never use locale grouping, on desktop or Android."""
from pathlib import Path
from PySide6.QtCore import QLocale

source = Path(__file__).with_name("test_report_tabs.py")
prefix = source.read_text().partition("def run_tests():")[0]
exec(compile(prefix, str(source), "exec"), globals())
js('splashActive=false; page="admin"')
year_control = w.findChild(QObject, "contractYear")
assert year_control is not None
for locale in ["en_US", "pt_PT", "de_DE", "fr_FR"]:
    year_control.setProperty("locale", QLocale(locale))
    for year_value in [2020, 2026, 2100]:
        year_control.setProperty("value", year_value)
        QTest.qWait(20)
        assert year_control.property("displayText") == str(year_value), locale
assert not errors, errors
print("PASS: year without grouping in US, Portuguese, German and French locales")
from shiboken6 import delete
delete(engine)
