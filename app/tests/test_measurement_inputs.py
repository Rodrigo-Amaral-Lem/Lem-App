# SPDX-License-Identifier: GPL-3.0-only
# Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

"""QML input limits, typed comma/period, NA selection and live voltage NOK boundaries."""
from pathlib import Path
from shiboken6 import delete
source=Path(__file__).with_name("test_report_tabs.py")
prefix=source.read_text().partition("def run_tests():")[0]
exec(compile(prefix,str(source),"exec"),globals())
w.setWidth(390);w.setHeight(844)
js('splashActive=false; page="report"; selectSection(4)')
QTest.qWait(150)
cases=json.loads((PROJECT_ROOT/"shared/measurement-cases.json").read_text())
for case in cases:
    key=json.dumps(case["id"]); raw=json.dumps(case["input"],ensure_ascii=False)
    assert js(f"Measurements.format({key},{raw})")==case.get("formatted"),case
    assert js(f"Measurements.outside({key},{raw})")==case.get("nok",False),case

for key,valid,invalid in [("MAN.07","238,33","238,333"),("MAN.20","4564,4","100000"),("MAN.11","999999","1000000"),("MAN.13","30","30,1"),("MAN.12","999999","1000000"),("MAN.14","0","0,5")]:
    field=find_visual(w.contentItem(),"value_"+key)
    assert field is not None
    field.setProperty("text",valid)
    assert field.property("acceptableInput")
    field.setProperty("text",invalid)
    if key in ("MAN.11", "MAN.12", "MAN.13", "MAN.14"):
        assert find_visual(w.contentItem(), "integerError_" + key).property("visible")
    else:
        assert not field.property("acceptableInput")
    field.setProperty("text",valid)
    QMetaObject.invokeMethod(field,"editingFinished",Qt.ConnectionType.DirectConnection)
    assert values["current"]["payload"]["answers"][key]["value"]==valid+(" V" if key=="MAN.07" else " mA" if key in ("MAN.11","MAN.13") else " ms" if key in ("MAN.12","MAN.14") else " Ω")

# Actual key events exercise the TextInput validator, not just acceptableInput.
for key,typed,expected in [("MAN.07","1234","123"),("MAN.20","123456","12345"),("MAN.07","238,333","238,33"),("MAN.11","1234567","1234567"),("MAN.13","3.0","3.0"),("MAN.13","3,0","3,0")]:
    field=find_visual(w.contentItem(),"value_"+key)
    QMetaObject.invokeMethod(field,"forceActiveFocus",Qt.ConnectionType.DirectConnection)
    QTest.keyClick(w,Qt.Key.Key_A,Qt.KeyboardModifier.ControlModifier)
    QTest.keyClick(w,Qt.Key.Key_Backspace)
    for char in typed:
        QTest.keyClick(w,Qt.Key(ord(char)))
    assert field.property("text")==expected,(key,field.property("text"))

# Paste must keep an invalid reading intact too. A subsequent explicit
# correction is the only way to accept the intended integer and its unit.
app.clipboard().setText("3.0")
QTest.keyClick(w, Qt.Key.Key_A, Qt.KeyboardModifier.ControlModifier)
QTest.keyClick(w, Qt.Key.Key_V, Qt.KeyboardModifier.ControlModifier)
QTest.keyClick(w, Qt.Key.Key_Return)
assert values["current"]["payload"]["answers"]["MAN.13"]["value"] == "3.0"
assert find_visual(w.contentItem(), "integerError_MAN.13").property("visible")
QTest.keyClick(w, Qt.Key.Key_A, Qt.KeyboardModifier.ControlModifier)
QTest.keyClick(w, Qt.Key.Key_3)
QTest.keyClick(w, Qt.Key.Key_Return)
assert values["current"]["payload"]["answers"]["MAN.13"]["value"] == "3 mA"

for key,value,nok in [("MAN.07","207 V",False),("MAN.07","253 V",False),("MAN.07","253,01 V",True),
                       ("MAN.04","360 V",False),("MAN.04","440,01 V",True),
                       ("MAN.10","49,99 V",False),("MAN.10","50 V",True)]:
    lem.answer(key,value,"");QTest.qWait(10)
    label=find_visual(w.contentItem(),"measurementNok_"+key)
    assert label.property("visible")==nok,(key,value,nok)
    if nok:assert label.property("color").name()=="#a12626"
trigger("notApplicable_MAN.10")
assert values["current"]["payload"]["answers"]["MAN.10"]["value"]=="Não Aplicável"
assert not find_visual(w.contentItem(),"measurementNok_MAN.10").property("visible")
# A selected NA has an empty numeric editor; an explicit erase action must clear it.
trigger("clearMeasurement_MAN.10")
assert values["current"]["payload"]["answers"]["MAN.10"]["value"]==""
trigger("notApplicable_MAN.10")
assert js('itemIssues(lem.sections[4].items.find(i=>i.id==="MAN.10")).length')==0
# An unavailable time leaves the chosen test assessment in force.
trip = find_visual(w.contentItem(), "checkpoint_MAN.14")
for assessment, needs_notes in (("OK", False), ("NOK", True)):
    lem.setAssessment("MAN.14", assessment)
    lem.answer("MAN.14", "Não Aplicável", "", True)
    QTest.qWait(10)
    assert trip.property("notesRequired") == needs_notes
    assert trip.property("needsPhoto")
lem.answer("MAN.14", "Não Aplicável", "")
QTest.qWait(10)
assert trip.property("notesRequired") and not trip.property("needsPhoto")
# Saved units never enter the editor, so backspace edits the last digit directly.
lem.answer("MAN.07", "238,3 V", ""); QTest.qWait(10)
field=find_visual(w.contentItem(),"value_MAN.07")
assert field.property("text")=="238,3"
QMetaObject.invokeMethod(field,"forceActiveFocus",Qt.ConnectionType.DirectConnection)
QTest.keyClick(w,Qt.Key.Key_End)
QTest.keyClick(w,Qt.Key.Key_Backspace)
assert field.property("text")=="238,"
QTest.keyClick(w,Qt.Key.Key_4)
QMetaObject.invokeMethod(field,"editingFinished",Qt.ConnectionType.DirectConnection)
assert values["current"]["payload"]["answers"]["MAN.07"]["value"]=="238,4 V"
# Selecting NA leaves a blank numeric editor with NA placeholder; typing replaces it.
trigger("notApplicable_MAN.07"); QTest.qWait(10)
assert field.property("text")==""
assert field.property("placeholderText")=="NA"
assert find_visual(w.contentItem(),"notApplicable_MAN.07").property("text")=="NA"
QMetaObject.invokeMethod(field,"forceActiveFocus",Qt.ConnectionType.DirectConnection)
for char in "230": QTest.keyClick(w,Qt.Key(ord(char)))
QMetaObject.invokeMethod(field,"editingFinished",Qt.ConnectionType.DirectConnection)
assert values["current"]["payload"]["answers"]["MAN.07"]["value"]=="230 V"
# Text fragments such as 'Não apli' cannot be entered with real keys or accepted.
field.setProperty("text", "Não apli")
assert not field.property("acceptableInput")
assert not errors,errors
print("PASS: shared cases, integer mA, input precision, formatting, inclusive voltage limits, N-T >=50 and red NOK/NA UI")
delete(engine)
