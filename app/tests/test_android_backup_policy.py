"""Reports must not enter personal cloud/device-transfer backup datasets."""

from pathlib import Path
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[1] / "android"
android = "{http://schemas.android.com/apk/res/android}"
app = ET.parse(root / "AndroidManifest.xml").getroot().find("application")
assert app.get(android + "allowBackup") == "false"
expected = {
    "root",
    "file",
    "database",
    "sharedpref",
    "external",
    "device_root",
    "device_file",
    "device_database",
    "device_sharedpref",
}
for attr, sections in [
    ("fullBackupContent", [None]),
    ("dataExtractionRules", ["cloud-backup", "device-transfer", "cross-platform-transfer"]),
]:
    ref = app.get(android + attr)
    assert ref.startswith("@xml/")
    rules = ET.parse(root / "res/xml" / (ref.split("/")[1] + ".xml")).getroot()
    for name in sections:
        section = rules if name is None else rules.find(name)
        assert section is not None
        excluded = {e.get("domain") for e in section.findall("exclude") if e.get("path") == "."}
        assert excluded == expected, (attr, name, excluded)
print(
    "PASS: Android manifest/backup resources exclude app data in legacy, cloud, device and cross-platform transfer rules."
)
