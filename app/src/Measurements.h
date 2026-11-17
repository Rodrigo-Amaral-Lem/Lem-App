// SPDX-License-Identifier: GPL-3.0-only
// Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

#pragma once
#include <QString>
#include <QRegularExpression>

namespace Measurements {
inline QString canonical(QString id) {
    if (id.startsWith("MAN.")) {
        bool ok = false; const int n = id.mid(4).toInt(&ok);
        if (ok) return QString("PC02.%1").arg(n + 1, 2, 10, QChar('0'));
    }
    return id;
}
inline bool milliamp(const QString &key) { return key == "MAN.11" || key == "MAN.12"; }
inline int digits(const QString &key) {
    if (milliamp(key)) return 6;
    const auto id = canonical(key);
    if (id == "PC02.19") return 5;
    return QRegularExpression("^PC02\\.(0[5-9]|10|11)$").match(id).hasMatch() ? 3 : 0;
}
inline QString unit(const QString &id) {
    return milliamp(id) ? QStringLiteral("mA") : digits(id) == 5 ? QStringLiteral("Ω") : digits(id) ? QStringLiteral("V") : QString();
}
inline bool notApplicable(QString value) {
    value = value.simplified().toUpper();
    value.replace(QStringLiteral("Ã"), "A").replace(QStringLiteral("Á"), "A");
    return value == "NA" || value == "N/A" || value == "NAO APLICAVEL";
}
inline QString format(const QString &id, QString value, bool *valid = nullptr) {
    value = value.trimmed();
    if (id == "MAN.18" && (value.toUpper() == "OK" || value.toUpper() == "NOK")) { if (valid) *valid = true; return value.toUpper(); }
    if (!digits(id)) { if (valid) *valid = true; return value; }
    if (notApplicable(value)) { if (valid) *valid = true; return QStringLiteral("Não Aplicável"); }
    const auto pattern = QString("^([0-9]{1,%1}%2)(?:\\s*%3)?$")
        .arg(digits(id)).arg(milliamp(id) ? QString() : QString("(?:[.,][0-9]{1,2})?"), unit(id));
    const auto match = QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption).match(value);
    if (valid) *valid = match.hasMatch();
    if (!match.hasMatch()) return {};
    auto number = match.captured(1);
    number.replace('.', ',');
    return number + ' ' + unit(id);
}
inline bool outside(const QString &key, const QString &value) {
    const auto id = canonical(key);
    bool valid = false;
    const auto text = format(id, value, &valid);
    if (!valid || digits(id) != 3 || notApplicable(text)) return false;
    auto number = text.section(' ', 0, 0);
    number.replace(',', '.');
    const double n = number.toDouble();
    if (id == "PC02.11") return n >= 50;
    if (id == "PC02.05" || id == "PC02.06" || id == "PC02.07") return n < 360 || n > 440;
    return n < 207 || n > 253;
}
}
