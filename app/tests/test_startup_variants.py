"""Exercise exclusive splash choices, real PNG frame decoding, timing and error fallback.

Uses the existing isolated QML backend; no network or user data is touched.
Run with PySide6 and its rcc tool installed: python app/tests/test_startup_variants.py
"""
from pathlib import Path
import subprocess
import tempfile
import PySide6
from PySide6.QtCore import QResource

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="lem-startup-") as tmp:
    tmp = Path(tmp)
    qrc = tmp / "startup.qrc"
    names = [f"{name}-{i}" for name in ["greeting", "checklist", "charging"] for i in range(6)]
    qrc.write_text('<RCC><qresource prefix="/startup">' + ''.join(
        f'<file alias="{name}.png">{root / "app/assets/startup" / (name + ".png")}</file>'
        for name in names) + '</qresource></RCC>')
    rcc = Path(PySide6.__file__).parent / "Qt/libexec/rcc"
    if not rcc.exists():
        rcc = Path(PySide6.__file__).parent / "rcc.exe"
    bundle = tmp / "startup.rcc"
    subprocess.run([str(rcc), "--binary", str(qrc), "-o", str(bundle)], check=True)
    assert QResource.registerResource(str(bundle))
    source = Path(__file__).with_name("test_report_tabs.py")
    prefix = source.read_text().partition("splash_checks = []")[0]
    exec(compile(prefix, str(source), "exec"), globals())

    # Exact boundary coverage, including all 100 equally sized percentage buckets.
    for draw, expected in [(0,1), (.049999,1), (.05,2), (.099999,2),
                           (.10,3), (.109999,3), (.11,0), (.999999,0)]:
        assert js(f"startupVariant({draw})") == expected
    counts = [0, 0, 0, 0]
    for i in range(100):
        counts[int(js(f"startupVariant({(i + .5) / 100})"))] += 1
    assert counts == [89, 5, 5, 1], counts

    for draw, choice, duration in [(.5,0,2000), (.025,1,2000),
                                    (.075,2,2400), (.105,3,2400)]:
        w.setProperty("splashActive", False)
        w.setProperty("startupDraw", draw)
        w.setProperty("splashActive", True)
        QTest.qWait(100)
        assert js("startupChoice") == choice
        assert js("startupLogo.visible") == (choice == 0)
        assert js("startupAnimation.visible") == (choice != 0)
        assert js("splashActive")
        if choice:
            assert js("startupAnimation.status") == 1
            assert js("startupAnimation.frameCount") >= 6
            QTest.qWait(700)
            assert js("startupAnimation.currentFrame") > 0
            remaining = duration - 800
        else:
            remaining = duration - 100
        QTest.qWait(remaining - 300)
        assert js("splashActive"), "splash ended before the final pose"
        QTest.qWait(600)
        assert not js("splashActive")
        assert not js("startupAnimation.playing")
        print("PASS: exclusive startup variant", choice, "duration", duration)

    # The splash is a scene item: completing it must not close any Qt window.
    closed = []
    app.lastWindowClosed.connect(lambda: closed.append(True))
    w.setProperty("startupDraw", .025)
    w.setProperty("splashActive", True)
    QTest.qWait(300)
    shot = w.grabWindow()
    assert shot.pixelColor(4, shot.height() // 2).name() == "#f4f6f7"
    shot.save(str(PREVIEW_DIR / "startup-transparent.png"))
    finished = []
    QTimer.singleShot(2800, lambda: (finished.append(w.isVisible() and not js("splashActive") and not closed), app.quit()))
    app.exec()
    assert finished == [True], (finished, closed)
    w.show()
    print("PASS: app event loop remains alive after splash; no windows closed")

    # Simulate a decoder error: show only the logo and still leave the splash.
    w.setProperty("startupDraw", .025)
    w.setProperty("startupAnimationFailed", True)
    w.setProperty("splashActive", True)
    QTest.qWait(50)
    assert js("startupLogo.visible") and not js("startupAnimation.visible")
    QTest.qWait(2200)
    assert not js("splashActive")
    assert not errors, errors
    print("PASS: 89/5/5/1 boundaries, offline PNG playback and safe logo fallback")

# Dispose QML before the mock backend during Python shutdown.
from shiboken6 import delete
delete(engine)
