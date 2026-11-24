# SPDX-License-Identifier: GPL-3.0-only
# Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

"""Combined photo editor at Android/desktop sizes, using a mock backend.

Native data-safety tests separately verify image pixels and persistence.
"""
from pathlib import Path
from shiboken6 import delete

source = Path(__file__).with_name("test_report_tabs.py")
prefix = source.read_text().partition("def run_tests():")[0]
# Offscreen Windows has no platform font default and can choose a symbol font.
# Load the installed UI font so screenshots and touch layouts remain readable.
prefix = prefix.replace('app = QGuiApplication(sys.argv)', '''
app = QGuiApplication(sys.argv)
if os.name == "nt":
    from PySide6.QtGui import QFont, QFontDatabase
    font_path = Path(os.environ.get("WINDIR", "C:/Windows")) / "Fonts/segoeui.ttf"
    if font_path.exists():
        font_id = QFontDatabase.addApplicationFont(str(font_path))
        families = QFontDatabase.applicationFontFamilies(font_id)
        if families:
            app.setFont(QFont(families[0], 10))
''')
prefix = prefix.replace('Lem = type("Lem", (QObject,), attrs)', '''
edits = []
accept_edits = True

def editPhoto(self, photo_id, turns, x, y, width, height):
    edits.append((photo_id, turns, x, y, width, height))
    return accept_edits

attrs["editPhoto"] = Slot(str, int, float, float, float, float, result=bool)(editPhoto)
video_calls = []

def takeVideo(self, key): video_calls.append(key)
attrs["takeVideo"] = Slot(str)(takeVideo)
attrs["openVideo"] = Slot(str)(lambda self, key: None)
Lem = type("Lem", (QObject,), attrs)''')
exec(compile(prefix, str(source), "exec"), globals())
station_section = next(i for i, section in enumerate(values["sections"]) if section["id"] == "EST")
js('splashActive=false; page="report"; selectSection(' + str(station_section) + ')')
fixture = QImage(640, 480, QImage.Format.Format_RGB32)
fixture.fill(QColor('#00abc0'))
# An asymmetric image makes clockwise direction visible in saved screenshots.
for y in range(100):
    for x in range(200):
        fixture.setPixelColor(x, y, QColor('#e57c32'))
path = PREVIEW_DIR / 'edit.png'
assert fixture.save(str(path))
values['current']['photos'] = [
    dict(id='picture', checkpoint='EST.01', mime='image/png', path=QUrl.fromLocalFile(str(path)).toString(), uploaded=False),
    dict(id='movie', checkpoint='EST.01', mime='video/mp4', path='file:///tmp/example.mp4', uploaded=False),
]
lem.changed.emit()
QTest.qWait(200)
assert js('photographsFor("EST.01").length') == 1


def check_crop_geometry():
    area = find_visual(w.contentItem(), 'cropImageArea')
    assert area and area.isVisible() and area.width() > 0 and area.height() > 0
    stage = area.parentItem()
    # Handles and expanded touch regions must fit the clipped stage in every orientation.
    for name in ['cropHandle_' + str(n) for n in range(4)] + ['cropTouchArea']:
        item = find_visual(w.contentItem(), name)
        top = item.mapToItem(stage, QPoint(0, 0))
        bottom = item.mapToItem(stage, QPoint(int(item.width()), int(item.height())))
        assert top.x() >= -.01 and top.y() >= -.01, (name, top)
        assert bottom.x() <= stage.width() + .01 and bottom.y() <= stage.height() + .01, (name, bottom)
    return area


def drag(start, end):
    QTest.mousePress(w, Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier, start.toPoint())
    QTest.mouseMove(w, end.toPoint(), 20)
    QTest.mouseRelease(w, Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier, end.toPoint())


def toggle_crop():
    box = find_visual(w.contentItem(), 'enablePhotoCrop')
    point = box.mapToScene(QPoint(int(box.width() / 2), int(box.height() / 2)))
    QTest.mouseClick(w, Qt.MouseButton.LeftButton, Qt.KeyboardModifier.NoModifier, point.toPoint())
    QTest.qWait(30)


