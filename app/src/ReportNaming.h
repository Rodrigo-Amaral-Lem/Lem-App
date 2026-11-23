// SPDX-License-Identifier: GPL-3.0-only
// Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

#pragma once
#include <QJsonObject>
#include <QRegularExpression>

// Public names come from the saved report snapshot. UUIDs address the stored records.
namespace ReportNaming {

inline constexpr int maxChargerIdBytes = 72;
inline constexpr int maxContextBytes = 112;
inline constexpr int maxFilenameBytes = 250;

inline QString component(QString text, const QString &fallback, int maxBytes) {
    text = text.normalized(QString::NormalizationForm_C);
    static const QRegularExpression unsafeCharacters(
        QStringLiteral("[\\s<>:\"/\\\\|?*\\x{0000}-\\x{001f}\\x{007f}]+"),
        QRegularExpression::UseUnicodePropertiesOption);
    static const QRegularExpression edgeSeparators(QStringLiteral("^[ ._-]+|[ ._-]+$"));
    text.replace(unsafeCharacters, "-");
    text.remove(edgeSeparators);

    // QString chops one UTF-16 code unit at a time. The byte limit can split a surrogate pair.
    while (text.toUtf8().size() > maxBytes) text.chop(1);
    if (!text.isEmpty() && text.at(text.size() - 1).isHighSurrogate()) text.chop(1);
    text.remove(edgeSeparators);
    return text.isEmpty() ? fallback : text;
}

inline QString identifier(const QJsonObject &report) {
    const auto snapshot = report["snapshot"].toObject();
    const auto chargerId = snapshot["charger"].toObject()["external_id"].toString();
    const auto charger = component(chargerId, "Carregador", maxChargerIdBytes);
    const auto reference = snapshot["intervention"].toObject()["reference"].toString();
    if (!reference.isEmpty()) {
        return charger + "_" + component(reference, "Intervenção", maxContextBytes);
    }
    const auto contractName = snapshot["contract"].toObject()["name"].toString();
    return charger + "_" + component(contractName, "Contrato", maxContextBytes);
}

inline QString evidenceFilename(const QString &identifier, const QString &id, const QString &extension) {
    // UUIDs distinguish originals from the same checkpoint. Keep legacy longer
    // evidence IDs intact and bound the name for filesystem extraction.
    const auto suffix = "_" + id + "." + extension;
    const auto prefixBudget = qMax(qsizetype(1), maxFilenameBytes - suffix.toUtf8().size());
    return component(identifier, "Report", prefixBudget) + suffix;
}

} // namespace ReportNaming
