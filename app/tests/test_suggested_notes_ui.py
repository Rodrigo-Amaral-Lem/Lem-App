# SPDX-License-Identifier: GPL-3.0-only
# Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

"""Clearing generated text must retain the editable observation and its focus.

Uses the isolated QML mock fixture; provenance persistence is covered by the native
measurement tests. The optional OK case catches a hidden editor that required NOK
observations mask because their visibility does not depend on nonempty text.
"""
from pathlib import Path
from shiboken6 import delete

source = Path(__file__).with_name("test_report_tabs.py")
exec(compile(source.read_text().partition("def run_tests():")[0], str(source), "exec"))
js('splashActive=false; page="report"; selectSection(4)')
w.setWidth(390)
w.setHeight(844)
QTest.qWait(100)

key = "MAN.18"
suggestion = "Valor de terra de proteção elevado mas dentro de limites para o calibre da proteção diferencial configurada"


def show_answer(notes, marker=None, assessment="OK"):
    # Blur before a server/model notification: loading a suggestion is not an edit.
    find_visual(w.contentItem(), "value_" + key).forceActiveFocus()
    answer = {"value": "150 Ω", "assessment": assessment, "notes": notes}
    if marker is not None:
        answer["suggested_notes"] = marker
    values["current"]["payload"]["answers"][key] = answer
    lem.changed.emit()
    QTest.qWait(30)
    return find_visual(w.contentItem(), "notes_" + key)


try:
    for assessment in ("OK", "NOK"):
        notes = show_answer(suggestion, suggestion, assessment)
        assert notes.isVisible() and notes.property("text") == suggestion
        notes.forceActiveFocus()
        QTest.qWait(50)
        assert notes.property("text") == "", ("Only the generated text is cleared", assessment, notes.property("text"))
        assert notes.isVisible(), "The empty observation editor must remain open"
        assert notes.hasActiveFocus(), "Typing must continue without a second tap"
        assert find_visual(w.contentItem(), "toggleNotes_" + key).property("checked")
        for char in "Nota pessoal":
            QTest.keyClick(w, ord(char))
        QTest.qWait(30)
        assert values["current"]["payload"]["answers"][key]["notes"] == "Nota pessoal"
        find_visual(w.contentItem(), "value_" + key).forceActiveFocus()
        notes.forceActiveFocus()
        assert notes.property("text") == "Nota pessoal"

    # Wording alone must never authorize removal, including a modified suggestion.
    for text, marker in ((suggestion, None), (suggestion + " - confirmado", suggestion), ("Texto próprio", None)):
        notes = show_answer(text, marker)
        notes.forceActiveFocus()
        QTest.qWait(30)
        assert notes.property("text") == text
        assert notes.isVisible() and notes.hasActiveFocus()

    # Retaining an editor after focus must not prevent explicit removal later.
    notes = show_answer(suggestion, suggestion)
    notes.forceActiveFocus()
    trigger("toggleNotes_" + key)
    assert not notes.isVisible(), "An explicit toggle can still close empty optional notes"
    trigger("toggleNotes_" + key)
    assert notes.isVisible() and notes.hasActiveFocus()

    # Discover defaults from the maintained form so future questions inherit the
    # same regression check automatically, including optional QVE.03 and connectors.
    cases = [
        (index, item["id"], "NA", None, item["na_notes_default"])
        for index, section in enumerate(values["sections"])
        for item in section["items"] if item.get("na_notes_default")
    ]
    cases.extend([
        (4, "MAN.12", "400 mA", "NOK",
         "Valor de intensidade de disparo diferencial fora do calibre configurado"),
        (4, "MAN.18", "200 Ω", "NOK",
         "Valor de terra de proteção elevado e acima do limite para o calibre da proteção diferencial configurada"),
        (4, "MAN.18", "150 Ω", "OK", suggestion),
    ])
    for section_index, question, value, assessment, generated in cases:
        js(f"selectSection({section_index})")
        QTest.qWait(20)
        state_button = find_visual(w.contentItem(), "answer_" + question + "_NA")
        state_button.forceActiveFocus()
        answer = {"value": value, "notes": generated, "suggested_notes": generated}
        if assessment:
            answer["assessment"] = assessment
        values["current"]["payload"]["answers"][question] = answer
        lem.changed.emit()
        QTest.qWait(20)
        editor = find_visual(w.contentItem(), "notes_" + question)
        assert editor.isVisible() and editor.property("text") == generated, question
        editor.forceActiveFocus()
        QTest.qWait(20)
        assert editor.property("text") == "", question
        assert editor.isVisible() and editor.hasActiveFocus(), question
        # Focusing/clearing a suggestion must not change the reading or assessment.
        saved = values["current"]["payload"]["answers"][question]
        assert saved["value"] == value and saved.get("assessment") == assessment, question
        for char in "Nota propria":
            QTest.keyClick(w, ord(char))
        assert saved["notes"] == "Nota propria", question
        state_button.forceActiveFocus()
        editor.forceActiveFocus()
        assert editor.property("text") == "Nota propria", question
        print("PASS:", question, assessment or "N/A", "clear text only; retain editor/focus/custom text")

    assert not errors, errors
    print(f"PASS: all {len(cases)} form suggestions; optional/required focus, custom text and explicit removal")
finally:
    delete(engine)
