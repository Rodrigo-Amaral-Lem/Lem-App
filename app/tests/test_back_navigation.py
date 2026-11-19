# SPDX-License-Identifier: GPL-3.0-only
# Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

"""Android Back must navigate without taking over text-editing Backspace."""
from pathlib import Path

source = Path(__file__).with_name("test_report_tabs.py")
prefix, _, _ = source.read_text().partition("def run_tests():")
exec(compile(prefix, str(source), "exec"))


def checks():
    try:
        w.setProperty("androidCamera", True)
        js('splashActive=false; page="report"')
        QTest.qWait(150)
        shortcut = w.findChild(QObject, "reportBackShortcut")
        assert shortcut is not None
        sequences = shortcut.property("sequences")
        assert len(sequences) == 1 and str(sequences[0]) == "Back", sequences
        QTest.keyClick(w, Qt.Key.Key_Backspace)
        QTest.qWait(100)
        assert js("page") == "report"
        js('reportScroll.forceActiveFocus()')
        QTest.keyPress(w, Qt.Key.Key_Back)
        assert js("page") == "report"
        QTest.keyRelease(w, Qt.Key.Key_Back)
        QTest.qWait(150)
        assert js("page") == "catalog"
        assert not errors, errors
        print("PASS: Android Back navigates; Backspace does not leave the report; no QML warnings")
        app.exit(0)
    except Exception:
        import traceback
        traceback.print_exc()
        app.exit(1)


QTimer.singleShot(2400, checks)
rc = app.exec()
del engine
sys.exit(rc)