for width, height in [(320, 760), (360, 760), (640, 360), (1100, 820)]:
    w.setWidth(width)
    w.setHeight(height)
    js('androidCamera=true')
    QTest.qWait(100)
    actions = [find_visual(w.contentItem(), name + '_EST.01')
               for name in ['addPhoto', 'otherAttachments', 'toggleNotes']]
    assert all(action.width() >= 48 and action.height() >= 48 for action in actions)
    for button in actions:
        left = button.mapToScene(QPoint(0, 0)).x()
        assert left >= 0 and left + button.width() <= width + 1
    if width == 320:
        assert actions[0].y() < actions[1].y() < actions[2].y()
    trigger('otherAttachments_EST.01')
    assert js('attachmentSheet.opened') and js('photoItem') == 'EST.01'
    trigger('recordAttachmentVideo')
    assert not js('attachmentSheet.visible')
    assert video_calls[-1] == 'EST.01'
    assert find_visual(w.contentItem(), 'rotatePhoto_picture') is None
    assert find_visual(w.contentItem(), 'cropPhoto_picture') is None
    editor_button = find_visual(w.contentItem(), 'editPhoto_picture')
    assert editor_button.property('symbol') == 'photoEdit'
    if width == 360:
        # Render the actual Canvas icon for visual inspection, not a substitute asset.
        from PySide6.QtCore import QSize
        icon_preview = editor_button.grabToImage(QSize(176, 176))
        QTest.qWait(80)
        assert icon_preview.saveToFile(str(PREVIEW_DIR / 'photo-edit-button.png'))

    # Rotation alone never requires a crop selection.
    trigger('editPhoto_picture')
    QTest.qWait(150)
    assert js('cropDialog.opened') and js('cropDialog.width') <= width
    assert not js('cropDialog.cropEnabled') and js('cropDialog.quarterTurns') == 0
    assert not find_visual(w.contentItem(), 'cropImageArea').isVisible()
    trigger('rotatePhotoPreview')
    assert js('cropDialog.quarterTurns') == 1
    if width == 360:
        w.grabWindow().save(str(PREVIEW_DIR / "edit-rotate-preview.png"))
    trigger('applyPhotoCrop')
    QTest.qWait(70)
    assert not js('cropDialog.opened')
    assert edits[-1] == ('picture', 1, 0, 0, 1, 1)

    trigger('editPhoto_picture')
    QTest.qWait(150)
    toggle_crop()
    assert js('cropDialog.cropEnabled')
    area = check_crop_geometry()
    # Drag the outer half of the corner; the hit target extends beyond the image.
    drag(area.mapToScene(QPoint(-8, -8)), area.mapToScene(QPoint(int(area.width() * .1), int(area.height() * .1))))
    assert js('cropDialog.leftEdge') > 0 and js('cropDialog.topEdge') > 0
    js('cropDialog.resetSelection()')
    drag(area.mapToScene(QPoint(int(area.width() - 2), int(area.height() - 2))),
         area.mapToScene(QPoint(int(area.width() * .7), int(area.height() * .7))))
    right, bottom = js('cropDialog.rightEdge'), js('cropDialog.bottomEdge')
    assert .5 < right < .9 and .5 < bottom < .9
    trigger('rotatePhotoPreview')
    QTest.qWait(40)
    assert abs(js('cropDialog.leftEdge') - (1 - bottom)) < 1e-8
    assert abs(js('cropDialog.bottomEdge') - right) < 1e-8
    check_crop_geometry()
    if width == 360:
        w.grabWindow().save(str(PREVIEW_DIR / "edit-crop-preview.png"))
    trigger('applyPhotoCrop')
    QTest.qWait(70)
    assert not js('cropDialog.opened')
    assert edits[-1][0:2] == ('picture', 1)
    assert abs(edits[-1][4] - bottom) < 1e-8 and abs(edits[-1][5] - right) < 1e-8

assert not find_visual(w.contentItem(), 'editPhoto_movie').isVisible()

# Portrait/panorama sources in all orientations. Turning crop off saves the whole
# image even if a smaller selection was previously drawn.
for image_width, image_height in [(480, 960), (1600, 200)]:
    extra = QImage(image_width, image_height, QImage.Format.Format_RGB32)
    extra.fill(QColor('#00abc0'))
    extra_path = PREVIEW_DIR / ('edit-' + str(image_width) + '.png')
    assert extra.save(str(extra_path))
    w.setWidth(320)
    w.setHeight(760)
    count_before = len(edits)
    js('cropDialog.photoSource=' + json.dumps(QUrl.fromLocalFile(str(extra_path)).toString()) + ';cropDialog.open()')
    QTest.qWait(200)
    toggle_crop()
    for turn in range(4):
        check_crop_geometry()
        trigger('rotatePhotoPreview')
        QTest.qWait(30)
    assert js('cropDialog.quarterTurns') == 0
    js('cropDialog.rightEdge=.5; cropDialog.bottomEdge=.5')
    toggle_crop()
    trigger('applyPhotoCrop')
    QTest.qWait(70)
    assert len(edits) == count_before + 1 and edits[-1] == ('picture', 0, 0, 0, 1, 1)

trigger('editPhoto_picture')
QTest.qWait(150)
toggle_crop()
trigger('rotatePhotoPreview')
js('cropDialog.rightEdge=.5')
trigger('resetPhotoCrop')
assert not js('cropDialog.cropEnabled') and js('cropDialog.quarterTurns') == 0
assert js('cropDialog.leftEdge') == 0 and js('cropDialog.rightEdge') == 1
count_before = len(edits)
trigger('rotatePhotoPreview')
trigger('cancelPhotoCrop')
QTest.qWait(70)
assert len(edits) == count_before and not js('cropDialog.opened')

# A rejected Save must preserve the open preview, including sync starting or a
# different report becoming current while the editor is open.
trigger('editPhoto_picture')
QTest.qWait(150)
for blocked in ['syncing', 'refreshing', 'attachingFiles', 'currentReportDownloadingPhotos']:
    values[blocked] = True
    lem.changed.emit()
    assert not find_visual(w.contentItem(), 'applyPhotoCrop').isEnabled()
    values[blocked] = False
    lem.changed.emit()
values['current']['id'] = 'another-report'
lem.changed.emit()
assert not js('cropDialog.canApply')
values['current']['id'] = 'report-demo'
lem.changed.emit()
accept_edits = False
trigger('applyPhotoCrop')
QTest.qWait(70)
assert js('cropDialog.opened')
trigger('cancelPhotoCrop')
QTest.qWait(70)

values['current']['status'] = 'closed'
lem.changed.emit()
QTest.qWait(70)
edit_button = find_visual(w.contentItem(), 'editPhoto_picture')
assert edit_button is None or not edit_button.isVisible()
assert not errors, errors
print('PASS: unified editor, optional crop, rotation/crop gestures, all orientations, cancel/reset, guarded Save, image/draft controls')
print('Previews:', PREVIEW_DIR)
delete(engine)
