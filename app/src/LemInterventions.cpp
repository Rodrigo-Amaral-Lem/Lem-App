// SPDX-License-Identifier: GPL-3.0-only
#include "Lem.h"
#include <QFile>
#include <QJsonDocument>

namespace {
QString serverMessage(QString error) {
    // Multipart errors can append the transport description after the JSON body.
    const auto body = error.left(error.lastIndexOf('}') + 1).toUtf8();
    const auto detail = QJsonDocument::fromJson(body).object()["detail"].toString();
    return detail.isEmpty() ? QStringLiteral("Verifique os campos e a ligação ao servidor.") : detail;
}
}

QVariantList Lem::contracts() const {
    QVariantList rows;
    for (const auto &entry : m_contracts)
        if (!entry.toObject().contains("intervention")) rows.append(entry.toVariant());
    return rows;
}

QVariantList Lem::interventions() const {
    QVariantList rows;
    for (const auto &entry : m_contracts) {
        const auto scope = entry.toObject();
        if (scope.contains("intervention")) {
            auto job = scope["intervention"].toObject();
            job["order_only"] = scope["order_only"].toBool();
            rows.append(job.toVariantMap());
        }
    }
    return rows;
}

void Lem::leaveInterventionScope() {
    for (const auto &entry : m_contracts) {
        if (entry.toObject()["id"] == m_contract && entry.toObject().contains("intervention")) {
            selectContract({});
            return;
        }
    }
}

QString Lem::prepareIntervention(QString id) {
    if (m_busy || attachingFiles() || !m_qveCopyTarget.isEmpty()) return "!";
    for (const auto &entry : m_contracts) {
        const auto scope = entry.toObject();
        const auto job = scope["intervention"].toObject();
        if (job["id"] != id || scope["order_only"].toBool()) continue;
        selectContract(id);
        if (m_contract != id) return "!";
        if (m_chargers.isEmpty()) {
            QJsonObject membership{{"id", id}, {"contract_id", id}, {"station_id", job["station_id"]},
                {"external_id", job["external_id"]}, {"details", job["details"]},
                {"station_updated", job["station_updated"]}, {"contract_notes", job["notes"]},
                {"notes_revision", job["notes_revision"]}};
            // The job can be cached before the first opening of its membership.
            // Use the physical station's latest details and keep the job notes.
            const auto useStation = [&membership](const QJsonObject &row, const char *idKey, const char *timeKey) {
                if (membership["station_id"].toString().isEmpty() || row[idKey] != membership["station_id"] ||
                    !row["details"].isObject() ||
                    row[timeKey].toString() < membership["station_updated"].toString()) return;
                membership["external_id"] = row["external_id"];
                membership["details"] = row["details"];
                membership["station_updated"] = row[timeKey];
            };
            for (const auto &value : m_stations) useStation(value.toObject(), "id", "updated");
            for (const auto &catalogue : m_catalogues)
                for (const auto &value : catalogue.toArray()) useStation(value.toObject(), "station_id", "station_updated");
            for (const auto &order : m_orders)
                for (const auto &value : order.toObject()["items"].toArray()) useStation(value.toObject(), "station_id", "station_updated");
            m_chargers.append(membership);
            emit changed();
        }
        const auto known = job["report_id"].toString();
        if (job["status"] == "closed" && !known.isEmpty() && !m_deletedReports.contains(known)) return known;
        for (const auto &entry : m_reports) {
            const auto report = entry.toObject();
            if (report["charger_id"] == id && report["owner_id"] == m_user["id"] && report["dirty"].toBool() &&
                !report.contains("_deleted_source_id") && report["correction"].toObject().isEmpty() &&
                !isOrderRecovery(report)) return report["id"].toString();
        }
        // A report may have synced since the last contracts refresh.
        const auto reports = m_chargers.first().toObject()["reports"].toArray();
        if (!known.isEmpty() && !m_deletedReports.contains(known) &&
            !isOrderRecovery(m_reports.value(known).toObject())) return known;
        for (const auto &entry : reports) {
            const auto summary = entry.toObject();
            const auto reportId = summary["id"].toString();
            if (!reportId.isEmpty() && !m_deletedReports.contains(reportId) && !isOrderRecovery(summary) &&
                !isOrderRecovery(m_reports.value(reportId).toObject())) return reportId;
        }
        for (const auto &entry : m_reports) {
            const auto report = entry.toObject();
            if (report["charger_id"] == id && !m_deletedReports.contains(report["id"].toString()) &&
                !report.contains("_deleted_source_id") && report["correction"].toObject().isEmpty() &&
                !isOrderRecovery(report)) return report["id"].toString();
        }
        return {};
    }
    return "!";
}

