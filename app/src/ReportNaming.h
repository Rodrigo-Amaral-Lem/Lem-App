// SPDX-License-Identifier: GPL-3.0-only
// Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

#pragma once
#include <QDateTime>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>
#include <QTimeZone>

// Public names are derived from frozen context; UUIDs still address stored reports.
namespace ReportNaming {
inline QString component(QString text, const QString &fallback, int maximum) {
    text = text.normalized(QString::NormalizationForm_C);
    static const QRegularExpression unsafe(QStringLiteral("[\\s<>:\"/\\\\|?*\\x{0000}-\\x{001f}\\x{007f}]+"),
                                           QRegularExpression::UseUnicodePropertiesOption);
    static const QRegularExpression edges(QStringLiteral("^[ ._-]+|[ ._-]+$"));
    text.replace(unsafe, "-");
    text.remove(edges);
    while (text.toUtf8().size() > maximum) text.chop(1);
    if (!text.isEmpty() && text.at(text.size() - 1).isHighSurrogate()) text.chop(1);
    text.remove(edges);
    return text.isEmpty() ? fallback : text;
}

inline QString identifier(const QJsonObject &report) {
    const auto snapshot = report["snapshot"].toObject();
    const auto charger = component(snapshot["charger"].toObject()["external_id"].toString(), "Carregador", 72);
    const auto contract = component(snapshot["contract"].toObject()["name"].toString(), "Contrato", 112);
    if (report["status"] != "closed") return charger + "_Rascunho_" + contract;
    QString stamp = "sem-data_sem-hora";
    for (const auto &value : QStringList{report["finished"].toString(), report["payload"].toObject()["finished"].toString()}) {
        const auto date = QDateTime::fromString(value, Qt::ISODateWithMs);
        if (!date.isValid() || date.timeSpec() == Qt::LocalTime) continue;
        stamp = date.toTimeZone(QTimeZone("Europe/Lisbon")).toString("yyyy-MM-dd_HH-mm-ss");
        break;
    }
    return charger + "_" + stamp + "_" + contract;
}
}
