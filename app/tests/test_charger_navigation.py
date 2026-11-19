# SPDX-License-Identifier: GPL-3.0-only
# Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

"""Catalogue addresses and Google Maps destinations; HTTPS launches are intercepted locally."""

from pathlib import Path
import json
from urllib.parse import urlparse, parse_qs

source = Path(__file__).with_name("test_report_tabs.py")
prefix = source.read_text().partition("def run_tests():")[0]
exec(compile(prefix, str(source), "exec"))
from PySide6.QtGui import QDesktopServices


class Receiver(QObject):
    urls = []

    @Slot(QUrl)
    def receive(self, url):
        self.urls.append(url.toString())


receiver = Receiver()
QDesktopServices.setUrlHandler("https", receiver, "receive")


def url(details):
    return js("navigationUrl(" + json.dumps(details) + ")")


def target(details):
    result = url(details)
    if not result:
        return ""
    parsed = urlparse(result)
    assert (
        parsed.scheme == "https"
        and parsed.netloc == "www.google.com"
        and parsed.path == "/maps/dir/"
    )
    args = parse_qs(parsed.query)
    assert args["api"] == ["1"] and args["travelmode"] == ["driving"]
    return args["destination"][0]


def checks():
    try:
        address = {"address": "Rua da Estação, 12", "postal_code": "1000-001 Lisboa"}
        assert (
            target({**address, "latitude": "38,7223", "longitude": "-9,1393"}) == "38.7223,-9.1393"
        )
        assert target({"latitude": 0, "longitude": 0}) == "0,0"
        assert js("coordinate(1e-10,90)") == 1e-10
        assert target({"latitude": 1e-10, "longitude": -8.5}) == "0.0000000001,-8.5"
        assert target({"latitude": -90, "longitude": 180}) == "-90,180"
        for lat, lon in [
            ("", ""),
            (None, -9),
            (True, -9),
            ("38oops", -9),
            (91, -9),
            (38, -181),
            ("NaN", 0),
            ("1e2", 0),
        ]:
            assert (
                target({**address, "latitude": lat, "longitude": lon})
                == "Rua da Estação, 12, 1000-001 Lisboa"
            )
        assert (
            target({"address": "Rua A, 1000-001 Lisboa", "postal_code": "1000-001 Lisboa"})
            == "Rua A, 1000-001 Lisboa"
        )
        assert (
            target({"location": "Local de teste", "postal_code": "1000-001"})
            == ""
        )
        assert (
            target({"address": "Rua A & destination=other #?", "postal_code": "1000-001"})
            == "Rua A & destination=other #?, 1000-001"
        )
        assert url({}) == "" and url({"postal_code": "1000-001"}) == ""
        values["chargers"][0]["details"].update(address, latitude="38.72", longitude="-9.13")
        lem.chargersChanged.emit()
        js('page="catalog";showFeedback=false;refreshCatalog(false)')
        for mobile in [False, True]:
            w.setWidth(390 if mobile else 1100)
            w.setHeight(844)
            w.setProperty("androidCamera", mobile)
            QTest.qWait(150)
            label = find_visual(w.contentItem(), "chargerAddress_charger")
            assert "1000-001 Lisboa" in label.property("text")
            button = find_visual(w.contentItem(), "navigateCharger_charger")
            assert button.isVisible() and button.property("enabled")
            assert button.width() >= 44 and button.height() >= 44
            trigger("navigateCharger_charger")
            assert parse_qs(urlparse(receiver.urls[-1]).query)["destination"] == ["38.72,-9.13"]
        values["chargers"][0]["details"] = {**address}
        lem.chargersChanged.emit()
        QTest.qWait(150)
        trigger("navigateCharger_charger")
        assert parse_qs(urlparse(receiver.urls[-1]).query)["destination"] == [
            "Rua da Estação, 12, 1000-001 Lisboa"
        ]
        values["chargers"][0]["details"] = {}
        lem.chargersChanged.emit()
        QTest.qWait(150)
        assert not find_visual(w.contentItem(), "navigateCharger_charger").property("enabled")
        assert not errors, errors
        print(
            "PASS: postal labels desktop/mobile; coordinate priority and validation; encoded address fallback; missing destination; real button launch intercepted without network"
        )
        app.exit(0)
    except Exception:
        import traceback

        traceback.print_exc()
        app.exit(1)
    finally:
        QDesktopServices.unsetUrlHandler("https")


QTimer.singleShot(2400, checks)
rc = app.exec()
del engine
sys.exit(rc)
