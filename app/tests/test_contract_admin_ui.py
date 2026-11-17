# SPDX-License-Identifier: GPL-3.0-only
"""Desktop batch export and administrator technician-password controls (mock API)."""
from pathlib import Path
from shiboken6 import delete

source = Path(__file__).with_name("test_report_tabs.py")
prefix = source.read_text().partition("def run_tests():")[0]
prefix = prefix.replace('app = QGuiApplication(sys.argv)', '''
app = QGuiApplication(sys.argv)
if os.name == "nt":
    from PySide6.QtGui import QFont, QFontDatabase
    font_path = Path(os.environ.get("WINDIR", "C:/Windows")) / "Fonts/segoeui.ttf"
    font_id = QFontDatabase.addApplicationFont(str(font_path))
    families = QFontDatabase.applicationFontFamilies(font_id)
    if families:
        app.setFont(QFont(families[0], 10))
''')
prefix = prefix.replace('Lem = type("Lem", (QObject,), attrs)', '''
exports, resets, cancellations = [], [], []
def exportContract(self, cid, kind, directory): exports.append((cid, kind, directory.toString()))
def cancelContractExport(self): cancellations.append(True)
def resetTechnicianPassword(self, uid, password):
    resets.append((uid, password))
    return True
attrs["exportContract"] = Slot(str, str, QUrl)(exportContract)
attrs["cancelContractExport"] = Slot()(cancelContractExport)
attrs["resetTechnicianPassword"] = Slot(str, str, result=bool)(resetTechnicianPassword)
Lem = type("Lem", (QObject,), attrs)''')
exec(compile(prefix, str(source), "exec"), globals())
values['authenticated'] = True
values['users'] = [dict(id='admin', role='admin', label='Administrador'),
                   dict(id='technician', role='tech', label='Técnico de teste • tecnico@example.test')]
lem.changed.emit()
js('splashActive=false; page="catalog"')
w.setWidth(1100)
w.setHeight(820)
QTest.qWait(100)
for kind, name in [('pdf', 'exportContractPdfs'), ('bundle', 'exportContractZips')]:
    button = find_visual(w.contentItem(), name)
    assert button.isVisible() and button.isEnabled()
    # Test picker context without opening a native dialog from an offscreen test.
    js('contractExportFolder.contractId="contract"; contractExportFolder.kind=' + json.dumps(kind))
    js('contractExportFolder.selectedFolder=' + json.dumps(QUrl.fromLocalFile(str(PREVIEW_DIR)).toString()))
    js('contractExportFolder.accepted()')
    assert exports[-1][0:2] == ('contract', kind), exports
w.grabWindow().save(str(PREVIEW_DIR / 'contract-export-desktop.png'))
values['contractExport'] = dict(active=True, directory=str(PREVIEW_DIR), message='A exportar 1 / 3', total=3, saved=0)
lem.changed.emit()
assert not find_visual(w.contentItem(), 'exportContractPdfs').isEnabled()
assert find_visual(w.contentItem(), 'cancelContractExport').isVisible()
trigger('cancelContractExport')
assert len(cancellations) == 1
values['contractExport']['active'] = False
values['desktopExports'] = False
lem.changed.emit()
assert not find_visual(w.contentItem(), 'exportContractPdfs').isVisible()
assert not find_visual(w.contentItem(), 'exportContractZips').isVisible()

# Password resetting is available to admins on both desktop and mobile, but only
# for a technician; changing the selected account never changes a pending target.
for width, height in [(1100, 820), (390, 844)]:
    w.setWidth(width)
    w.setHeight(height)
    js('page="admin"; adminArea.currentIndex=2; userBox.currentIndex=0')
    QTest.qWait(100)
    assert not find_visual(w.contentItem(), 'resetTechnicianPasswordButton').isEnabled()
    js('userBox.currentIndex=1')
    trigger('resetTechnicianPasswordButton')
    QTest.qWait(80)
    assert js('resetPasswordDialog.opened') and js('resetPasswordDialog.userId') == 'technician'
    js('resetPassword.text="SyntheticPassword123!"; repeatPassword.text="mismatch"')
    assert not find_visual(w.contentItem(), 'confirmResetTechnicianPassword').isEnabled()
    js('repeatPassword.text=resetPassword.text')
    assert find_visual(w.contentItem(), 'confirmResetTechnicianPassword').isEnabled()
    if width == 390:
        w.grabWindow().save(str(PREVIEW_DIR / 'password-reset-mobile.png'))
    js('userBox.currentIndex=0')
    trigger('confirmResetTechnicianPassword')
    QTest.qWait(80)
    assert resets[-1] == ('technician', 'SyntheticPassword123!')
    assert not js('resetPasswordDialog.opened')
    assert js('resetPassword.text') == '' and js('repeatPassword.text') == ''

before = len(resets)
js('userBox.currentIndex=1')
trigger('resetTechnicianPasswordButton')
QTest.qWait(80)
js('resetPassword.text="DiscardedPassword123!"; repeatPassword.text=resetPassword.text; resetPasswordDialog.close()')
QTest.qWait(80)
assert len(resets) == before and js('resetPassword.text') == ''
values['user']['role'] = 'tech'
lem.changed.emit()
assert not find_visual(w.contentItem(), 'resetTechnicianPasswordButton').isVisible()
assert not errors, errors
print('PASS: desktop-only contract export, captured picker context, progress/cancel and admin-only technician reset with masked/cleared passwords')
print('Previews:', PREVIEW_DIR)
delete(engine)
