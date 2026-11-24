// SPDX-License-Identifier: GPL-3.0-only
// Upgrade editable v6 reports. The caller filters frozen uploads before migration.
#pragma once
#include "MaintenanceRules.h"
#include <QJsonArray>
#include <QFile>
#include <QJsonDocument>
#include <QStringList>

namespace MaintenanceMigration {

inline QString checkpoint(const QString &key) {
    if (key == "MAN.21") return QString("MAN.22");
    if (key == "EST.10") return QString("EST.18");
    return key;
}

inline QString joinedNotes(const QStringList &texts) {
    QStringList unique;
    for (const auto &text : texts) {
        if (!text.isEmpty() && !unique.contains(text)) unique.append(text);
    }
    return unique.join('\n');
}

// An explicit A or mA reading must fit six whole mA digits. An empty result requires confirmation.
inline QString legacyMilliamps(const QString &raw) {
    const auto match = QRegularExpression(
        "^([0-9]{1,9}(?:[.,][0-9]{1,6})?)\\s*(mA|A)$",
        QRegularExpression::CaseInsensitiveOption).match(raw);
    if (!match.hasMatch()) return {};

    // Keep the fractional digits as an integer ratio so conversion never rounds.
    const auto parts = match.captured(1).replace(',', '.').split('.');
    const auto fraction = parts.size() > 1 ? parts[1] : QString();
    qint64 denominator = 1;
    for (int digit = 0; digit < fraction.size(); ++digit) denominator *= 10;

    const qint64 scaledReading = parts[0].toLongLong() * denominator + fraction.toLongLong();
    const int unitScale = match.captured(2).compare("A", Qt::CaseInsensitive) == 0 ? 1000 : 1;
    const qint64 numerator = scaledReading * unitScale;
    if (numerator % denominator != 0 || numerator / denominator > 999999) return {};
    return QString::number(numerator / denominator) + " mA";
}

inline QJsonObject payload(QJsonObject result) {
    auto answers = result["answers"].toObject();
    auto retired = result["retired_answers"].toObject();
    if (answers.contains("MAN.21")) {
        const auto formerAnswer = answers.take("MAN.21");
        if (!answers.contains("MAN.22")) {
            answers["MAN.22"] = formerAnswer;
        } else if (answers["MAN.22"] != formerAnswer) {
            // Keep the full former answer as well as its text in the visible general notes.
            retired["MAN.21"] = formerAnswer;
            auto general = answers["MAN.22"].toObject();
            const auto formerFields = formerAnswer.toObject();
            general["notes"] = joinedNotes({
                general["notes"].toString(),
                formerFields["value"].toString(),
                formerFields["notes"].toString(),
            });
            answers["MAN.22"] = general;
        }
    }

    if (answers.contains("EST.10")) {
        const auto formerAnswer = answers.take("EST.10");
        retired["EST.10"] = formerAnswer;
        const auto formerNotes = formerAnswer.toObject()["notes"].toString();
        if (!formerNotes.trimmed().isEmpty()) {
            auto general = answers["EST.18"].toObject();
            if (general.isEmpty()) general = QJsonObject{{"value", ""}, {"notes", ""}};
            general["notes"] = joinedNotes({general["notes"].toString(), formerNotes});
            answers["EST.18"] = general;
        }
    }

    for (const auto &key : {QString("MAN.11"), QString("MAN.12")}) {
        if (!answers.value(key).isObject()) continue;
        auto answer = answers[key].toObject();
        const auto raw = answer["value"].toString().trimmed();
        if (raw.isEmpty() || Measurements::notApplicable(raw)) continue;
        const auto milliamps = legacyMilliamps(raw);
        if (!milliamps.isEmpty()) {
            answer["value"] = milliamps;
        } else {
            // Copy the original before insertion because adding a key can invalidate QJsonValueRef.
            const auto previousValue = answer.value("value");
            answer["previous_value"] = previousValue;
            answer["value"] = "";
            answer.remove("assessment");
            answer.remove("auto_assessment");
        }
        answers[key] = answer;
    }
    if (!retired.isEmpty()) result["retired_answers"] = retired;
    result["answers"] = MaintenanceRules::apply(answers, true);
    return result;
}

inline QJsonObject report(QJsonObject result) {
    auto schema = result["template"].toObject();
    if (result["status"] != "draft" || schema["version"].toInt() != 6) return result;
    auto sections = schema["sections"].toArray();
    for (int sectionIndex = 0; sectionIndex < sections.size(); ++sectionIndex) {
        auto section = sections[sectionIndex].toObject();
        QJsonArray items;
        for (auto entry : section["items"].toArray()) {
            auto item = entry.toObject();
            const auto key = item["id"].toString();
            if (key == "EST.10") continue;
            if (key == "EST.01") {
                item["label"] = QStringLiteral(
                    "Fotografias do interior e exterior do carregador e sua limpeza antes da tarefa executada "
                    "(visualizar todos os componentes internos e externos)");
            }
            if (key == "EST.13") {
                item["label"] = QStringLiteral("Verifique se o tarifário está visível e em boas condições");
            }
            if (key == "EST.16") {
                item["label"] = QStringLiteral("Verifique se os autocolantes da DGEG estão visíveis e em boas condições");
            }
            if (key == "EST.15") {
                item["label"] = item["label"].toString().replace("(POS)", "(TPM)");
                item["na_notes_default"] = "Sem TPM";
                item["na_notes_required"] = true;
            }
            if (key == "MAN.11" || key == "MAN.12") {
                item["measurement_unit"] = "mA";
                item["measurement_digits"] = 6;
                item["measurement_decimals"] = 0;
                item["help"] = QStringLiteral("1 A = 1000 mA · 0,03 A = 30 mA");
            }
            if (key == "MAN.18") item.remove("help");
            if (key == "MAN.21") item["id"] = "MAN.22";
            items.append(item);
        }
        if (section["id"] == "MAN") {
            // The new after-work photograph precedes the general notes that moved to MAN.22.
            const QJsonObject afterWorkPhoto{
                {"id", "MAN.21"},
                {"label", QStringLiteral(
                    "Fotografias do interior e exterior do carregador e sua limpeza depois da tarefa executada "
                    "(visualizar todos os componentes internos e externos)")},
                {"kind", "status"},
                {"photo_required", true},
                {"na_notes_required", true},
            };
            items.insert(qMax(0, int(items.size()) - 1), afterWorkPhoto);
        }
        section["items"] = items;
        sections[sectionIndex] = section;
    }
    schema["version"] = 7;
    schema["sections"] = sections;
    result["template"] = schema;
    result["payload"] = payload(result["payload"].toObject());
    // Transform the acknowledgement snapshot identically to the server's GET
    // projection, so upgrading after a lost ACK does not create a false conflict.
    if (result["_inflight_payload"].isObject()) {
        result["_inflight_payload"] = payload(result["_inflight_payload"].toObject());
    }
    auto photos = result["photos"].toArray();
    for (int photoIndex = 0; photoIndex < photos.size(); ++photoIndex) {
        auto photo = photos[photoIndex].toObject();
        photo["checkpoint"] = checkpoint(photo["checkpoint"].toString());
        photos[photoIndex] = photo;
    }
    if (result.contains("photos")) result["photos"] = photos;
    return result;
}


inline QString tripTimeCheckpoint(const QString &key) {
    if (key == "MAN.12") return QStringLiteral("MAN.13");
    if (key.size() == 6 && key >= "MAN.13" && key <= "MAN.22") {
        bool numeric = false;
        const int number = key.mid(4).toInt(&numeric);
        if (numeric) return QString("MAN.%1").arg(number + 2);
    }
    return key;
}

inline QJsonObject tripTimeReport(QJsonObject result) {
    auto schema = result["template"].toObject();
    if (result["status"] != "draft" || schema["version"].toInt() != 7) return result;
    // Use the shipped definitions for new questions; retain all frozen connector
    // expansions and historical sections from the editable copy.
    QFile file(":/shared/checklist-v1.json");
    if (!file.open(QIODevice::ReadOnly)) return result;
    QJsonObject definitions;
    for (const auto section : QJsonDocument::fromJson(file.readAll()).object()["sections"].toArray())
        if (section.toObject()["id"] == "MAN")
            for (const auto entry : section.toObject()["items"].toArray())
                definitions[entry.toObject()["id"].toString()] = entry;
    auto sections = schema["sections"].toArray();
    for (qsizetype i = 0; i < sections.size(); ++i) {
        auto section = sections[i].toObject();
        if (section["id"] != "MAN") continue;
        QJsonArray items;
        for (const auto entry : section["items"].toArray()) {
            auto item = entry.toObject();
            const auto old = item["id"].toString();
            const auto key = tripTimeCheckpoint(old);
            item["id"] = key;
            if (key != old) item["measurement_key"] = old;
            if (old == "MAN.12") item["nok_notes_default"] = definitions[key].toObject()["nok_notes_default"];
            items.append(item);
            if (old == "MAN.11" || old == "MAN.12")
                items.append(definitions[old == "MAN.11" ? "MAN.12" : "MAN.14"]);
        }
        section["items"] = items;
        sections[i] = section;
    }
    schema["version"] = 8;
    schema["sections"] = sections;
    result["template"] = schema;
    for (const auto field : {"payload", "_inflight_payload"}) {
        if (!result.value(field).isObject()) continue;
        auto payload = result[field].toObject();
        for (const auto name : {"answers", "signatures"}) {
            if (!payload.value(name).isObject()) continue;
            const auto original = payload[name].toObject();
            QJsonObject mapped;
            for (auto it = original.begin(); it != original.end(); ++it)
                mapped[tripTimeCheckpoint(it.key())] = it.value();
            if (QString::fromLatin1(name) == "answers") {
                auto trip = mapped.value("MAN.13").toObject();
                if (trip["assessment"] == "NOK" && MaintenanceRules::untouched(trip) &&
                    trip["suggested_notes"] == MaintenanceRules::tripNote()) {
                    trip["notes"] = QStringLiteral("Proteção diferencial não dispara");
                    trip["suggested_notes"] = trip.value("notes");
                    mapped["MAN.13"] = trip;
                }
            }
            payload[name] = mapped;
        }
        result[field] = payload;
    }
    if (result.contains("photos")) {
        auto photos = result["photos"].toArray();
        for (qsizetype i = 0; i < photos.size(); ++i) {
            auto photo = photos[i].toObject();
            photo["checkpoint"] = tripTimeCheckpoint(photo["checkpoint"].toString());
            photos[i] = photo;
        }
        result["photos"] = photos;
    }
    return result;
}
} // namespace MaintenanceMigration
