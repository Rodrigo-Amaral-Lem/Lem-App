"""Explicit page Flickables retain desktop wheel and keyboard scrolling."""

from pathlib import Path

source = Path(__file__).with_name("test_report_tabs.py")
prefix, _, _ = source.read_text().partition("def run_tests():")
exec(compile(prefix, str(source), "exec"))
from PySide6.QtCore import QPointF
from PySide6.QtGui import QWheelEvent


def checks():
    try:
        js("adminArea.currentIndex=2")
        w.setWidth(1100)
        w.setHeight(760)
        w.setProperty("androidCamera", False)
        w.setProperty("autoHideChrome", False)
        for page, view in [("report", "reportScroll"), ("admin", "adminScroll")]:
            js(f'page="{page}";{view}.contentY=0;{view}.forceActiveFocus()')
            QTest.qWait(150)
            QTest.keyClick(w, Qt.Key.Key_PageDown)
            QTest.qWait(60)
            assert js(view + ".contentY") > 0
            QTest.keyClick(w, Qt.Key.Key_Home)
            QTest.qWait(50)
            assert js(view + ".contentY") == 0
            position = QPointF(1070, 500)
            wheel = QWheelEvent(
                position,
                position,
                QPoint(),
                QPoint(0, -120),
                Qt.MouseButton.NoButton,
                Qt.KeyboardModifier.NoModifier,
                Qt.ScrollPhase.NoScrollPhase,
                False,
            )
            app.sendEvent(w, wheel)
            QTest.qWait(150)
            assert js(view + ".contentY") > 0, ("wheel", page)
            js(view + ".forceActiveFocus()")
            QTest.keyClick(w, Qt.Key.Key_End)
            QTest.qWait(60)
            assert js(view + ".atYEnd")
        assert not errors, errors
        print("PASS: desktop report/admin keyboard paging, Home/End and mouse wheel")
        app.exit(0)
    except Exception:
        import traceback

        traceback.print_exc()
        app.exit(1)


QTimer.singleShot(2400, checks)
rc = app.exec()
del engine
sys.exit(rc)
