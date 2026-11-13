#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QRegularExpression>
#include <QMap>

// Correct the six voltage labels for display/export without rewriting frozen reports.
namespace Checklist {
// Apply the v6 manual-field bug fixes to editable reports; closed history stays frozen.
inline QJsonObject correctedDraft(QJsonObject report) {
    auto schema = report["template"].toObject();
    if (report["status"] != "draft" || schema["version"].toInt() != 6) return report;
    auto sections = schema["sections"].toArray();
    for (int s = 0; s < sections.size(); ++s) {
        auto section = sections[s].toObject(); auto items = section["items"].toArray();
        for (int i = 0; i < items.size(); ++i) {
            auto item = items[i].toObject(); const auto id = item["id"].toString();
            if (id == "MAN.12" || id == "MAN.18") item["reading_required"] = true;
            if (id == "MAN.12") { item["photo_required"] = true; item["photo_always"] = true; }
            if (id == "MAN.19") item["kind"] = "status";
            if (QRegularExpression("^SEG\\.0[2-6]$").match(id).hasMatch()) {
                item["notes_disabled"] = true; item["media_disabled"] = true;
            }
            items[i] = item;
        }
        if (section["id"] == "SEG") {
            bool found = false; for (auto i : items) if (i.toObject()["id"] == "SEG.07") found = true;
            if (!found) items.append(QJsonObject{{"id","SEG.07"},{"label",QStringLiteral("Observações gerais de segurança")},
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
                                     {"na_notes_required", prototype["na_notes_required"].toBool()}});
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
