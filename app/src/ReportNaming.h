// SPDX-License-Identifier: GPL-3.0-only
// Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

#pragma once
#include <QJsonObject>
#include <QRegularExpression>

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
    return charger + "_" + contract;
}

inline QString evidenceFilename(const QString &identifier, const QString &id, const QString &extension) {
    // UUIDs distinguish originals from the same checkpoint. Keep legacy longer
    // evidence IDs intact and bound the name for filesystem extraction.
    const auto suffix = "_" + id + "." + extension;
    return component(identifier, "Report", qMax(qsizetype(1), 250 - suffix.toUtf8().size())) + suffix;
}
}
