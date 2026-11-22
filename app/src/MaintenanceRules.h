// SPDX-License-Identifier: GPL-3.0-only
// Current maintenance assessments and suggestion provenance mirror maintenance_rules.py.
#pragma once
#include "Measurements.h"
#include <QJsonObject>
#include <QMap>

namespace MaintenanceRules {

inline QString tripNote() {
    return QStringLiteral("Valor de intensidade de disparo diferencial fora do calibre configurado");
}

inline QString earthFailNote() {
    return QStringLiteral("Valor de terra de proteção elevado e acima do limite "
                          "para o calibre da proteção diferencial configurada");
}

inline QString earthHighNote() {
    return QStringLiteral("Valor de terra de proteção elevado mas dentro de limites "
                          "para o calibre da proteção diferencial configurada");
}

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

// -1 marks an unusable reading, including legacy units awaiting confirmation.
inline qint64 reading(const QJsonObject &answers, const QString &key, int scale = 1) {
    const auto answer = answers[key].toObject();
    if (answer.contains("previous_value")) return -1;
    bool valid = false;
    const auto value = Measurements::format(key, answer["value"].toString(), &valid);
    if (!valid || Measurements::notApplicable(value) || value == "OK" || value == "NOK") return -1;
    auto number = value.section(' ', 0, 0).replace(',', '.');
    bool numeric = false;
    const double parsed = number.toDouble(&numeric);
    return numeric ? qRound64(parsed * scale) : -1;
}

struct Assessment {
    QString state;
    QString note;
};

inline QMap<QString, Assessment> calculate(const QJsonObject &answers) {
    QMap<QString, Assessment> result;
    const qint64 configuredMilliamps = reading(answers, "MAN.11");
    if (configuredMilliamps <= 0) return result;

    const qint64 tripMilliamps = reading(answers, "MAN.12");
    if (tripMilliamps >= 0) {
        // Both endpoints fail. Cross-multiplication keeps 50% and 105% exact.
        const bool outsideTripBand = tripMilliamps * 100 >= configuredMilliamps * 105
                                     || tripMilliamps * 2 <= configuredMilliamps;
        result["MAN.12"] = outsideTripBand ? Assessment{"NOK", tripNote()} : Assessment{"OK", {}};
    }

    constexpr int hundredthsPerOhm = 100;
    const qint64 earthHundredths = reading(answers, "MAN.18", hundredthsPerOhm);
    if (earthHundredths >= 0) {
        // Scale the form's temporary 50 V limit to hundredths of an ohm × mA.
        // Common/docs/report-contract.md describes the 25 V outdoor exception.
        constexpr qint64 scaledVoltageLimit = 50LL * hundredthsPerOhm * 1000;
        if (earthHundredths * configuredMilliamps > scaledVoltageLimit) {
            result["MAN.18"] = {"NOK", earthFailNote()};
        } else if (earthHundredths > 50 * hundredthsPerOhm) {
            result["MAN.18"] = {"OK", earthHighNote()};
        } else {
            result["MAN.18"] = {"OK", {}};
        }
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
                (untouched(answer) && answer["suggested_notes"].toString() != target.note)) {
                clearSuggestion(answer);
            }
            answer["assessment"] = target.state;
            answer["auto_assessment"] = target.state;
            if (offerSuggestions) suggest(answer, target.note);
        } else if (answer.contains("auto_assessment")) {
            // A reading disappeared. Keep any assessment the technician has since changed.
            if (answer["assessment"] == answer["auto_assessment"]) answer.remove("assessment");
            answer.remove("auto_assessment");
            clearSuggestion(answer);
        }
        answers[key] = answer;
    }
    return answers;
}

} // namespace MaintenanceRules
