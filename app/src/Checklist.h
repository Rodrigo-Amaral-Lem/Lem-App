// SPDX-License-Identifier: GPL-3.0-only
// Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QRegularExpression>
#include <QMap>

// Correct the six voltage labels for display/export without rewriting frozen reports.
namespace Checklist {
// Legacy closure read missing optional answers through operator[], inserting null.
// Remove only those placeholders; retain real answers and every required field.
inline QJsonObject withoutNullOptionalAnswers(QJsonObject report) {
    for (const auto &payloadKey : {"payload", "_inflight_payload"}) {
        if (!report.value(payloadKey).isObject()) continue;
        auto payload = report.value(payloadKey).toObject();
        auto answers = payload["answers"].toObject();
        bool changed = false;
        for (auto section : report["template"].toObject()["sections"].toArray())
            for (auto entry : section.toObject()["items"].toArray()) {
                const auto item = entry.toObject();
                const auto id = item["id"].toString();
                if (item["optional"].toBool() && answers.value(id).isNull()) {
                    answers.remove(id); changed = true;
                }
            }
        if (changed) { payload["answers"] = answers; report[payloadKey] = payload; }
    }
    return report;
}
inline QString naNotesDefault(const QJsonObject &schema, const QString &key) {
    for (auto section : schema["sections"].toArray())
        for (auto entry : section.toObject()["items"].toArray()) {
            const auto item = entry.toObject();
            if (item["id"] == key) return item["na_notes_default"].toString();
        }
    return {};
}

// Normalize current v6 draft fields/order and optional notes; never reinterpret closed history.
inline QJsonObject correctedDraft(QJsonObject report) {
    auto schema = report["template"].toObject();
    if (report["status"] != "draft" || schema["version"].toInt() != 6) return report;
    // Preserve text entered before FIN.03 became a general-notes field.
    auto payload = report["payload"].toObject();
    auto answers = payload["answers"].toObject();
    auto finalNotes = answers["FIN.03"].toObject();
    const auto previousText = finalNotes["value"].toString();
    if (!previousText.isEmpty()) {
        const auto notes = finalNotes["notes"].toString();
        finalNotes["notes"] = notes.isEmpty() || notes == previousText ? previousText : previousText + "\n" + notes;
        finalNotes["value"] = "";
        answers["FIN.03"] = finalNotes; payload["answers"] = answers; report["payload"] = payload;
    }
    auto sections = schema["sections"].toArray();
    for (int s = 0; s < sections.size(); ++s) {
        auto section = sections[s].toObject(); auto items = section["items"].toArray();
        for (int i = 0; i < items.size(); ++i) {
            auto item = items[i].toObject(); const auto id = item["id"].toString();
            if (id == "MAN.12" || id == "MAN.18") item["reading_required"] = true;
            if (id == "MAN.12") { item["photo_required"] = true; item["photo_always"] = false; }
            if (id == "SEG.01") item["na_notes_required"] = true;
            if (id == "MAN.02") { item["kind"] = "status"; item["na_notes_required"] = false; }
            if (id == "MAN.19") item["kind"] = "status";
            if (QRegularExpression("^SEG\\.0[2-6]$").match(id).hasMatch()) {
                item.remove("notes_disabled"); item["media_disabled"] = true;
                item["notes_button_hidden"] = true;
                if (id == "SEG.02" || id == "SEG.04") item["na_notes_required"] = true;
            }
            if (id == "SITE.01") item["label"] = QStringLiteral("Verifique a limpeza geral do posto de carregamento");
            if (id == "SITE.05") item["label"] = QStringLiteral("Verifique possíveis sinais de vandalismo (cabos cortados, fechadura forçada, carregador danificado, etc.)");
            if (id == "SITE.02") {
                item["na_notes_required"] = true;
                item["na_notes_default"] = QStringLiteral("Sem sinalização horizontal");
            }
            if (id == "SITE.03") {
                item["na_notes_required"] = true;
                item["na_notes_default"] = QStringLiteral("Sem sinalética vertical");
            }
            if (id == "SITE.04") {
                item["na_notes_required"] = true;
                item["na_notes_default"] = QStringLiteral("Sem proteção mecânica");
            }
            if (id == "QVE.02") {
                item["na_notes_required"] = true;
                item["na_notes_default"] = QStringLiteral("Não testado devido a veículo em carga");
                item["label"] = QStringLiteral("Botoneira de emergência - Verifique o correto funcionamento");
            }
            if (id == "QVE.04") {
                item["na_notes_required"] = true;
            }
            if (id == "QVE.09") {
                item["na_notes_required"] = true;
                item["na_notes_default"] = QStringLiteral("Apertos não verificados devido a veículo em carga");
            }
            if (id == "QVE.10") {
                item["na_notes_required"] = true;
                item["na_notes_default"] = QStringLiteral("QVE sem sinalética");
            }
            if (id == "QVE.11") {
                item["na_notes_required"] = true;
                item["na_notes_default"] = QStringLiteral("Não testado devido a veículo em carga");
            }
            if (id == "QVE.12") {
                item["na_notes_required"] = true;
                item["na_notes_default"] = QStringLiteral("Não testado devido a veículo em carga");
            }
            if (id == "EST.01") {
                item["na_notes_required"] = true;
            }
            if (id == "EST.16") {
                item["na_notes_required"] = true;
            }
            if (id == "EST.17") {
                item["na_notes_required"] = true;
            }
            if (id == "MAN.12") {
                item["na_notes_required"] = true;
                item["na_notes_default"] = QStringLiteral("Não testado devido a veículo em carga");
                item["photo_always"] = false;
            }
            if (id == "MAN.13") {
                item["na_notes_required"] = true;
                item["na_notes_default"] = QStringLiteral("Não testado devido a veículo em carga");
            }
            if (id == "MAN.18") {
                item["na_notes_required"] = true;
            }
            if (id == "MAN.20") {
                item["na_notes_required"] = true;
                item["na_notes_default"] = QStringLiteral("Apertos não verificados devido a veículo em carga");
            }
            if (id == "MAN.02") {
                item["na_notes_required"] = false;
            }
            if (id == "FIN.03") { item["kind"] = "notes"; item["optional"] = true; item["label"] = QStringLiteral("Observações gerais finais"); }
            if (id == "FIN.02") {
                item["signature_required"] = true;
                item["media_disabled"] = true;
                item["notes_visible"] = true;
            }
            if (id == "QVE.01") {
                item["label"] = QStringLiteral("Botoneira de emergência - Verifique integridade");
            }
            if (id == "QVE.03") {
                item["na_notes_default"] = QStringLiteral("Botoneira sem dupla sinalização");
                item["label"] = QStringLiteral("Botoneira de emergência - Verifique a dupla sinalização");
            }
            if (id == "MAN.11" || id == "MAN.12") item["label"] = item["label"].toString().replace("corrente", "intensidade");
            if (id.startsWith("CARG.01.")) item["na_notes_default"] = QStringLiteral("Veículo usado não compatível");
            items[i] = item;
        }
        if (section["id"] == "SEG") {
            bool found = false; for (auto i : items) if (i.toObject()["id"] == "SEG.07") found = true;
            if (!found) items.append(QJsonObject{{"id","SEG.07"},{"label",QStringLiteral("Observações gerais de segurança")},
                {"kind","notes"},{"photo_required",false},{"optional",true}});
        }
        if (section["id"] == "CARG") {
            bool found = false; for (auto i : items) if (i.toObject()["id"] == "CARG.07") found = true;
            if (!found) items.append(QJsonObject{{"id","CARG.07"},{"label",QStringLiteral("Observações gerais dos ensaios de carregamento")},
                {"kind","notes"},{"photo_required",false},{"optional",true}});
        }
        if (section["id"] == "EST") {
            bool found = false; for (auto i : items) if (i.toObject()["id"] == "EST.18") found = true;
            if (!found) items.append(QJsonObject{{"id","EST.18"},{"label",QStringLiteral("Observações gerais do equipamento")},
                {"kind","notes"},{"photo_required",false},{"optional",true}});
        }
        if (section["id"] == "SITE") {
            section["title"] = QStringLiteral("SITE - Posto de carregamento");
            bool found = false; for (auto i : items) if (i.toObject()["id"] == "SITE.06") found = true;
            if (!found) items.append(QJsonObject{{"id","SITE.06"},{"label",QStringLiteral("Observações gerais do local")},
                {"kind","notes"},{"photo_required",false},{"optional",true}});
        }
        if (section["id"] == "QVE" || section["id"] == "MAN") {
            const bool qve = section["id"] == "QVE";
            const QString key = qve ? "QVE.14" : "MAN.21";
            bool found = false; for (auto i : items) if (i.toObject()["id"] == key) found = true;
            if (!found) items.append(QJsonObject{{"id",key},{"label",qve ? QStringLiteral("QVE - Observações gerais") : QStringLiteral("Observações gerais da manutenção")},
                {"kind","notes"},{"photo_required",false},{"optional",true}});
        }
        section["items"] = items; sections[s] = section;
    }
    QJsonArray ordered;
    const QStringList order{"SEG","SITE","QVE","EST","MAN","CARG","FIN"};
    for (const auto &id : order) for (auto section : sections) if (section.toObject()["id"] == id) ordered.append(section);
    for (auto section : sections) if (!order.contains(section.toObject()["id"].toString())) ordered.append(section);
    sections = ordered;
    schema["sections"] = sections; report["template"] = schema; return report;
}

// Canonical connector names and order are shared with server/app/checklist.py.
// Invalid counts fail visibly instead of silently dropping physical connectors.
inline QStringList connectorNames(QJsonValue value, QString *error) {
    QStringList result;
    const QMap<QString, QString> aliases = {{"AC", "AC (tipo não indicado)"},
                                            {"T2", "Mennekes Type 2"},
                                            {"TYPE2", "Mennekes Type 2"},
                                            {"TIPO2", "Mennekes Type 2"},
                                            {"MENNEKES", "Mennekes Type 2"},
                                            {"MENNEKESTYPE2", "Mennekes Type 2"},
                                            {"MENNEKESTIPO2", "Mennekes Type 2"},
                                            {"CCS", "CCS"},
                                            {"CCS2", "CCS2"},
                                            {"CCSTYPE2", "CCS2"},
                                            {"CCSCOMBO2", "CCS2"},
                                            {"COMBO2", "CCS2"},
                                            {"CCS1", "CCS1"},
                                            {"CHADEMO", "CHAdeMO"}};
    QJsonArray raw = value.isArray() ? value.toArray() : QJsonArray{value};
    for (const auto entry : raw) {
        if (!entry.isString() || entry.toString().trimmed().toUpper() == "N/A")
            continue;
        auto entryText = entry.toString();
        // Preserve N/A as one missing-value token before slash-separated parsing.
        entryText.replace(QRegularExpression("\\bN\\s*/\\s*A\\b",
                                             QRegularExpression::CaseInsensitiveOption), "NA");
        for (auto text :
             entryText.split(QRegularExpression("[,;|/\\n]+"), Qt::SkipEmptyParts)) {
            text = text.simplified();
            if (text.isEmpty() ||
                QStringList{"NA", "N/A", "NÃO INDICADO", "-"}.contains(text.toUpper()))
                continue;
            int count = 1;
            auto prefix = QRegularExpression("^(\\d+)\\s*[x×]\\s*(.+)$",
                                             QRegularExpression::CaseInsensitiveOption)
                              .match(text);
            auto suffix = QRegularExpression("^(.+?)\\s*[x×]\\s*(\\d+)$",
                                             QRegularExpression::CaseInsensitiveOption)
                              .match(text);
            if (prefix.hasMatch()) {
                count = prefix.captured(1).toInt();
                text = prefix.captured(2).trimmed();
            } else if (suffix.hasMatch()) {
                count = suffix.captured(2).toInt();
                text = suffix.captured(1).trimmed();
            }
            if (QStringList{"NA", "N/A", "NÃO INDICADO", "-"}.contains(text.toUpper()))
                continue;
            if (count < 1 || count > 64 || result.size() + count > 64 || text.size() > 80 ||
                !text.contains(QRegularExpression("[A-Za-z]"))) {
                *error = "Configuração de conectores inválida (máximo 64 conectores).";
                return {};
            }
            auto key = text.toUpper();
            key.remove(QRegularExpression("[^A-Z0-9]"));
            text = aliases.value(key, text);
            for (const auto &existing : result)
                if (existing.compare(text, Qt::CaseInsensitive) == 0) {
                    text = existing;
                    break;
                }
            for (int n = 0; n < count; ++n)
                result.append(text);
        }
    }
    return result;
}
inline QJsonObject forCharger(QJsonObject source, QJsonObject details, QString *error) {
    const auto names = connectorNames(details.value("connectors"), error);
    if (!error->isEmpty())
        return {};
    QString prefix = "PC03";
    for (const auto entry : source["sections"].toArray())
        if (entry.toObject()["id"] == "CARG") prefix = "CARG";
    QJsonObject prototype;
    for (auto section : source["sections"].toArray())
        if (section.toObject()["id"] == prefix)
            for (auto item : section.toObject()["items"].toArray())
                if (item.toObject()["id"].toString().startsWith(prefix + ".01.")) { prototype = item.toObject(); break; }
    QJsonArray generated;
    QMap<QString, int> seen;
    const auto displayed = names.isEmpty() ? QStringList{"tipo não indicado"} : names;
    for (int i = 0; i < displayed.size(); ++i) {
        const auto name = displayed[i];
        const auto suffix =
            names.count(name) > 1 ? " #" + QString::number(++seen[name]) : QString();
        generated.append(QJsonObject{{"id", QString(prefix + ".01.%1").arg(i + 1, 2, 10, QChar('0'))},
                                     {"label", "Teste de carga do conector " + name + suffix},
                                     {"kind", "status"},
                                     {"photo_required", prototype["photo_required"].toBool()},
                                     {"na_notes_required", prototype["na_notes_required"].toBool()},
                                     {"na_notes_default", prototype["na_notes_default"].toString()}});
    }
    auto sections = source["sections"].toArray();
    for (int i = 0; i < sections.size(); ++i) {
        auto section = sections[i].toObject();
        if (section["id"] != prefix)
            continue;
        auto items = generated;
        for (const auto item : section["items"].toArray())
            if (!item.toObject()["id"].toString().startsWith(prefix + ".01."))
                items.append(item);
        section["items"] = items;
        sections[i] = section;
    }
    source["sections"] = sections;
    source["load_test_connectors"] = QJsonArray::fromStringList(names);
    return source;
}

inline QString label(QJsonObject item) {
    const auto id = item["id"].toString();
    auto value = item["label"].toString();
    if (id == "MAN.04" || id == "MAN.05" || id == "MAN.06" || id == "PC02.05" || id == "PC02.06" || id == "PC02.07")
        value.replace("380-420 V", "360-440 V");
    if (id == "MAN.07" || id == "MAN.08" || id == "MAN.09" || id == "PC02.08" || id == "PC02.09" || id == "PC02.10")
        value.replace("200-250 V", "207-253 V");
    return value;
}
inline QJsonObject displayTemplate(QJsonObject source) {
    auto sections = source["sections"].toArray();
    for (int i = 0; i < sections.size(); ++i) {
        auto section = sections[i].toObject();
        auto items = section["items"].toArray();
        for (int j = 0; j < items.size(); ++j) {
            auto item = items[j].toObject();
            item["label"] = label(item);
            items[j] = item;
        }
        section["items"] = items;
        sections[i] = section;
    }
    source["sections"] = sections;
    return source;
}
} // namespace Checklist
