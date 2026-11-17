// SPDX-License-Identifier: GPL-3.0-only
// Editable-v6 migration; keep frozen reports and the original correction source intact.
#pragma once
#include "MaintenanceRules.h"
#include <QJsonArray>
#include <QStringList>

namespace MaintenanceMigration {
inline QString checkpoint(const QString &key) {
    return key == "MAN.21" ? QString("MAN.22") : key == "EST.10" ? QString("EST.18") : key;
}
inline QString joinedNotes(const QStringList &texts) {
    QStringList unique;
    for (const auto &text : texts) if (!text.isEmpty() && !unique.contains(text)) unique.append(text);
    return unique.join('\n');
}
inline QJsonObject payload(QJsonObject result) {
    auto answers = result["answers"].toObject();
    auto retired = result["retired_answers"].toObject();
    if (answers.contains("MAN.21")) {
        const auto old = answers.take("MAN.21");
        if (!answers.contains("MAN.22")) answers["MAN.22"] = old;
        else if (answers["MAN.22"] != old) {
            retired["MAN.21"] = old;
            auto general = answers["MAN.22"].toObject();
            general["notes"] = joinedNotes({general["notes"].toString(), old.toObject()["value"].toString(), old.toObject()["notes"].toString()});
            answers["MAN.22"] = general;
        }
    }
    if (answers.contains("EST.10")) {
        const auto old = answers.take("EST.10"); retired["EST.10"] = old;
        if (!old.toObject()["notes"].toString().trimmed().isEmpty()) {
            auto general = answers["EST.18"].toObject();
            if (general.isEmpty()) general = QJsonObject{{"value", ""}, {"notes", ""}};
            general["notes"] = joinedNotes({general["notes"].toString(), old.toObject()["notes"].toString()});
            answers["EST.18"] = general;
        }
    }
    for (const auto &key : {QString("MAN.11"), QString("MAN.12")}) {
        if (!answers.value(key).isObject()) continue;
        auto answer = answers[key].toObject();
        const auto raw = answer["value"].toString().trimmed();
        if (raw.isEmpty() || Measurements::notApplicable(raw)) continue;
        const auto match = QRegularExpression("^([0-9]{1,9}(?:[.,][0-9]{1,6})?)\\s*(mA|A)$",
            QRegularExpression::CaseInsensitiveOption).match(raw);
        bool converted = false;
        if (match.hasMatch()) {
            // Parse decimal units as scaled integers; never round a fractional mA.
            const auto parts = match.captured(1).replace(',', '.').split('.');
            const auto fraction = parts.size() > 1 ? parts[1] : QString();
            qint64 denominator = 1; for (int n = 0; n < fraction.size(); ++n) denominator *= 10;
            const qint64 scaled = parts[0].toLongLong() * denominator + fraction.toLongLong();
            const qint64 numerator = scaled * (match.captured(2).compare("A", Qt::CaseInsensitive) == 0 ? 1000 : 1);
            if (numerator % denominator == 0 && numerator / denominator <= 999999) {
                answer["value"] = QString::number(numerator / denominator) + " mA";
                converted = true;
            }
        }
        if (!converted) {
            // Copy before inserting: QJsonValueRef indexes can shift when a key is added.
            const auto previousValue = answer.value("value");
            answer["previous_value"] = previousValue;
            answer["value"] = "";
            answer.remove("assessment"); answer.remove("auto_assessment");
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
    for (int s = 0; s < sections.size(); ++s) {
        auto section = sections[s].toObject(); QJsonArray items;
        for (auto entry : section["items"].toArray()) {
            auto item = entry.toObject(); const auto key = item["id"].toString();
            if (key == "EST.10") continue;
            if (key == "EST.01") item["label"] = QStringLiteral("Fotografias do interior e exterior do carregador e sua limpeza antes da tarefa executada (visualizar todos os componentes internos e externos)");
            if (key == "EST.13") item["label"] = QStringLiteral("Verifique se o tarifário está visível e em boas condições");
            if (key == "EST.16") item["label"] = QStringLiteral("Verifique se os autocolantes da DGEG estão visíveis e em boas condições");
            if (key == "EST.15") { item["na_notes_default"] = "Sem POS"; item["na_notes_required"] = true; }
            if (key == "MAN.11" || key == "MAN.12") {
                item["measurement_unit"] = "mA"; item["measurement_digits"] = 6; item["measurement_decimals"] = 0;
                item["help"] = QStringLiteral("1 A = 1000 mA · 0,03 A = 30 mA");
            }
            if (key == "MAN.18") item.remove("help");
            if (key == "MAN.21") item["id"] = "MAN.22";
            items.append(item);
        }
        if (section["id"] == "MAN") {
            const QJsonObject after{{"id", "MAN.21"},
                {"label", QStringLiteral("Fotografias do interior e exterior do carregador e sua limpeza depois da tarefa executada (visualizar todos os componentes internos e externos)")},
                {"kind", "status"}, {"photo_required", true}, {"na_notes_required", true}};
            items.insert(qMax(0, int(items.size()) - 1), after);
        }
        section["items"] = items; sections[s] = section;
    }
    schema["version"] = 7; schema["sections"] = sections; result["template"] = schema;
    result["payload"] = payload(result["payload"].toObject());
    // Transform the acknowledgement snapshot identically to the server's GET
    // projection, so upgrading after a lost ACK does not create a false conflict.
    if (result["_inflight_payload"].isObject()) result["_inflight_payload"] = payload(result["_inflight_payload"].toObject());
    auto photos = result["photos"].toArray();
    for (int n = 0; n < photos.size(); ++n) {
        auto photo = photos[n].toObject(); photo["checkpoint"] = checkpoint(photo["checkpoint"].toString()); photos[n] = photo;
    }
    if (result.contains("photos")) result["photos"] = photos;
    return result;
}
}
