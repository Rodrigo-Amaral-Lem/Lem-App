// SPDX-License-Identifier: GPL-3.0-only
// Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

#pragma once
#include <QRegularExpression>
#include <QString>

namespace Measurements {

inline QString canonical(QString id) {
    if (id.startsWith("MAN.")) {
        bool numeric = false;
        const int questionNumber = id.mid(4).toInt(&numeric);
        if (numeric) return QString("PC02.%1").arg(questionNumber + 1, 2, 10, QChar('0'));
    }
    return id;
}

inline bool milliamp(const QString &key) {
    return key == "MAN.11" || key == "MAN.12";
}

inline int digits(const QString &key) {
    // Only MAN.11 and MAN.12 use mA. Other MAN fields reuse their PC02 rules.
    if (milliamp(key) || key == "ms") return 6;
    const auto id = canonical(key);
    if (id == "PC02.19") return 5;
    return QRegularExpression("^PC02\\.(0[5-9]|10|11)$").match(id).hasMatch() ? 3 : 0;
}

inline QString unit(const QString &id) {
    if (id == "ms") return QStringLiteral("ms");
    if (milliamp(id)) return QStringLiteral("mA");
    const int maxDigits = digits(id);
    if (maxDigits == 5) return QStringLiteral("Ω");
    return maxDigits ? QStringLiteral("V") : QString();
}

inline bool notApplicable(QString value) {
    value = value.simplified().toUpper();
    value.replace(QStringLiteral("Ã"), "A").replace(QStringLiteral("Á"), "A");
    return value == "NA" || value == "N/A" || value == "NAO APLICAVEL";
}

inline QString format(const QString &id, QString value, bool *valid = nullptr) {
    value = value.trimmed();
    if (id == "MAN.18" && (value.toUpper() == "OK" || value.toUpper() == "NOK")) {
        if (valid) *valid = true;
        return value.toUpper();
    }
    const int maxDigits = digits(id);
    if (!maxDigits) {
        if (valid) *valid = true;
        return value;
    }
    if (notApplicable(value)) {
        if (valid) *valid = true;
        return QStringLiteral("Não Aplicável");
    }

    const auto expectedUnit = unit(id);
    const auto optionalFraction = (milliamp(id) || id == "ms") ? QString() : QString("(?:[.,][0-9]{1,2})?");
    const auto pattern = QString("^([0-9]{1,%1}%2)(?:\\s*%3)?$")
        .arg(maxDigits).arg(optionalFraction, expectedUnit);
    const auto match = QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption).match(value);
    if (valid) *valid = match.hasMatch();
    if (!match.hasMatch()) return {};
    auto number = match.captured(1);
    number.replace('.', ',');
    return number + ' ' + expectedUnit;
}

inline bool outside(const QString &key, const QString &value) {
    const auto id = canonical(key);
    bool valid = false;
    const auto text = format(id, value, &valid);
    if (!valid || digits(id) != 3 || notApplicable(text)) return false;
    auto number = text.section(' ', 0, 0);
    number.replace(',', '.');
    const double voltage = number.toDouble();
    if (id == "PC02.11") return voltage >= 50;
    if (id == "PC02.05" || id == "PC02.06" || id == "PC02.07") return voltage < 360 || voltage > 440;
    return voltage < 207 || voltage > 253;
}

} // namespace Measurements
