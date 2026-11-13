"""Update and blocked-login notices survive transient status messages."""

from pathlib import Path

source = Path(__file__).with_name("test_report_tabs.py")
prefix, _, _ = source.read_text().partition("def run_tests():")
exec(compile(prefix, str(source), "exec"))


def checks():
    try:
        w.setWidth(390)
        w.setHeight(844)
        for allowed in [True, False]:
            values["clientCompatibility"] = {
                "checked": True,
                "compatible": allowed,
                "update_available": allowed,
                "minimum_version": "0.1.28",
                "latest_version": "0.1.33",
                "message": (
                    "Atualize quando possível."
                    if allowed
                    else "Versão incompatível. Instale 0.1.33."
                ),
                "update_url": "https://lem.example.org/download",
            }
            lem.changed.emit()
            lem.compatibilityChanged.emit()
            QTest.qWait(120)
            assert js("versionDialog.opened")
            assert js("versionDialog.title") == (
                "Atualização disponível" if allowed else "Atualização necessária"
            )
            assert js("versionNoticeLabel.text") == values["clientCompatibility"]["message"]
            values["message"] = "Catálogo atualizado."
            lem.changed.emit()
            assert js("versionDialog.opened")
            js("versionDialog.close();loginDialog.open()")
            QTest.qWait(150)
            assert js("accountVersionStatus.visible")
            assert js("accountVersionStatus.text") == values["clientCompatibility"]["message"]
            js("loginDialog.close()")
            QTest.qWait(100)
        assert not errors, errors
        print("PASS: update/blocked notices and persistent account guidance; no QML warnings")
        app.exit(0)
    except Exception:
        import traceback

        traceback.print_exc()
        app.exit(1)


QTimer.singleShot(2400, checks)
rc = app.exec()
del engine
sys.exit(rc)
