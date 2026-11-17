// SPDX-License-Identifier: GPL-3.0-only
// Current maintenance assessments and suggestion provenance; mirror maintenance_rules.py.
#pragma once
#include "Measurements.h"
#include <QJsonObject>
#include <QMap>

namespace MaintenanceRules {
inline QString tripNote() { return QStringLiteral("Valor de intensidade de disparo diferencial fora do calibre configurado"); }
inline QString earthFailNote() { return QStringLiteral("Valor de terra de proteção elevado e acima do limite para o calibre da proteção diferencial configurada"); }
inline QString earthHighNote() { return QStringLiteral("Valor de terra de proteção elevado mas dentro de limites para o calibre da proteção diferencial configurada"); }
inline bool untouched(const QJsonObject &answer) {
    const auto marker = answer["suggested_notes"].toString();
    return !marker.isEmpty() && marker == answer["notes"].toString();
}
inline void clearSuggestion(QJsonObject &answer) {
    if (untouched(answer)) answer["notes"] = "";
    answer.remove("suggested_notes");
}
inline void suggest(QJsonObject &answer, const QString &note) {
    if (!note.isEmpty() && answer["notes"].toString().trimmed().isEmpty()) {
        answer["notes"] = note;
        answer["suggested_notes"] = note;
    }
}
// Integer cross-products make inclusive 50%/105% boundaries and the earth
// resistance boundary exact, even where division would produce a recurring decimal.
inline qint64 reading(const QJsonObject &answers, const QString &key, int scale = 1) {
    const auto answer = answers[key].toObject();
    if (answer.contains("previous_value")) return -1; // Ambiguous legacy units need confirmation.
    bool valid = false;
    const auto value = Measurements::format(key, answer["value"].toString(), &valid);
    if (!valid || Measurements::notApplicable(value) || value == "OK" || value == "NOK") return -1;
    auto number = value.section(' ', 0, 0).replace(',', '.');
    bool numeric = false;
    const double parsed = number.toDouble(&numeric);
    return numeric ? qRound64(parsed * scale) : -1;
}
struct Assessment { QString state; QString note; };
inline QMap<QString, Assessment> calculate(const QJsonObject &answers) {
    QMap<QString, Assessment> result;
    const qint64 configured = reading(answers, "MAN.11");
    if (configured <= 0) return result;
    const qint64 trip = reading(answers, "MAN.12");
    if (trip >= 0) {
        const bool failed = trip * 100 >= configured * 105 || trip * 2 <= configured;
        result["MAN.12"] = {failed ? "NOK" : "OK", failed ? tripNote() : QString()};
    }
    const qint64 earth = reading(answers, "MAN.18", 100);
    if (earth >= 0) {
        // User-selected temporary criterion: 50 V. The 25 V public-outdoor
        // exception is documented; choosing the installation category is deferred.
        const bool failed = earth * configured > 50LL * 100000;
        result["MAN.18"] = {failed ? "NOK" : "OK", failed ? earthFailNote() : earth > 5000 ? earthHighNote() : QString()};
    }
    return result;
}
inline QJsonObject apply(QJsonObject answers, bool offerSuggestions) {
    const auto calculated = calculate(answers);
    for (const auto &key : {QString("MAN.12"), QString("MAN.18")}) {
        if (!answers.contains(key)) continue;
        auto answer = answers[key].toObject();
        if (calculated.contains(key)) {
            const auto target = calculated[key];
            if (answer["assessment"].toString() != target.state ||
                (untouched(answer) && answer["suggested_notes"].toString() != target.note))
                clearSuggestion(answer);
            answer["assessment"] = target.state;
            answer["auto_assessment"] = target.state;
            if (offerSuggestions) suggest(answer, target.note);
        } else if (answer.contains("auto_assessment")) {
            if (answer["assessment"] == answer["auto_assessment"]) answer.remove("assessment");
            answer.remove("auto_assessment");
            clearSuggestion(answer);
        }
        answers[key] = answer;
    }
    return answers;
}
}
