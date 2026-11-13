"""Answer typing/status updates must not destroy and reload unchanged thumbnails."""
from pathlib import Path
import os
from shiboken6 import isValid, delete

source = Path(__file__).with_name("test_report_tabs.py")
prefix = source.read_text().partition("def run_tests():")[0]
baseline = os.getenv("LEM_PERF_QML_ROOT")
if baseline:
    prefix = prefix.replace('PROJECT_ROOT = Path(__file__).resolve().parents[2]',
                            f'PROJECT_ROOT = Path({baseline!r})')
exec(compile(prefix, str(source), "exec"), globals())
js('splashActive=false; page="report"; selectedSection=0')
w.setWidth(390)
w.setHeight(844)
fixture = PREVIEW_DIR / "performance-photo.png"
im = QImage(64, 48, QImage.Format.Format_RGB32)
im.fill(QColor("#00a6bb"))
assert im.save(str(fixture))
values["current"]["photos"] = [{"id": "perf-photo", "checkpoint": "EST.01",
    "uploaded": False, "path": QUrl.fromLocalFile(str(fixture)).toString()}]
lem.changed.emit()
QTest.qWait(150)
original = find_visual(w.contentItem(), "removePhoto_perf-photo")
assert original is not None
for i in range(40):
    lem.answer("FIN.03", "typed note " + str(i), "")
    QTest.qWait(2)
assert isValid(original), "Unchanged photograph delegate was destroyed while typing"
assert find_visual(w.contentItem(), "removePhoto_perf-photo") == original
for i in range(100):
    values["message"] = "Operation status " + str(i)
    lem.changed.emit()
assert isValid(original)
assert not errors, errors
lem.removePhoto("perf-photo")
QTest.qWait(50)
assert find_visual(w.contentItem(), "removePhoto_perf-photo") is None
print("PASS: same photo delegate survives 40 edits and 100 status updates; removal still updates UI")
delete(engine)