void Lem::loadCorrectiveContracts(QString stationId) {
    if (!authenticated() || m_user["role"] != "admin") return;
    const auto token = m_token;
    QString path = "/api/corrective-contracts";
    if (!stationId.isEmpty()) path += "?station_id=" + stationId;
    request("GET", path, {}, [this, token, stationId](QByteArray bytes) {
        if (token != m_token) return;
        const auto data = QJsonDocument::fromJson(bytes);
        emit correctiveContractsReceived(stationId, data.array().toVariantList(),
            data.isArray() ? QString{} : QStringLiteral("Lista de contratos inválida."));
    }, "application/json", [this, token, stationId](int, QString error) {
        if (token == m_token) emit correctiveContractsReceived(stationId, {}, serverMessage(error));
    });
}

void Lem::writeIntervention(QString id, QString suffix, QVariantMap fields) {
    const auto token = m_token;
    if (!authenticated()) { emit interventionSaved(id, false, "É necessária uma ligação ao servidor."); return; }
    request("PUT", "/api/interventions/" + id + suffix, QJsonDocument(QJsonObject::fromVariantMap(fields)).toJson(),
        [this, id, token](QByteArray bytes) {
            if (token != m_token) return;
            const auto data = QJsonDocument::fromJson(bytes).object();
            bool saved = data["id"] == id;
            emit interventionSaved(id, saved, saved ? "Intervenção guardada." : "Confirmação inválida. Tente novamente.");
            if (saved) refresh();
        }, "application/json", [this, token, id](int code, QString error) {
            if (token != m_token) return;
            emit interventionSaved(id, false, serverMessage(error));
            if (code == 409) refresh();
        });
}

void Lem::createIntervention(QString id, QVariantMap fields) { writeIntervention(id, {}, fields); }
void Lem::shareIntervention(QString id, QVariantMap fields) { writeIntervention(id, "/share", fields); }

void Lem::importCorrectiveContract(QString id, QVariantMap fields, QUrl workbook) {
    if (!authenticated() || m_user["role"] != "admin") {
        emit interventionSaved(id, false, "A importação exige uma sessão de administrador.");
        return;
    }
    QFile file(localPath(workbook));
    if (!file.open(QIODevice::ReadOnly)) { emit interventionSaved(id, false, "Não foi possível abrir o Excel."); return; }
    const auto limit = 20 * 1024 * 1024;
    const auto bytes = file.read(limit + 1);
    if (bytes.isEmpty() || bytes.size() > limit || file.error() != QFile::NoError) {
        emit interventionSaved(id, false, "Selecione um Excel válido até 20 MB.");
        return;
    }
    QList<QPair<QString, QString>> form;
    for (const auto &key : {"company", "name", "start", "end"}) form.append({key, fields.value(key).toString()});
    const auto token = m_token;
    multipart("PUT", "/api/corrective-contracts/" + id, form, "contrato.xlsx", bytes,
        [this, token, id](QByteArray bytes) {
            if (token != m_token) return;
            const auto result = QJsonDocument::fromJson(bytes).object();
            const bool saved = result["id"] == id && result["count"].toInt() > 0;
            emit interventionSaved(id, saved, saved ? QString("Contrato criado com %1 IDs.").arg(result["count"].toInt())
                                                   : QStringLiteral("Confirmação inválida. Tente novamente."));
        }, [this, token, id](int, QString error) {
            if (token == m_token) emit interventionSaved(id, false, serverMessage(error));
        });
}
