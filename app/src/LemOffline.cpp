// SPDX-License-Identifier: GPL-3.0-only
// Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

// Local durability and synchronization for the Lem controller.
// Reading order: journals -> scheduling -> report/photograph uploads -> catalogue
// refresh -> photograph downloads/cleanup -> ownership/conflict recovery.
// Worker tasks receive value snapshots; shared controller state stays on its thread.
// See docs/architecture.md for ordering and failure cases.

#include "Lem.h"
#include <QRegularExpression>
#include "Checklist.h"
#include "SafeStorage.h"
#include "NetworkSecurity.h"
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QDir>
#include <QJsonDocument>
#include <QDateTime>
#include <QCryptographicHash>
#include <QNetworkInformation>
#include <algorithm>
#include <utility>
#include <QtConcurrent/QtConcurrentRun>
#include <QThread>
#include <QUuid>
namespace {
QByteArray compact(QJsonObject o) {
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

constexpr int orderStartRecheckMs = 30000;

bool orderHasNotStarted(const QString &message) {
    // The earlier server returned this planning refusal as a plain string.
    return message == QStringLiteral("A ordem desta intervenção ainda não começou.");
}

bool workGrantDenied(const QString &message) {
    return message == QStringLiteral("Contrato ou tarefa não atribuídos para esta operação.");
}

void deferUntilOrderStarts(QJsonObject &report, qint64 retryAfter) {
    report.remove("_conflict");
    report.remove("_conflict_reason");
    report.remove("_conflict_kind");
    report["_order_start_retry_at"] = double(retryAfter);
}

bool retryableCorrectionIdentity(const QJsonObject &report) {
    if (!report["_conflict"].toBool() || report.contains("_replacement_id") || report["status"] != "closed" ||
        report["correction"].toObject()["state"] != "editing")
        return false;
    // Older servers returned this specific identity conflict as plain text.
    // Only the existing Sync command may send it back for server revalidation.
    return report["_conflict_kind"] == "station_identity_conflict" ||
           report["_conflict_reason"].toString().startsWith("Os dados do carregador foram corrigidos noutro report;");
}
} // namespace

void Lem::recoverOrderSyncConflict(QJsonObject &report) {
    if (!report.value("dirty").toBool() || !report.value("_conflict").toBool() || report.contains("_conflict_kind"))
        return;
    const auto message = report.value("_conflict_reason").toString();
    if (orderHasNotStarted(message))
        deferUntilOrderStarts(report, QDateTime::currentMSecsSinceEpoch());
    else if (workGrantDenied(message) && (independentIntervention(report) || !report["_order_item"].toString().isEmpty()))
        report["_conflict_kind"] = "order_scope";
    else
        return;
    report["_edit_seq"] = report["_edit_seq"].toDouble() + 1;
}

namespace {
bool writeJournal(const QString &path, const QByteArray &bytes) {
#ifdef LEM_TESTING
    const int delay = qEnvironmentVariableIntValue("LEM_TEST_WRITE_DELAY_MS");
    if (delay > 0)
        QThread::msleep(qMin(delay, 2000));
#endif
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
} // namespace

// Hash report IDs into deterministic filenames inside this user/server journal directory.
QString Lem::editPath(QString reportId) const {
    const auto name = QString::fromLatin1(
        QCryptographicHash::hash(reportId.toUtf8(), QCryptographicHash::Sha256).toHex());
    return cacheDir() + "/edits/" + name + ".json";
}

// Coalesce rapid input for 20 ms and retain the newest snapshot for each report ID.
void Lem::queueEdits(QJsonObject report) {
    if (!storageWritable()) return;
    m_pendingEdits[report["id"].toString()] = report;
    emit localSaveStateChanged();
    if (m_activeEditId.isEmpty() && !m_editTimer.isActive())
        m_editTimer.start(20);
}

// Start at most one immutable journal snapshot on the worker pool.
void Lem::writeNextEdits() {
    if (m_localReadError || m_flushingEdits || !m_activeEditId.isEmpty() || m_pendingEdits.isEmpty())
        return;
    m_activeEditId = m_pendingEdits.keys().first();
    m_activeEdit = m_pendingEdits[m_activeEditId].toObject();
    const auto path = editPath(m_activeEditId);
    const auto bytes = compact(m_activeEdit);
    m_editWatcher.setFuture(
        QtConcurrent::run(&m_editPool, [path, bytes] { return writeJournal(path, bytes); }));
}

// A successful old snapshot may clear the queue only if no newer edit replaced it.
// Failures keep pending data in memory and retry; they must not appear saved in the UI.
void Lem::completeEditWrite() {
    if (m_activeEditId.isEmpty() || !m_editWatcher.isFinished())
        return;
    const bool ok = m_editWatcher.result();
    if (ok) {
        if (m_pendingEdits[m_activeEditId].toObject() == m_activeEdit)
            m_pendingEdits.remove(m_activeEditId);
        m_localSaveError = false;
    } else
        m_localSaveError = true;
    m_activeEditId.clear();
    m_activeEdit = {};
    if (!m_flushingEdits) {
        emit localSaveStateChanged();
        if (!ok)
            status(
                "Falha ao guardar no dispositivo. Mantenha a app aberta; nova tentativa automática.");
        if (!m_pendingEdits.isEmpty())
            m_editTimer.start(ok ? 20 : 2000);
    }
}

// An incomplete read is a recovery condition, not an empty account. Do not write
// checkpoints/journals or erase evidence until a repaired reload succeeds.
bool Lem::storageWritable() {
    if (!m_localReadError) return true;
    m_localSaveError = true;
    status("Falha ao ler os dados locais. Os ficheiros foram conservados; gravação, envio e limpeza suspensos até recuperar os dados e reabrir a conta.");
    emit localSaveStateChanged();
    return false;
}

// Durability boundary for identity changes, full checkpoints and application backgrounding.
// This deliberately waits for the active write, then synchronously saves the latest snapshots.
bool Lem::flushEdits() {
    if (!storageWritable()) return false;
    m_editTimer.stop();
    m_flushingEdits = true;
    if (!m_activeEditId.isEmpty()) {
        m_editWatcher.waitForFinished();
        completeEditWrite();
    }
    bool ok = true;
    for (const auto &id : m_pendingEdits.keys()) {
        if (!writeJournal(editPath(id), compact(m_pendingEdits[id].toObject()))) {
            ok = false;
            break;
        }
        m_pendingEdits.remove(id);
    }
    m_flushingEdits = false;
    m_localSaveError = !ok;
    emit localSaveStateChanged();
    if (!ok) {
        m_editTimer.start(2000);
        status(
            "Não foi possível guardar as respostas no dispositivo. Mantenha a app aberta e tente novamente.");
    }
    return ok;
}

// Immediate per-report durability used by attachment/removal and synchronization transitions.
bool Lem::saveEdits(QJsonObject report) {
    if (!flushEdits())
        return false;
    if (!writeJournal(editPath(report["id"].toString()), compact(report))) {
        status(
            "Não foi possível guardar a resposta no dispositivo. Tente novamente antes de sair.");
        return false;
    }
    return true;
}

// Schedule only when a token and dirty work exist; respect any transient-error backoff.
void Lem::scheduleSync(int delay) {
    if (m_token.isEmpty() || m_server.startsWith("demo:") || (pendingCount() == 0 && !m_manualSyncRequested))
        return;
    const auto remaining = m_retryNotBefore - QDateTime::currentMSecsSinceEpoch();
    m_autoSync.start(int(qMax(qint64(delay), remaining)));
}

// Persist the removal of the group-boundary hold before a batch becomes eligible.
bool Lem::releasePhotoSync(QString reportId) {
    // QJsonObject::operator[] on a non-const object inserts a null entry for a
    // missing key. Read with value() wherever the report may not exist.
    if (reportId.isEmpty() || !m_reports.contains(reportId))
        return true;
    auto report = m_reports.value(reportId).toObject();
    if (report["owner_id"] != m_user["id"] || !report["_defer_photo_sync"].toBool())
        return true;
    report.remove("_defer_photo_sync");
    report["_edit_seq"] = report["_edit_seq"].toDouble() + 1;
    if (!saveEdits(report))
        return false;
    m_reports[reportId] = report;
    if (m_current.value("id").toString() == reportId)
        m_current = report;
    return true;
}

// Called by QML when changing groups or leaving a report; no photograph uploads per tap.
void Lem::reportSyncBoundary() {
    if (releasePhotoSync(m_current.value("id").toString()))
        scheduleSync(1500);
}
void Lem::sync() {
    startSync(true);
}

// A contract in retention is unavailable, not an ownership conflict or an acknowledged upload.
// Persist suspension for all its local reports; only a fresh active-contract list lifts it.
void Lem::suspendContractSync(QString contractId, QString reason) {
    for (const auto &id : m_reports.keys()) {
        auto report = withContractPresentation(m_reports.value(id).toObject());
        if (contractId.isEmpty() || report["contract_id"].toString() != contractId) continue;
        report["_contract_unavailable"] = reason;
        m_reports[id] = report;
        m_syncConflicts.insert(id);
        if (m_current["id"].toString() == id) m_current = report;
    }
    persist();
    emit changed();
}

void Lem::reconcileContractSync(bool fromManualSync) {
    QSet<QString> available;
    QSet<QString> individualInterventions;
    for (const auto &entry : m_contracts) {
        const auto contract = entry.toObject();
        const auto id = contract["id"].toString();
        available.insert(id);
        if (contract.contains("intervention") && !contract["order_only"].toBool())
            individualInterventions.insert(id);
    }
    bool resumed = false;
    for (const auto &id : m_reports.keys()) {
        auto report = m_reports.value(id).toObject();
        const auto contractId = report["contract_id"].toString();
        // Direct grants may be restored without any change to the crew's orders.
        if (report.value("_conflict_kind") == "order_scope" && independentIntervention(report) &&
            individualInterventions.contains(contractId))
            resumed |= m_syncConflicts.remove(id);
        if (!report.contains("_contract_unavailable") || !available.contains(contractId)) continue;
        report.remove("_contract_unavailable");
        m_reports[id] = report;
        // A manual Sync may first need to rediscover a restored contract. Give
        // this correction one identity recheck on the next upload pass.
        const bool recheckIdentity = fromManualSync && report["dirty"].toBool() &&
            report["owner_id"] == m_user["id"] && retryableCorrectionIdentity(report);
        if (recheckIdentity) m_manualRestoredCorrections.insert(id);
        if (recheckIdentity || !report["_conflict"].toBool()) m_syncConflicts.remove(id);
        if (m_current["id"].toString() == id) m_current = report;
        resumed = true;
    }
    if (resumed) scheduleSync(1500);
}

// Select eligible dirty reports. Manual send releases photograph holds and can
// recheck a correction's identity conflict. Ownership/revision conflicts stay protected.
void Lem::startSync(bool manual) {
    if (!storageWritable()) return;
    manual = manual || m_manualSyncRequested;
    if (m_busy || !m_qveCopyTarget.isEmpty() || attachingFiles() || m_syncing || m_refreshing || m_catalogueLoading) {
        // Keep the user's receive/refresh request when a catalogue or another operation is active.
        // A plain automatic retry would skip suspended reports without discovering their restoration.
        m_manualSyncRequested = manual;
        scheduleSync(3000);
        return;
    }
    m_manualSyncRequested = false;
    if (m_token.isEmpty()) {
        if (manual)
            online();
        return;
    }
    if (!manual && QNetworkInformation::instance() &&
        QNetworkInformation::instance()->reachability() ==
            QNetworkInformation::Reachability::Disconnected) {
        m_syncState = "Sem rede · guardado no dispositivo";
        scheduleSync(30000);
        emit changed();
        return;
    }
    if (manual) {
        for (const auto &id : m_reports.keys())
            if (!releasePhotoSync(id))
                return;
        m_syncConflicts.clear();
        m_retryNotBefore = 0;
    }
    QStringList ids;
    const auto now = QDateTime::currentMSecsSinceEpoch();
    qint64 nextOrderRetry = 0;
    for (auto i = m_reports.begin(); i != m_reports.end(); ++i) {
        const auto report = i.value().toObject();
        const bool dirty = report["dirty"].toBool();
        const bool owned = report["owner_id"] == m_user["id"];
        const bool recheckIdentity = (manual || m_manualRestoredCorrections.contains(i.key())) &&
                                     retryableCorrectionIdentity(report);
        const bool recheckOrder = report["_conflict_kind"] == "order_scope" &&
            (manual || !m_syncConflicts.contains(i.key()));
        if ((report["_conflict"].toBool() && !recheckIdentity && !recheckOrder) ||
            report.contains("_contract_unavailable") || (dirty && !owned))
            m_syncConflicts.insert(i.key());
        if (owned && dirty && correctionAllowed(report) && !m_syncConflicts.contains(i.key()) &&
            (manual || !report["_defer_photo_sync"].toBool())) {
            const auto retryAt = report["_order_start_retry_at"].toInteger();
            const auto remaining = retryAt - now;
            // A clock adjustment must not strand a retry beyond its normal interval.
            if (!manual && remaining > 0 && remaining <= orderStartRecheckMs) {
                if (!nextOrderRetry || retryAt < nextOrderRetry) nextOrderRetry = retryAt;
            } else {
                ids << i.key();
            }
        }
    }
    if (ids.isEmpty()) {
        m_syncState =
            pendingCount()
                ? (m_syncConflicts.isEmpty()
                       ? "Fotografias guardadas · envio ao mudar de grupo ou sair do report"
                       : "Existem reports que precisam de atenção")
                : "Sem envios pendentes";
        if (nextOrderRetry) {
            if (m_syncConflicts.isEmpty()) m_syncState = "A aguardar o início da ordem · guardado no dispositivo";
            scheduleSync(int(qMin(nextOrderRetry - now, qint64(orderStartRecheckMs))));
        }
        emit changed();
        // Manual synchronization also receives current server state for observers.
        if (manual) refreshContracts(true);
        return;
    }
    m_syncIds = ids;
    m_syncTotal = m_syncIds.size();
    m_syncCompleted = 0;
    m_autoSync.stop();
    m_manualSync = manual;
    m_syncing = true;
    m_syncState = "A enviar em segundo plano · pode continuar a trabalhar";
    emit changed();
    syncNext(0);
}

// Classify failure before choosing recovery: 410 removes a deleted report; 403/409
// protect conflicting work; 422 needs attention; 401 needs login; other failures retry.
void Lem::syncFailed(int code, QString message) {
    // Own the JSON value: operator[] on a temporary object leaves a dangling ref.
    const auto response = QJsonDocument::fromJson(message.toUtf8()).object();
    const auto detail = response.value("detail");
    if (detail.isString())
        message = detail.toString();
    else if (detail.isObject() && detail.toObject()["message"].isString())
        message = detail.toObject()["message"].toString();
    // Keep the current batch and its manual retry eligibility for the following
    // reports. Starting an automatic batch here would skip saved identity conflicts.
    const auto continueAfterRejection = [this](const QString &notice) {
        if (m_syncing) {
            if (m_syncCompleted + 1 < m_syncIds.size())
                status(notice);
            syncNext(m_syncCompleted + 1, notice);
        } else {
            status(notice);
            scheduleSync(1500);
        }
    };
    if (code == 410) {
        const auto id = m_syncReport;
        const auto replacement = detail.toObject()["replacement_id"].toString();
        if (!replacement.isEmpty() ? receiveReplacement(id, replacement) : forgetReport(id)) {
            continueAfterRejection(replacement.isEmpty()
                ? "Este report foi apagado no servidor; cópia local removida."
                : "Report substituído; alterações locais pendentes conservadas.");
        } else {
            m_syncing = false;
            emit changed();
        }
        return;
    }
    // A request the server will keep rejecting (400/404/413…) concerns this report
    // only: set it aside like 422 so the reports queued after it are still sent.
    // 408/429 are transient and 426 already cleared the session.
    const bool rejected = code >= 400 && code < 500 && code != 401 && code != 408 &&
                          code != 426 && code != 429;
    const bool known = m_reports.contains(m_syncReport);
    if (rejected && !known) {
        // Removed while its request was in flight (for example by a catalogue refresh).
        m_syncState = "Envio interrompido · report já não existe neste dispositivo";
        continueAfterRejection(m_syncState);
    } else if (rejected) {
        if (code == 409 && (response["reason"] == "work_order_not_started" || orderHasNotStarted(message))) {
            // Preserve the draft and let unrelated reports finish this batch.
            auto report = m_reports.value(m_syncReport).toObject();
            deferUntilOrderStarts(report, QDateTime::currentMSecsSinceEpoch() + orderStartRecheckMs);
            report["_edit_seq"] = report["_edit_seq"].toDouble() + 1;
            m_reports[m_syncReport] = report;
            if (m_current["id"] == m_syncReport) m_current = report;
            m_syncConflicts.remove(m_syncReport);
            saveEdits(report);
            persist();
            continueAfterRejection(message + " Dados locais conservados.");
            return;
        }
        if ((code == 422 || code == 413) && m_reports.value(m_syncReport).toObject()["status"] == "closed") {
            // A rejected closure is still a draft on the server. Restore editing
            // with current rules, preserving answers/media from older offline apps.
            auto report = m_reports.value(m_syncReport).toObject();
            report["status"] = "draft";
            // A rejected historical closure is editable again. Keeping this
            // transfer-only marker would disable editing and migration forever.
            auto schema = report["template"].toObject();
            schema.remove("frozen_upload");
            report["template"] = schema;
            report = Checklist::correctedDraft(report);
            m_reports[m_syncReport] = report;
            if (m_current["id"] == m_syncReport) {
                m_current = report;
                m_template = report["template"].toObject();
            }
            saveEdits(report); persist();
        }
        if (code == 423) {
            const auto report = withContractPresentation(m_reports.value(m_syncReport).toObject());
            suspendContractSync(report["contract_id"].toString(), message);
        }
        if (code == 409 || code == 403) {
            auto r = m_reports.value(m_syncReport).toObject();
            r.remove("_order_start_retry_at");
            r["_conflict"] = true;
            r["_conflict_reason"] = message;
            if (code == 409 && detail.toObject()["reason"] == "station_identity_conflict")
                r["_conflict_kind"] = "station_identity_conflict";
            else if (code == 403 && (!r["_order_item"].toString().isEmpty() ||
                     (independentIntervention(r) && workGrantDenied(message))))
                r["_conflict_kind"] = "order_scope";
            else
                r.remove("_conflict_kind");
            r["_edit_seq"] = r["_edit_seq"].toDouble() + 1;
            m_reports[m_syncReport] = r;
            if (m_current.value("id").toString() == m_syncReport)
                m_current = r;
            saveEdits(r);
            persist();
        }
        m_syncConflicts.insert(m_syncReport);
        m_syncState = "Envio precisa de atenção · dados locais conservados";
        continueAfterRejection(m_syncState + ": " + message);
    } else if (code == 401) {
        m_syncing = false;
        m_autoSync.stop();
        m_syncState = "Entre novamente para enviar · dados locais conservados";
        status(m_syncState);
    } else {
        m_syncing = false;
        m_syncState = "Envio pendente · nova tentativa automática";
        status(m_syncState + ". " + message);
        m_retryNotBefore = QDateTime::currentMSecsSinceEpoch() + m_retrySeconds * 1000;
        scheduleSync(m_retrySeconds * 1000);
        m_retrySeconds = std::min(600, m_retrySeconds * 2);
    }
}
void Lem::syncRequest(QString method, QString path, QByteArray body, Callback cb) {
    request(method, path, body, cb, "application/json",
            [this](int code, QString message) { syncFailed(code, message); });
}

QJsonArray Lem::reconcilePhotos(QJsonArray photos, const QJsonArray &serverPhotos, const QJsonArray &removed) const {
    // Only the server can enumerate another device's uploads. Preserve local
    // additions and tombstones made while the request was in flight.
    const auto manifest = receivedPhotoPaths({{"photos", serverPhotos}})["photos"].toArray();
    QSet<QString> remoteIds;
    for (const auto &entry : manifest) {
        const auto remote = entry.toObject();
        const auto pid = remote["id"].toString();
        remoteIds.insert(pid);
        if (removed.contains(pid)) continue;
        auto existing = std::find_if(photos.begin(), photos.end(), [&pid](const auto &value) {
            return value.toObject()["id"] == pid;
        });
        if (existing == photos.end()) photos.append(remote);
        else {
            auto photo = (*existing).toObject();
            if (photo["uploaded"].toBool()) {
                auto merged = remote;
                if (photo["digest"] == remote["digest"] && !photo["path"].toString().isEmpty())
                    merged["path"] = photo["path"];
                *existing = merged;
            }
        }
    }
    for (qsizetype i = photos.size(); i-- > 0;) {
        const auto photo = photos[i].toObject();
        if (photo["uploaded"].toBool() && !remoteIds.contains(photo["id"].toString()))
            photos.removeAt(i);
    }
    return photos;
}

// Merge acknowledgements into the LATEST local report, never replace it with sent data.
// Only matching edit sequences and fully acknowledged photos/deletions clear dirty.
bool Lem::mergeSync(QJsonObject sent, bool complete, QJsonValue serverPhotos) {
    const auto id = sent["id"].toString();
    if (!m_reports.contains(id)) {
        syncFailed(409, "Report local indisponível.");
        return false;
    }
    const auto previous = m_reports[id].toObject();
    auto latest = previous;
    for (const auto &key : {"revision", "snapshot", "template", "started"})
        if (sent.contains(key))
            latest[key] = sent[key];
    auto photos = latest["photos"].toArray();
    const auto acknowledged = sent["photos"].toArray();
    for (int i = 0; i < photos.size(); ++i) {
        auto p = photos[i].toObject();
        for (auto value : acknowledged) {
            auto ack = value.toObject();
            if (p["id"] == ack["id"] && ack["uploaded"].toBool()) {
                p["uploaded"] = true;
                if (ack.contains("digest"))
                    p["digest"] = ack["digest"];
                break;
            }
        }
        photos[i] = p;
    }
    if (serverPhotos.isArray())
        photos = reconcilePhotos(photos, serverPhotos.toArray(), latest["deleted_photos"].toArray());
    latest["photos"] = photos;
    auto deleted = latest["deleted_photos"].toArray();
    const auto ackDeleted = sent["_ack_deleted"].toArray();
    for (int i = deleted.size() - 1; i >= 0; --i)
        if (ackDeleted.contains(deleted[i]))
            deleted.removeAt(i);
    latest["deleted_photos"] = deleted;
    if (ackDeleted.contains(latest.value("_inflight_delete_id"))) {
        latest.remove("_inflight_delete_id");
        latest.remove("_inflight_delete_revision");
        latest.remove("_inflight_delete_payload");
    }
    if (complete) {
        latest.remove("_order_start_retry_at");
        bool uploaded = true;
        for (auto p : photos)
            if (!p.toObject()["uploaded"].toBool())
                uploaded = false;
        latest["dirty"] = latest["_edit_seq"].toDouble() != sent["_edit_seq"].toDouble() ||
                          !uploaded || !deleted.isEmpty();
        latest["last_sync"] = double(QDateTime::currentSecsSinceEpoch());
        if (!latest["dirty"].toBool()) {
            latest.remove("_inflight_payload");
            latest.remove("_inflight_revision");
        }
    }
    const auto correction = sent["correction"].toObject();
    const bool committedCorrection = complete && correction["state"] == "committed";
    const auto conflictKeys = {"_conflict", "_conflict_reason", "_conflict_kind"};
    // Keep the saved conflict until a clean replacement can reach the checkpoint.
    const bool resolvedIdentity = complete && !latest["dirty"].toBool() &&
        ((committedCorrection && retryableCorrectionIdentity(previous)) || previous["_conflict_kind"] == "order_scope");
    if (resolvedIdentity)
        for (const auto key : conflictKeys) latest.remove(key);
    if (committedCorrection) {
        for (const auto &key : {"owner_id", "technician", "correction", "finished", "updated"})
            if (sent.contains(key)) latest[key] = sent[key];
    }
    // A correction must never change the catalogue while it is only staged.
    // Apply the server's live identity after commit (including lost-ACK recovery),
    // not the historical answers, which may precede another intervention.
    if (sent["status"] == "closed" && (correction.isEmpty() ||
        (committedCorrection && sent.contains("station_update"))))
        applyClosedIdentity(sent);
    m_reports[id] = latest;
    if (m_current.value("id").toString() == id)
        m_current = latest;
    if (!persist()) {
        latest["dirty"] = true;
        if (correction["state"] == "committed") {
            // Keep the editor eligible to retry the lost durable acknowledgement.
            // Restoring the technician owner in memory now would remove this work
            // from the administrator's sync queue before it reached disk.
            latest["owner_id"] = previous.value("owner_id");
            latest["correction"] = previous.value("correction");
        }
        if (resolvedIdentity)
            for (const auto key : conflictKeys) latest[key] = previous.value(key);
        m_reports[id] = latest;
        if (m_current.value("id").toString() == id)
            m_current = latest;
        m_syncing = false;
        m_syncState = "Falha ao guardar o progresso; envio continua pendente";
        scheduleSync(30000);
        emit changed();
        return false;
    }
    if (complete && latest["status"] == "closed" && !m_orders.isEmpty()) refreshOrders();
    emit changed();
    return true;
}

// For one queued report, compare owner/revision with the server before sending.
// The durable in-flight payload/revision pair identifies a previously accepted PUT
// whose acknowledgement was lost, without accepting unrelated remote edits.
void Lem::syncNext(int index, QString notice) {
    m_syncCompleted = index;
    if (index >= m_syncIds.size()) {
        m_syncing = false;
        m_retrySeconds = 30;
        m_retryNotBefore = 0;
        m_syncReport.clear();
        m_syncIds.clear();
        m_syncState = pendingCount() ? "Alterações locais pendentes de envio"
                                     : "Todos os envios confirmados pelo servidor";
        if (!m_syncConflicts.isEmpty())
            m_syncState = QString::number(m_syncConflicts.size()) +
                          " report(s) precisam de atenção; restantes envios tratados";
        cleanPhotoCache();
        status(notice.isEmpty() ? m_syncState : notice);
        scheduleSync(1500);
        if (m_manualSync) refreshContracts(true);
        return;
    }
    const auto id = m_syncIds[index];
    auto r = m_reports.value(id).toObject();
    m_syncReport = id;
    m_manualRestoredCorrections.remove(id);
    if (r.isEmpty() || m_syncConflicts.contains(id) || r.contains("_contract_unavailable") ||
        r["owner_id"] != m_user["id"] || !r["dirty"].toBool() ||
        (!m_manualSync && r["_defer_photo_sync"].toBool()) || !correctionAllowed(r)) {
        syncNext(index + 1, notice);
        return;
    }
    // Background sync can run before a cached draft is ever opened. Apply the
    // same migration here, including its in-flight snapshot, before GET/PUT.
    const auto repaired = Checklist::withoutNullOptionalAnswers(Checklist::correctedDraft(r));
    if (repaired != r) {
        r = repaired;
        r["_edit_seq"] = r["_edit_seq"].toDouble() + 1;
        m_reports[id] = r;
        if (m_current.value("id") == id) {
            m_current = r;
            m_template = r["template"].toObject();
        }
        if (!persist()) { m_syncing = false; emit changed(); return; }
        emit changed();
    }
    m_syncState =
        "Report " + QString::number(index + 1) + " / " + QString::number(m_syncIds.size()) +
        " · verificar dados no servidor · " +
        r["snapshot"].toObject()["charger"].toObject()["external_id"].toString(id.left(8));
    emit operationChanged();
    r["_ack_deleted"] = QJsonArray{};
    auto send = [this, index](QJsonObject local) {
        const auto id = local["id"].toString();
        if (m_deletedReports.contains(id)) {
            syncNext(index + 1);
            return;
        }
        auto latest = m_reports[id].toObject();
        // Durable record permits recovery of a PUT whose response was lost, even if
        // the technician has typed newer answers while that PUT was in flight.
        latest["_inflight_payload"] = local["payload"];
        latest["_inflight_revision"] = local["revision"].toInt() + 1;
        m_reports[id] = latest;
        if (m_current.value("id").toString() == id)
            m_current = latest;
        if (!persist()) {
            m_syncing = false;
            emit changed();
            return;
        }
        // An unsent draft still has its original device snapshot. Local closure
        // records those three source values before projecting the final answers.
        QJsonValue identityAtStart = local.value("_station_identity_at_start");
        if (local["revision"].toInt() == 0 && local["status"] == "draft")
            identityAtStart = stationIdentity(local["snapshot"].toObject()["charger"].toObject());
        syncRequest("PUT", "/api/reports/" + id,
                    compact({{"charger_id", local["charger_id"]},
                             {"station_identity_at_start", identityAtStart},
                             {"work_item_id", local.value("_order_item")},
                             {"base_revision", local["revision"]},
                             {"checklist_version", local["template"].toObject()["version"]},
                             {"frozen_history", local["status"] == "closed" && local["template"].toObject()["version"].toInt() == 6 && local["correction"].toObject()["state"] != "editing"},
                             {"load_test_connectors", local["template"].toObject()["load_test_connectors"]},
                             {"status", "draft"},
                             {"payload", local["payload"]}}),
                    [this, index, local](QByteArray data) mutable {
                        const auto saved = QJsonDocument::fromJson(data).object();
                        if (!validRemoteReport(saved, local["id"].toString()) || saved["status"] != "draft" ||
                            saved["charger_id"] != local["charger_id"] || saved["owner_id"] != local["owner_id"] ||
                            saved["revision"].toInt() != local["revision"].toInt() + 1) {
                            // Leave the in-flight journal at its previous base. GET on
                            // retry must prove which payload the server accepted.
                            syncFailed(0, "A revisão recebida não confirma este envio. A verificar novamente.");
                            return;
                        }
                        for (const auto &key : {"revision", "snapshot", "template", "started"})
                            if (saved.contains(key))
                                local[key] = saved[key];
                        // Record the server's representation, which can normalize
                        // answers during PUT. Keep the editable payload separate.
                        local["_inflight_delete_payload"] = saved.value("payload");
                        if (mergeSync(local, false, saved["photos"]))
                            syncPhotos(index, local, 0);
                    });
    };
    const QString path = "/api/reports/" + id + (r["revision"].toInt() == 0 ? "/exists" : "");
    syncRequest("GET", path, {}, [this, index, r, send](QByteArray data) mutable {
        const auto remote = QJsonDocument::fromJson(data).object();
        if (m_deletedReports.contains(r["id"].toString())) {
            syncNext(index + 1);
            return;
        }
        if (r["revision"].toInt() == 0 && remote.value("exists") == QJsonValue(false)) {
            send(r);
            return;
        }
        if (!validRemoteReport(remote, r["id"].toString())) {
            syncFailed(0, "Resposta de report inválida. Os dados locais foram conservados.");
            return;
        }
        if (remote["work_item_id"].isString() && r["_order_item"].isString() &&
            remote["work_item_id"] != r["_order_item"]) {
            syncFailed(409, "O report pertence a outra visita. A cópia local foi conservada.");
            return;
        }
        if (remote["charger_id"] != r["charger_id"]) {
            syncFailed(409, "O carregador do report no servidor é diferente.");
            return;
        }
        const bool committedCorrection = remote["correction"].toObject()["state"] == "committed" &&
            remote["correction"].toObject()["editor_id"] == m_user["id"] &&
            remote["correction"].toObject()["source_id"] == r["correction"].toObject()["source_id"];
        if (!committedCorrection && (remote["owner_id"] != m_user["id"] || remote["owner_id"] != r["owner_id"])) {
            syncFailed(409, "A propriedade do rascunho mudou no servidor. Proprietário atual: " +
                                remote["technician"].toString() +
                                ". A cópia local foi conservada.");
            return;
        }
        const bool samePayload = remote["payload"] == r["payload"];
        if (remote["status"].toString() == "closed") {
            if (!samePayload || r["status"].toString() != "closed" ||
                !r["deleted_photos"].toArray().isEmpty()) {
                syncFailed(409, "O report já foi fechado no servidor.");
                return;
            }
            auto photos = r["photos"].toArray();
            for (int i = 0; i < photos.size(); ++i) {
                auto p = photos[i].toObject();
                bool found = false;
                for (auto value : remote["photos"].toArray()) {
                    auto ack = value.toObject();
                    if (ack["id"] == p["id"]) {
                        found = true;
                        p["uploaded"] = true;
                        p["digest"] = ack["digest"];
                        break;
                    }
                }
                if (!found) {
                    syncFailed(
                        409,
                        "O report foi fechado no servidor e existem fotografias locais por enviar.");
                    return;
                }
                photos[i] = p;
            }
            r["photos"] = photos;
            r["revision"] = remote["revision"];
            if (remote.contains("station_update")) r["station_update"] = remote["station_update"];
            if (committedCorrection)
                for (const auto &key : {"owner_id", "technician", "correction", "started", "finished", "updated", "snapshot"})
                    r[key] = remote[key];
            if (mergeSync(r, true, remote["photos"]))
            {
                if (committedCorrection) receiveReplacement(remote["correction"].toObject()["source_id"].toString(), r["id"].toString());
                syncNext(index + 1);
            }
            return;
        }
        bool confirmedDelete = false;
        if (remote["revision"] != r["revision"]) {
            const bool lostAck = remote["payload"] == r["_inflight_payload"] &&
                                 remote["revision"] == r["_inflight_revision"];
            auto queuedDeletions = r["deleted_photos"].toArray();
            const auto remotePhotos = remote["photos"].toArray();
            // An older server can return the new revision even when DELETE did
            // nothing. Verify the actual report before adopting that revision.
            // Older checkpoints may only have a matching local or prior PUT payload.
            const bool matchesDeleteBase = r.contains("_inflight_delete_payload")
                ? remote["payload"] == r["_inflight_delete_payload"]
                : samePayload || (r["_inflight_revision"] == r["revision"] &&
                                  remote["payload"] == r["_inflight_payload"]);
            if (!lostAck && !queuedDeletions.isEmpty() &&
                r.value("_inflight_delete_id") == queuedDeletions.first() &&
                r.value("_inflight_delete_revision") == r["revision"] &&
                remote["revision"] == QJsonValue(r["revision"].toDouble() + 1) &&
                remote["status"] == "draft" && remote["payload"].isObject() &&
                remote["photos"].isArray() && matchesDeleteBase) {
                const auto pending = queuedDeletions.first();
                confirmedDelete = std::none_of(remotePhotos.begin(), remotePhotos.end(),
                    [&pending](const auto &photo) { return photo.toObject()["id"] == pending; });
                if (confirmedDelete) {
                    queuedDeletions.removeAt(0);
                    r["deleted_photos"] = queuedDeletions;
                    r["_ack_deleted"] = QJsonArray{pending};
                }
            }
            if (!lostAck && !confirmedDelete) {
                syncFailed(409, "Report alterado noutro dispositivo. Rascunho local conservado.");
                return;
            }
        }
        r["revision"] = remote["revision"];
        // Equal answers alone cannot acknowledge a form migration. In particular,
        // reopening a rejected historical closure must send the current version
        // and clear the server's frozen-upload marker before evidence is sent.
        const auto remoteTemplate = remote["template"].toObject();
        const auto localTemplate = r["template"].toObject();
        const bool needsVisitBinding = r["_order_item"].isString() && !remote["work_item_id"].isString();
        const bool sameForm = !needsVisitBinding && r["_conflict_kind"] != "order_scope" &&
            remoteTemplate["version"] == localTemplate["version"] &&
            remoteTemplate["frozen_upload"].toBool() == localTemplate["frozen_upload"].toBool();
        r["_inflight_delete_payload"] = remote.value("payload");
        // Checkpoint a recovered deletion before sending any newer local answers.
        if ((confirmedDelete || (samePayload && sameForm)) && !mergeSync(r, false, remote["photos"]))
            return;
        if (samePayload && sameForm)
            syncPhotos(index, r, 0);
        else
            send(r);
    });
}

// Process deletions, then missing uploads, then final closure. Each stage continues
// only after acknowledgement; mergeSync preserves edits made during network waits.
void Lem::syncPhotos(int index, QJsonObject r, int photoIndex) {
    auto deleted = r["deleted_photos"].toArray();
    const auto id = r["id"].toString();
    if (!deleted.isEmpty()) {
        const auto pid = deleted.first().toString();
        // Persist this exact DELETE intent before sending it. Recovery must not
        // infer that another device's deletion was our lost acknowledgement.
        auto latest = m_reports.value(id).toObject();
        latest["_inflight_delete_id"] = pid;
        latest["_inflight_delete_revision"] = r["revision"];
        latest["_inflight_delete_payload"] = r.value("_inflight_delete_payload");
        m_reports[id] = latest;
        if (m_current.value("id").toString() == id)
            m_current = latest;
        if (!persist()) {
            m_syncing = false;
            emit changed();
            return;
        }
        syncRequest("DELETE",
                    "/api/reports/" + id + "/photos/" + pid +
                        "?base_revision=" + QString::number(r["revision"].toInt()),
                    {}, [this, index, r, deleted, photoIndex, pid](QByteArray data) mutable {
                        const auto saved = QJsonDocument::fromJson(data).object();
                        const auto revision = saved.value("revision");
                        if (saved.value("id") != pid || saved.value("deleted") != QJsonValue(true) ||
                            !revision.isDouble()) {
                            syncFailed(0, "Resposta inválida ao remover fotografia.");
                            return;
                        }
                        if (revision == QJsonValue(r["revision"].toDouble() + 1)) {
                            // Reuse the durable DELETE recovery path for a one-step
                            // advance. The response alone cannot confirm the answers.
                            syncNext(index);
                            return;
                        }
                        if (revision != r.value("revision")) {
                            syncFailed(409, "Report alterado noutro dispositivo. Rascunho local conservado.");
                            return;
                        }
                        deleted.removeAt(0);
                        r["deleted_photos"] = deleted;
                        auto ack = r["_ack_deleted"].toArray();
                        ack.append(pid);
                        r["_ack_deleted"] = ack;
                        if (mergeSync(r))
                            syncPhotos(index, r, photoIndex);
                    });
        return;
    }
    auto photos = r["photos"].toArray();
    // The user can remove queued evidence while another upload awaits its reply.
    // Consult the latest metadata before opening a file from the batch snapshot.
    QSet<QString> retained;
    for (const auto &value : m_reports.value(id).toObject()["photos"].toArray())
        retained.insert(value.toObject()["id"].toString());
    while (photoIndex < photos.size() &&
           (photos[photoIndex].toObject()["uploaded"].toBool() ||
            !retained.contains(photos[photoIndex].toObject()["id"].toString())))
        ++photoIndex;
    if (photoIndex < photos.size()) {
        auto p = photos[photoIndex].toObject();
        m_syncState = "Report " + QString::number(index + 1) + " / " + QString::number(m_syncIds.size()) +
                      " · enviar fotografia " + QString::number(photoIndex + 1) + " / " +
                      QString::number(photos.size()) + " · " + p["checkpoint"].toString();
        emit operationChanged();
        auto file = new QFile(localPath(QUrl(p["path"].toString())));
        if (!file->open(QIODevice::ReadOnly)) {
            delete file;
            syncFailed(422, "Fotografia local em falta; report conservado.");
            return;
        }
        // Windows cannot unlink the streamed QFile while it is open. multipart
        // closes it before either callback, so retry a durably requested removal.
        const auto cleanup = [this, id, p] {
            const auto latest = m_reports.value(id).toObject();
            if (latest.isEmpty() || latest["deleted_photos"].toArray().contains(p["id"]))
                removeCachedPhoto(p);
        };
        multipart(
            "PUT", "/api/reports/" + id + "/photos/" + p["id"].toString(),
            {{"checkpoint", p["checkpoint"].toString()},
             {"base_revision", QString::number(r["revision"].toInt())}},
            p["filename"].toString(), {},
            [this, index, r, photos, p, photoIndex, cleanup](QByteArray data) mutable {
                cleanup();
                const auto ack = QJsonDocument::fromJson(data).object();
                if (ack["id"] != p["id"] || ack["digest"].toString().isEmpty()) {
                    syncFailed(0, "Confirmação de fotografia inválida; envio pendente conservado.");
                    return;
                }
                p["uploaded"] = true;
                p["digest"] = ack["digest"];
                photos[photoIndex] = p;
                r["photos"] = photos;
                if (mergeSync(r))
                    syncPhotos(index, r, photoIndex + 1);
            },
            [this, cleanup](int code, QString message) { cleanup(); syncFailed(code, message); }, file);
        return;
    }
    if (r["status"].toString() == "closed") {
        const bool correction = r["correction"].toObject()["state"] == "editing";
        syncRequest(correction ? "POST" : "PUT", "/api/reports/" + id + (correction ? "/correction/commit" : ""),
                    compact({{"charger_id", r["charger_id"]},
                             {"work_item_id", r.value("_order_item")},
                             {"base_revision", r["revision"]},
                             {"checklist_version", r["template"].toObject()["version"]},
                             {"frozen_history", !correction && r["template"].toObject()["version"].toInt() == 6},
                             {"load_test_connectors", r["template"].toObject()["load_test_connectors"]},
                             {"status", "closed"},
                             {"payload", r["payload"]}}),
                    [this, index, r](QByteArray data) mutable {
                        const auto saved = QJsonDocument::fromJson(data).object();
                        if (!validRemoteReport(saved, r["id"].toString()) || saved["status"] != "closed" ||
                            saved["charger_id"] != r["charger_id"] || saved["revision"].toInt() != r["revision"].toInt() + 1) {
                            syncFailed(0, "Resposta inválida ao concluir.");
                            return;
                        }
                        for (const auto &key : {"revision", "snapshot", "template", "started", "finished", "updated"})
                            if (saved.contains(key)) r[key] = saved[key];
                        if (saved.contains("station_update")) r["station_update"] = saved["station_update"];
                        if (saved["correction"].toObject()["state"] == "committed")
                            for (const auto &key : {"owner_id", "technician", "correction"}) r[key] = saved[key];
                        if (mergeSync(r, true, saved["photos"]))
                        {
                            if (saved["correction"].toObject()["state"] == "committed")
                                receiveReplacement(saved["correction"].toObject()["source_id"].toString(), r["id"].toString());
                            syncNext(index + 1);
                        }
                    });
    } else if (mergeSync(r, true))
        syncNext(index + 1);
}

// Use the local catalogue for 15 minutes unless explicitly forced.
// Station revisions are UTC ISO timestamps from the server. Reconcile racing
// responses before updating the membership projections and the offline station list.
QJsonArray Lem::currentPcveRows(QJsonArray rows) {
    // Clean live catalogue rows from older servers and offline caches only.
    // Closed report snapshots and pending report payloads are separate data.
    static const QSet<QString> obsolete = {"location", "localização", "localizacao", "local",
        "nome do local", "site name", "localização / nome do local", "localizacao / nome do local",
        "site id", "id do site", "notas", "contract notes"};
    static const QRegularExpression lowerCedilla("(?<=[a-zà-öø-ÿ])Ý(?=[aouãõáàâóôú])");
    static const QRegularExpression upperCedilla("Ý(?=[aouãõáàâóôúAOUÃÕÁÀÂÓÔÚ])");
    static const QRegularExpression smallCedilla("ý(?=[aouãõáàâóôúAOUÃÕÁÀÂÓÔÚ])");
    for (qsizetype i = 0; i < rows.size(); ++i) {
        auto row = rows.at(i).toObject();
        auto details = row["details"].toObject();
        const auto previous = details;
        details.remove("source");
        for (const auto &key : details.keys()) {
            QString name = key.startsWith("extra.") ? key.mid(6) : key;
            if (obsolete.contains(name.replace('_', ' ').simplified().toLower())) details.remove(key);
        }
        for (const auto *field : {"address", "region", "manufacturer", "energy_supply"}) {
            const auto value = details.value(field).toString();
            if (value.contains(u'Ý') || value.contains(u'ý')) {
                QString fixed = value;
                details[field] = fixed.replace(lowerCedilla, "ç").replace(upperCedilla, "Ç").replace(smallCedilla, "ç");
            }
        }
        if (details != previous) {
            row["details"] = details;
            rows[i] = row;
        }
    }
    return rows;
}

void Lem::applyStationRecords(const QJsonArray &incoming, bool complete, QJsonArray *memberships) {
    const auto records = currentPcveRows(incoming);
    if (memberships) *memberships = currentPcveRows(*memberships);
    const auto canonical = [](const QJsonObject &row) -> QJsonObject {
        return {{"id", row["station_id"]}, {"external_id", row["external_id"]},
                {"details", row["details"]}, {"updated", row["station_updated"]}};
    };
    QHash<QString, QJsonObject> latest;
    for (const auto &value : records) {
        const auto row = value.toObject();
        const auto id = row["id"].toString();
        if (!id.isEmpty() && !row["updated"].toString().isEmpty() && row["details"].isObject())
            latest.insert(id, row);
    }
    if (!latest.isEmpty()) {
        const auto keepNewer = [&latest](const QJsonObject &row) {
            auto current = latest.find(row["id"].toString());
            if (current != latest.end() && row["details"].isObject() &&
                row["updated"].toString() >= current->value("updated").toString())
                current.value() = row;
        };
        for (const auto &value : m_stations) keepNewer(value.toObject());
        for (const auto &value : m_chargers) keepNewer(canonical(value.toObject()));
        for (const auto &catalogue : std::as_const(m_catalogues))
            for (const auto &value : catalogue.toArray()) keepNewer(canonical(value.toObject()));
        // A local closure updates the order and membership details before the
        // server issues a new station timestamp. Keep those corrections when
        // an in-flight listing still has the same timestamp.
        for (const auto &order : m_orders)
            for (const auto &entry : order.toObject()["items"].toArray())
                keepNewer(canonical(entry.toObject()));

        const auto patchRow = [&latest](QJsonObject &row) {
            const auto station = latest.constFind(row.value("station_id").toString());
            if (station == latest.cend() ||
                row.value("station_updated").toString() > station->value("updated").toString()) return;
            row["external_id"] = station->value("external_id");
            row["details"] = station->value("details");
            row["station_updated"] = station->value("updated");
        };
        const auto patchMemberships = [&patchRow](QJsonArray &rows) {
            for (qsizetype i = 0; i < rows.size(); ++i) {
                auto row = rows.at(i).toObject();
                patchRow(row);
                rows[i] = row;
            }
        };
        patchMemberships(m_chargers);
        for (const auto &id : m_catalogues.keys()) {
            auto rows = m_catalogues[id].toArray();
            patchMemberships(rows);
            m_catalogues[id] = rows;
        }
        if (memberships) patchMemberships(*memberships);
        updateWorkOrderStations(patchRow);
    }

    // Only a complete GET can establish visibility of new IDs or remove old ones.
    auto stations = complete ? records : m_stations;
    for (qsizetype i = 0; i < stations.size(); ++i) {
        const auto station = latest.constFind(stations.at(i).toObject()["id"].toString());
        if (station != latest.cend()) stations[i] = station.value();
    }
    m_stations = stations;
}

// Validate the whole listing before acting on even the first removal marker.
// Optional fields were absent on older servers. An empty array is authoritative.
static bool validMembershipCatalogue(const QJsonDocument &document, const QString &contractId) {
    if (!document.isArray()) return false;
    QSet<QString> ids;
    for (const auto &entry : document.array()) {
        const auto row = entry.toObject();
        const auto id = row["id"].toString();
        if (id.isEmpty() || ids.contains(id) || row["external_id"].toString().isEmpty()
            || !row["details"].isObject() || !row["reports"].isArray()
            || (row.contains("contract_id") && row["contract_id"] != contractId)
            || (row.contains("deleted_reports") && !row["deleted_reports"].isArray())
            || (row.contains("report_replacements") && !row["report_replacements"].isObject()))
            return false;
        ids.insert(id);
        if ((row.contains("notes_revision") || row.contains("contract_notes"))
            && (row["notes_revision"].toInt(-1) < 0 || !row["contract_notes"].isString())) return false;
        for (const auto &summary : row["reports"].toArray())
            if (summary.toObject()["id"].toString().isEmpty()) return false;
        for (const auto &removed : row["deleted_reports"].toArray())
            if (removed.toString().isEmpty()) return false;
        const auto replacements = row["report_replacements"].toObject();
        for (auto it = replacements.begin(); it != replacements.end(); ++it)
            if (it.key().isEmpty() || it.value().toString().isEmpty()) return false;
    }
    return true;
}

// Server deletion markers remove stale local copies before installing fresh summaries.
void Lem::refreshCatalogue(QString id, bool force) {
    if (id.isEmpty() || m_token.isEmpty() || m_catalogueLoading)
        return;
    const auto last = m_catalogueTimes[id].toDouble();
    if (!force && m_catalogues.contains(id) && QDateTime::currentSecsSinceEpoch() - last < 15 * 60)
        return;
    // Some normal callers open an authorised membership before their first
    // contracts listing arrives. A known or refreshing scope can revoke it.
    const bool hadContractScope = m_refreshing || !m_contracts.isEmpty();
    m_catalogueLoading = true;
    m_refreshing = true;
    emit changed();
    const auto failed = [this, id](int, QString) {
        m_catalogueLoading = false;
        m_refreshing = false;
        status("Sem atualização do catálogo; dados locais conservados.");
        if (m_contract != id) refreshCatalogue(m_contract);
        scheduleSync(3000);
    };
    request(
        "GET", "/api/contracts/" + id + "/chargers", {},
        [this, id, failed, hadContractScope](QByteArray data) {
            const auto document = QJsonDocument::fromJson(data);
            if (!validMembershipCatalogue(document, id)) {
                failed(0, {});
                return;
            }
            auto rows = document.array();
            QJsonObject contract;
            for (const auto &entry : m_contracts)
                if (entry.toObject()["id"] == id) contract = entry.toObject();
            if (contract.isEmpty() && (hadContractScope || !m_contracts.isEmpty())) { failed(0, {}); return; }
            if (contract["order_only"].toBool() && contract["order_targets"].isArray()) {
                QSet<QString> allowed;
                for (const auto &target : contract["order_targets"].toArray()) allowed.insert(target.toString());
                for (qsizetype i = rows.size(); i-- > 0;)
                    if (!allowed.contains(rows[i].toObject()["id"].toString())) rows.removeAt(i);
            }
            // A catalogue GET can race a single-ID read or a confirmed notes edit.
            // Retain the newer notes without masking report/PCVE catalogue updates.
            auto history = m_historicalNotes.value(id).toObject();
            QHash<QString, QJsonObject> previousNotes;
            for (const auto &entry : m_catalogues.value(id).toArray()) {
                const auto row = entry.toObject();
                if (row.contains("notes_revision")) previousNotes.insert(row["id"].toString(), row);
            }
            for (qsizetype i = 0; i < rows.size(); ++i) {
                auto row = rows[i].toObject();
                const auto membership = row["id"].toString();
                auto old = previousNotes.take(membership);
                if (!row.contains("notes_revision")) {
                    retainContractNote(history, old);
                    continue;
                }
                const auto archived = history.take(membership).toObject();
                if (archived["notes_revision"].toInt(-1) > old["notes_revision"].toInt(-1)) old = archived;
                if (old["notes_revision"].toInt(-1) > row["notes_revision"].toInt()) {
                    row["contract_notes"] = old["contract_notes"];
                    row["notes_revision"] = old["notes_revision"];
                    rows[i] = row;
                }
            }
            for (auto row : rows)
                for (auto removed : row.toObject()["deleted_reports"].toArray())
                    if (m_reports.contains(removed.toString()))
                        if (!(row.toObject()["report_replacements"].toObject().contains(removed.toString()) ?
                              receiveReplacement(removed.toString(), row.toObject()["report_replacements"].toObject()[removed.toString()].toString()) :
                              forgetReport(removed.toString()))) {
                            m_catalogueLoading = false;
                            m_refreshing = false;
                            emit changed();
                            return;
                        }
            QJsonArray stations;
            for (const auto &value : std::as_const(rows)) {
                const auto row = value.toObject();
                if (!row["station_updated"].toString().isEmpty())
                    stations.append(QJsonObject{{"id", row["station_id"]}, {"external_id", row["external_id"]},
                                                {"details", row["details"]}, {"updated", row["station_updated"]}});
            }
            applyStationRecords(stations, false, &rows);
            // Only departed memberships need separate storage. Active notes
            // already live in the listing that is checkpointed below.
            for (const auto &departed : std::as_const(previousNotes)) retainContractNote(history, departed);
            if (history.isEmpty()) m_historicalNotes.remove(id);
            else m_historicalNotes[id] = history;
            m_catalogues[id] = rows;
            m_catalogueTimes[id] = double(QDateTime::currentSecsSinceEpoch());
            if (m_contract == id)
                m_chargers = rows;
            applyContractPresentations();
            m_catalogueLoading = false;
            m_refreshing = false;
            persist();
            status("Catálogo atualizado. Pode trabalhar sem rede.");
            if (m_contract != id)
                refreshCatalogue(m_contract);
            else if (!m_current.isEmpty() && !m_current["dirty"].toBool() &&
                     m_current["revision"].toInt() > 0 && !m_syncing)
                refreshReport();
            scheduleSync(1500);
        },
        "application/json",
        failed);
}

// Explicit refresh never replaces dirty local work. Conflicts have a separate preview/copy flow.
void Lem::refreshReport() {
    if (m_busy || !m_qveCopyTarget.isEmpty() || attachingFiles() || m_current.isEmpty() || !online())
        return;
    if (m_current["dirty"].toBool()) {
        status("Este report tem alterações locais. Envie-as antes de atualizar do servidor.");
        return;
    }
    if (m_syncing) {
        status("Aguarde o envio em curso para atualizar este report.");
        return;
    }
    const auto id = m_current.value("id").toString();
    m_busy = true;
    emit changed();
    request(
        "GET", "/api/reports/" + id, {},
        [this, id](QByteArray data) {
            auto report = QJsonDocument::fromJson(data).object();
            report["dirty"] = false;
            if (m_current.value("id") != id || m_current.value("dirty").toBool()) { m_busy = false; emit changed(); return; }
            getPhotos(report, id);
        },
        "application/json",
        [this, id](int code, QString message) {
            m_busy = false;
            if (code == 410) {
                const auto replacement = QJsonDocument::fromJson(message.toUtf8()).object()["detail"].toObject()["replacement_id"].toString();
                if (!replacement.isEmpty() ? receiveReplacement(id, replacement) : forgetReport(id))
                    status(replacement.isEmpty() ? "Report apagado no servidor; cópia local removida." : "Report substituído por uma correção do administrador.");
            } else
                status("Não foi possível atualizar: " + message);
        });
}

// Count local, downloadable and unavailable files before starting a complete-report download.
QVariantMap Lem::photoDownloadSummary() const {
    int total = 0, missing = 0, unknown = 0, unavailable = 0;
    qint64 bytes = 0;
    for (auto value : m_current["photos"].toArray()) {
        const auto p = value.toObject();
        ++total;
        if (QFileInfo(localPath(QUrl(p["path"].toString()))).size() > 0)
            continue;
        if (!p["uploaded"].toBool()) {
            ++unavailable;
            continue;
        }
        ++missing;
        if (p["size"].toDouble() > 0)
            bytes += qint64(p["size"].toDouble());
        else
            ++unknown;
    }
    return {{"total", total},
            {"missing", missing},
            {"bytes", bytes},
            {"unknown", unknown},
            {"unavailable", unavailable}};
}

bool Lem::currentReportDownloadingPhotos() const {
    const auto id = m_current["id"].toString();
    if (id.isEmpty()) return false;
    if (m_photoBatchReport == id) return true;
    for (const auto &entry : m_current["photos"].toArray())
        if (m_downloads.contains(entry.toObject()["id"].toString())) return true;
    return false;
}

// Pin the report for offline use and queue only missing acknowledged photographs.
void Lem::downloadReportPhotos() {
    if (m_current.isEmpty() || m_user.isEmpty())
        return;
    if (downloadingPhotos()) {
        status("Já existe um descarregamento de report em curso.");
        return;
    }
    const auto reportId = m_current.value("id").toString();
    auto report = m_current;
    const auto previousReports = m_reports;
    report["offline_photos"] = true;
    report["last_access"] = double(QDateTime::currentSecsSinceEpoch());
    m_reports[reportId] = report;
    m_current = report;
    if (!persist()) {
        m_current.remove("offline_photos");
        m_reports = previousReports;
        m_current = previousReports[reportId].toObject();
        emit changed();
        return;
    }
    m_photoBatchQueue.clear();
    m_photoBatchPending.clear();
    for (auto value : report["photos"].toArray()) {
        const auto p = value.toObject();
        if (p["uploaded"].toBool() &&
            QFileInfo(localPath(QUrl(p["path"].toString()))).size() <= 0) {
            m_photoBatchQueue.append(p["id"].toString());
            m_photoBatchPending.insert(p["id"].toString());
        }
    }
    if (m_photoBatchQueue.isEmpty()) {
        m_photoBatchState =
            photoDownloadSummary()["unavailable"].toInt() > 0
                ? "Existem fotografias locais em falta que ainda não foram enviadas; não podem ser recuperadas do servidor."
                : "Todas as fotografias disponíveis no dispositivo; report mantido offline.";
        status(m_photoBatchState);
        return;
    }
    if (!online())
        return;
    m_photoBatchReport = reportId;
    m_photoBatchTotal = m_photoBatchQueue.size();
    m_photoBatchDone = 0;
    m_photoBatchFailures = 0;
    m_photoBatchLastError.clear();
    m_photoBatchStopped = false;
    pumpPhotoBatch();
}

// Stop queued work; already active requests are allowed to finish and retain their results.
void Lem::cancelReportPhotoDownloads() {
    if (!downloadingPhotos())
        return;
    m_photoBatchStopped = true;
    for (const auto &id : m_photoBatchQueue)
        m_photoBatchPending.remove(id);
    m_photoBatchQueue.clear();
    pumpPhotoBatch();
}

// Remove the explicit offline pin; normal draft/dirty/current-report safeguards still apply.
void Lem::releaseReportPhotos() {
    const auto id = m_current.value("id").toString();
    if (id.isEmpty() || m_photoBatchReport == id)
        return;
    const auto previous = m_current;
    m_current["offline_photos"] = false;
    m_reports[id] = m_current;
    if (!persist()) {
        m_current = previous;
        m_reports[id] = previous;
        emit changed();
        return;
    }
    cleanPhotoCache();
    status(
        "Report incluído novamente na limpeza automática de cópias locais. Rascunhos e envios pendentes continuam protegidos.");
}

// Share a bounded four-request limit with individual evidence downloads.
void Lem::pumpPhotoBatch() {
    if (!downloadingPhotos())
        return;
    while (!m_photoBatchStopped && !m_photoBatchQueue.isEmpty() && m_downloads.size() < NetworkSecurity::EvidenceConcurrency) {
        const auto id = m_photoBatchQueue.takeFirst();
        if (!m_photoBatchPending.contains(id) || m_downloads.contains(id))
            continue;
        fetchPhotoForReport(m_photoBatchReport, id);
    }
    const auto reportLabel = m_reports.value(m_photoBatchReport)
                                 .toObject()["snapshot"]
                                 .toObject()["charger"]
                                 .toObject()["external_id"]
                                 .toString(m_photoBatchReport.left(8));
    if (m_photoBatchPending.isEmpty()) {
        const bool saved = persist(); // Final checkpoint includes every completed file.
        bool complete = true;
        for (auto value : m_reports.value(m_photoBatchReport).toObject()["photos"].toArray())
            if (QFileInfo(localPath(QUrl(value.toObject()["path"].toString()))).size() <= 0)
                complete = false;
        m_photoBatchState =
            (m_photoBatchStopped && m_photoBatchDone < m_photoBatchTotal) || !complete
                ? "Descarregamento interrompido: " + QString::number(m_photoBatchDone) + " / " +
                      QString::number(m_photoBatchTotal) + ". Pode repetir para obter as restantes."
                : "Todas as fotografias descarregadas; report mantido offline.";
        m_photoBatchState += " · " + reportLabel;
        if (!complete && !m_photoBatchLastError.isEmpty()) m_photoBatchState += " · " + m_photoBatchLastError;
        if (!saved) m_photoBatchState += " · Não foi possível guardar o catálogo local.";
        m_photoBatchReport.clear();
        cleanPhotoCache();
        status(m_photoBatchState);
        return;
    }
    m_photoBatchState =
        "Evidências de " + reportLabel + ": " + QString::number(m_photoBatchDone) + " / " +
        QString::number(m_photoBatchTotal) +
        (m_photoBatchStopped ? " · a terminar pedidos em curso" : " · pode continuar a trabalhar");
    emit changed();
}

// Reconcile one batch item and continue or report a resumable partial result.
void Lem::photoDownloadFinished(QString reportId, QString photoId, bool success) {
    if (m_photoBatchReport == reportId && m_photoBatchPending.remove(photoId)) {
        if (success)
            ++m_photoBatchDone;
        else {
            ++m_photoBatchFailures;
            m_photoBatchLastError = m_message;
            // A permanently failed item must not discard the rest of the queue.
        }
    }
    // Queue advancement is deferred so error callbacks cannot recurse through an entire batch.
    QTimer::singleShot(0, this, [this] { pumpPhotoBatch(); });
    QTimer::singleShot(0, this, &Lem::receiveFollowedReports);
    emit changed();
}
void Lem::fetchPhoto(QString photoId) {
    fetchPhotoForReport(m_current.value("id").toString(), photoId);
}

// Capture reportId/photoId in callbacks so navigation cannot attach bytes to another report.
// Check the server digest before atomically publishing the downloaded private file.
void Lem::fetchPhotoForReport(QString reportId, QString photoId) {
    if (m_user.isEmpty() || m_token.isEmpty()) {
        online();
        photoDownloadFinished(reportId, photoId, false);
        return;
    }
    if (!SafeStorage::component(photoId) || !SafeStorage::component(reportId)) {
        status("Identificador remoto inválido; fotografia não descarregada.");
        photoDownloadFinished(reportId, photoId, false);
        return;
    }
    if (m_downloads.contains(photoId))
        return;
    QJsonObject photo;
    for (auto value : m_reports.value(reportId).toObject()["photos"].toArray())
        if (value.toObject()["id"].toString() == photoId)
            photo = value.toObject();
    if (photo.isEmpty() || !photo["uploaded"].toBool()) {
        photoDownloadFinished(reportId, photoId, false);
        return;
    }
    if (m_downloads.size() >= NetworkSecurity::EvidenceConcurrency) {
        status("Já existem quatro evidências a descarregar. Aguarde antes de pedir outra.");
        return;
    }
    const QString path = SafeStorage::photoPath(cacheDir(), photoId, photo["mime"].toString());
    QDir().mkpath(cacheDir() + "/photos");
    m_downloads.insert(photoId);
    emit changed();
    requestPhoto(
        "/api/reports/" + reportId + "/photos/" + photoId, path,
        (photo["mime"].toString().startsWith("video/") ? 60LL : 20LL) * 1024 * 1024,
        [this, photoId, reportId, path, photo](QSharedPointer<NetworkSecurity::FileBody> body) {
            m_downloads.remove(photoId);
            if (body->writeFailed) {
                status("Não foi possível guardar a fotografia.");
                photoDownloadFinished(reportId, photoId, false);
                return;
            }
            if (body->received <= 0 || (photo["size"].toDouble() > 0 && body->received != qint64(photo["size"].toDouble())) ||
                !body->matches(photo["digest"].toString())) {
                status("Fotografia inválida; tente descarregar novamente.");
                photoDownloadFinished(reportId, photoId, false);
                return;
            }
            auto report = m_reports.value(reportId).toObject();
            auto photos = report["photos"].toArray();
            int found = -1;
            for (int i = 0; i < photos.size(); ++i)
                if (photos[i].toObject()["id"].toString() == photoId)
                    found = i;
            if (found < 0) {
                photoDownloadFinished(reportId, photoId, false);
                return;
            }
            if (!body->commit()) {
                status("Não foi possível guardar a fotografia.");
                photoDownloadFinished(reportId, photoId, false);
                return;
            }
            auto p = photos[found].toObject();
            p["path"] = QUrl::fromLocalFile(path).toString();
            photos[found] = p;
            report["photos"] = photos;
            report["last_access"] = double(QDateTime::currentSecsSinceEpoch());
            m_reports[reportId] = report;
            if (m_current.value("id").toString() == reportId)
                m_current = report;
            // Avoid rewriting the entire catalogue for every file in a large batch.
            // Files are already atomic; checkpoint each eight successes and at finish.
            if ((m_photoBatchReport != reportId || (m_photoBatchDone + 1) % 8 == 0) && !persist()) {
                photoDownloadFinished(reportId, photoId, false);
                return;
            }
            cleanPhotoCache();
            if (!downloadingPhotos())
                status("Fotografia disponível no dispositivo.");
            photoDownloadFinished(reportId, photoId, true);
        },
        [this, photoId, reportId](int code, QString detail) {
            m_downloads.remove(photoId);
            if (code == 401 || code == 403 || code == 423 || code == 426) cancelReportPhotoDownloads();
            status("Não foi possível descarregar a evidência: " + detail);
            photoDownloadFinished(reportId, photoId, false);
        }, photo["digest"].toString(), {}, 0,
        [this, reportId, batch = m_photoBatchReport == reportId] {
            return !batch || (m_photoBatchReport == reportId && !m_photoBatchStopped);
        });
}

// Manual cleanup evicts only fully acknowledged evidence bytes. Report metadata
// and answers remain available offline and visible without a catalogue refresh.
// Protect unpublished/unsent/conflicted work; never delete gallery originals.
void Lem::clearLocalCopies() {
    if (!storageWritable()) return;
    if (m_user.isEmpty()) {
        status("Não existe uma conta local para limpar.");
        return;
    }
    if (switchingBlocked()) {
        status("Aguarde as transferências antes de limpar as cópias locais.");
        return;
    }
    if (!flushEdits())
        return;
    const auto previousReports = m_reports, previousCurrent = m_current;
    int cleared = 0, kept = 0;
    QSet<QString> protectedPaths;
    for (const auto &id : m_reports.keys()) {
        const auto r = m_reports[id].toObject();
        bool uploaded = true;
        for (auto p : r["photos"].toArray())
            if (!p.toObject()["uploaded"].toBool())
                uploaded = false;
        const auto state = r["status"].toString();
        if ((state == "closed" || state == "draft") && !r["dirty"].toBool() &&
            !r["_conflict"].toBool() && r["deleted_photos"].toArray().isEmpty() &&
            r["revision"].toInt() > 0 && uploaded) {
            auto retained = r;
            auto photos = retained["photos"].toArray();
            for (int index = 0; index < photos.size(); ++index) {
                auto photo = photos[index].toObject();
                photo.remove("path");
                photos[index] = photo;
            }
            retained["photos"] = photos;
            retained["offline_photos"] = false;
            m_reports[id] = retained;
            if (m_current["id"].toString() == id) m_current = retained;
            ++cleared;
        } else {
            ++kept;
            for (auto p : r["photos"].toArray())
                protectedPaths.insert(
                    QFileInfo(localPath(QUrl(p.toObject()["path"].toString()))).absoluteFilePath());
        }
    }
    if (!persist()) {
        m_reports = previousReports;
        m_current = previousCurrent;
        emit changed();
        return;
    }
    // Private, unreferenced images include leftovers from interrupted earlier cleanups.
    int photos = 0, failures = 0;
    qint64 bytes = 0;
    m_photoBytes = 0;
    const QDir folder(cacheDir() + "/photos");
    for (const auto &file : folder.entryInfoList(QDir::Files | QDir::NoSymLinks)) {
        const auto size = file.size();
        if (!protectedPaths.contains(file.absoluteFilePath())) {
            if (QFile::remove(file.absoluteFilePath())) {
                ++photos;
                bytes += size;
                continue;
            }
            ++failures;
        }
        m_photoBytes += size;
    }
    status(
        QString::number(cleared) + " report(s) conservados; " +
        QString::number(photos) + " evidência(s) local(is) sincronizada(s) removida(s) · " +
        QString::number(bytes / 1048576.0, 'f', 1) +
        " MB libertados. " + QString::number(kept) +
        " report(s) com dados por enviar, não publicados ou em conflito conservados integralmente. Respostas e histórico mantidos neste dispositivo e no servidor." +
        (failures ? " Não foi possível remover " + QString::number(failures) +
                        " ficheiro(s); tente novamente."
                  : ""));
}

// Automatic image eviction: oldest eligible images first, using a 256 MiB budget/7-day age.
// Dirty, draft, active and explicitly pinned reports are excluded from automatic eviction.
void Lem::cleanPhotoCache() {
    if (m_localReadError || m_user.isEmpty() || !m_qveCopyTarget.isEmpty() || attachingFiles() || m_syncing || !m_downloads.isEmpty() || downloadingPhotos())
        return;
    constexpr qint64 budget = 256LL * 1024 * 1024, age = 7 * 24 * 60 * 60;
    const auto now = QDateTime::currentSecsSinceEpoch();
    const QDir folder(cacheDir() + "/photos");
    m_photoBytes = 0;
    for (const auto &file : folder.entryInfoList(QDir::Files | QDir::NoSymLinks))
        m_photoBytes += file.size();
    struct Candidate {
        QString report;
        QString photo;
        QString path;
        qint64 size;
        double access;
    };
    QList<Candidate> candidates;
    for (auto it = m_reports.begin(); it != m_reports.end(); ++it) {
        const auto r = it.value().toObject();
        if (r["offline_photos"].toBool() || r["dirty"].toBool() || r["_conflict"].toBool() ||
            r["revision"].toInt() <= 0 || !r["deleted_photos"].toArray().isEmpty() ||
            r["status"].toString() != "closed" || r["id"] == m_current.value("id"))
            continue;
        for (auto value : r["photos"].toArray()) {
            const auto p = value.toObject();
            const QFileInfo file(localPath(QUrl(p["path"].toString())));
            if (!p["uploaded"].toBool() || !file.isFile() || file.isSymLink() ||
                file.canonicalPath() != QFileInfo(folder.path()).canonicalFilePath())
                continue;
            candidates.append({it.key(), p["id"].toString(), file.absoluteFilePath(), file.size(),
                               r["last_access"].toDouble()});
        }
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate &a, const Candidate &b) { return a.access < b.access; });
    bool changed = false;
    for (const auto &candidate : candidates) {
        if (m_photoBytes <= budget && now - candidate.access < age)
            continue;
        if (!QFile::remove(candidate.path))
            continue;
        m_photoBytes -= candidate.size;
        auto report = m_reports[candidate.report].toObject();
        auto photos = report["photos"].toArray();
        for (int i = 0; i < photos.size(); ++i) {
            auto p = photos[i].toObject();
            if (p["id"].toString() == candidate.photo) {
                p["path"] = "";
                photos[i] = p;
            }
        }
        report["photos"] = photos;
        m_reports[candidate.report] = report;
        changed = true;
    }
    if (changed)
        persist();
    emit this->changed();
}

// The protected local report remains the authoritative local copy. Server previews
// are stored separately, never merged over answers or queued photo deletions.

// Store the server response under _conflict_remote for a read-only comparison.
// The original local payload, photo queue and ownership stay intact.
void Lem::viewServerReport() {
    if (reportActionBlocked() || m_current.isEmpty() || !online())
        return;
    const auto id = m_current.value("id").toString();
    m_busy = true;
    emit changed();
    request("GET", "/api/reports/" + id, {}, [this, id](QByteArray data) {
        if (!m_reports.contains(id)) {
            m_busy = false;
            status("O report já não existe neste dispositivo.");
            return;
        }
        auto r = m_reports.value(id).toObject();
        const auto remote = QJsonDocument::fromJson(data).object();
        if (!validRemoteReport(remote, id)) {
            m_busy = false;
            status("Resposta de report inválida. Os dados locais foram conservados.");
            return;
        }
        r["_conflict_remote"] = remote;
        m_reports[id] = r;
        if (m_current.value("id").toString() == id)
            m_current = r;
        m_busy = false;
        persist();
        status("Versão do servidor consultada; os dados locais foram conservados.");
    });
}

// Ask the server for users eligible for this report, rather than trusting a local user list.
void Lem::loadReportOwners() {
    if (reportActionBlocked() || m_current.isEmpty() || m_user["role"].toString() != "admin" ||
        !online())
        return;
    const auto id = m_current.value("id").toString();
    m_busy = true;
    m_users = {};
    emit changed();
    request("GET", "/api/reports/" + id + "/owners", {}, [this](QByteArray data) {
        m_users = QJsonDocument::fromJson(data).array();
        m_busy = false;
        status("Selecione o novo proprietário; a transferência requer confirmação.");
    });
}

// Require a clean server-backed draft and submit the exact revision being transferred.
void Lem::transferReport(QString ownerId) {
    if (reportActionBlocked() || m_current.isEmpty() || m_user["role"].toString() != "admin" ||
        m_current["status"].toString() != "draft" || !online())
        return;
    if (m_current["dirty"].toBool() || m_current["_conflict"].toBool()) {
        status(
            "Não é possível transferir sobre alterações locais. Preserve-as como novo rascunho e abra a versão atual do report.");
        return;
    }
    const auto id = m_current.value("id").toString();
    m_busy = true;
    emit changed();
    request(
        "POST", "/api/reports/" + id + "/owner",
        compact({{"owner_id", ownerId}, {"base_revision", m_current["revision"]}}),
        [this, id](QByteArray data) {
            auto r = QJsonDocument::fromJson(data).object();
            r["dirty"] = false;
            if (!getPhotos(r, id)) return;
            status("Propriedade transferida. Só o novo proprietário pode editar ou concluir.");
        },
        "application/json",
        [this](int, QString message) {
            m_busy = false;
            status("Transferência não efetuada: " + message +
                   ". Atualize o report antes de tentar novamente.");
        });
}

// Create an independent draft only when EVERY source photograph is available locally.
// Copy files off the UI thread, assign fresh report/photo UUIDs, then checkpoint both
// the new report and the original report reference; roll back memory/files on failure.
void Lem::copyLocalReport() {
    if (reportActionBlocked() || m_current.isEmpty() || m_user.isEmpty())
        return;
    const auto source = m_current;
    const auto sourceId = source.value("id").toString();
    const auto summary = photoDownloadSummary();
    if (summary["unavailable"].toInt()) {
        status(
            "Há fotografias locais em falta. A cópia não foi criada; recupere os ficheiros primeiro. O original foi conservado.");
        return;
    }
    if (summary["missing"].toInt()) {
        status(
            "Descarregue todas as fotografias antes de criar a cópia. O original e as alterações locais foram conservados.");
        return;
    }
    auto copy = source;
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto date = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    for (const auto &key : copy.keys())
        if (key.startsWith('_'))
            copy.remove(key);
    copy.remove("finished");
    copy.remove("last_sync");
    copy.remove("updated");
    copy.remove("deleted_photos");
    copy.remove("correction");
    copy["id"] = id;
    copy["owner_id"] = m_user["id"];
    copy["technician"] = m_user["name"];
    copy["revision"] = 0;
    copy["status"] = "draft";
    // The marker belongs to the old UUID's unfinished historical upload, never
    // to a new independent draft created to preserve work after a conflict.
    auto copySchema = copy["template"].toObject();
    copySchema.remove("frozen_upload");
    copy["template"] = copySchema;
    copy["dirty"] = true;
    copy["started"] = date;
    copy["last_access"] = double(QDateTime::currentSecsSinceEpoch());
    auto payload = copy["payload"].toObject();
    payload["started"] = date;
    payload.remove("finished");
    payload["source_report_id"] = sourceId;
    copy["payload"] = payload;
    copy = Checklist::correctedDraft(copy);
    payload = copy["payload"].toObject();
    QJsonArray photos;
    QJsonObject copiedPhotoIds;
    QList<QPair<QString, QString>> files;
    for (auto value : copy["photos"].toArray()) {
        auto p = value.toObject();
        const auto pid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        auto mime = p["mime"].toString();
        if (mime.isEmpty()) mime = QFileInfo(localPath(QUrl(p["path"].toString()))).suffix().toLower() == "png" ? "image/png" : "image/jpeg";
        const auto target = SafeStorage::photoPath(cacheDir(), pid, mime);
        const auto ext = "." + QFileInfo(target).suffix();
        p["mime"] = mime;
        p["filename"] = pid + ext;
        files.append({localPath(QUrl(p["path"].toString())), target});
        copiedPhotoIds[p["id"].toString()] = pid;
        p["id"] = pid;
        p["path"] = QUrl::fromLocalFile(target).toString();
        p["uploaded"] = false;
        p.remove("digest");
        photos.append(p);
    }
    for (const auto &field : {"signatures", "legacy_signatures"}) {
        auto signatures = payload.value(field).toObject();
        for (const auto &key : signatures.keys()) {
            auto signature = signatures[key].toObject();
            const auto oldId = signature["photo_id"].toString();
            bool validSource = false;
            for (auto value : source["photos"].toArray()) {
                const auto photo = value.toObject();
                // Historical signatures moved to ANT checkpoints during migration;
                // their old question key no longer identifies the evidence group.
                if (photo["id"] == oldId && photo["mime"] == "image/png" &&
                    (QString(field) == "legacy_signatures" || photo["checkpoint"] == key))
                    validSource = true;
            }
            if (!validSource || !copiedPhotoIds.contains(oldId)) {
                status("A assinatura referencia uma fotografia em falta ou inválida. A cópia não foi criada; o original foi conservado.");
                return;
            }
            signature["photo_id"] = copiedPhotoIds[oldId];
            signatures[key] = signature;
        }
        if (payload.contains(field)) payload[field] = signatures;
    }
    copy["payload"] = payload;
    copy["photos"] = photos;
    m_busy = true;
    status("A criar uma cópia independente com todas as fotografias…");
    auto watcher = new QFutureWatcher<bool>(this);
    connect(
        watcher, &QFutureWatcher<bool>::finished, this, [this, watcher, copy, files, id, sourceId] {
            const bool ok = watcher->result();
            watcher->deleteLater();
            const auto previousReports = m_reports;
            const auto previousCurrent = m_current;
            bool saved = false;
            if (ok) {
                auto original = m_reports[sourceId].toObject();
                original["_conflict_copy_id"] = id;
                original["_conflict_copy_seq"] = original["_edit_seq"].toDouble();
                m_reports[sourceId] = original;
                m_reports[id] = copy;
                m_current = copy;
                saved = persist();
            }
            if (!saved) {
                m_reports = previousReports;
                m_current = previousCurrent;
                for (const auto &f : files)
                    QFile::remove(f.second);
                m_busy = false;
                status("Não foi possível guardar a cópia; o report original foi conservado.");
                return;
            }
            for (const auto &f : files)
                m_photoBytes += QFileInfo(f.second).size();
            m_template = copy["template"].toObject();
            m_busy = false;
            status(
                "Novo rascunho criado com respostas e fotografias. A cópia original foi conservada.");
            scheduleSync();
        });
    watcher->setFuture(QtConcurrent::run(&m_editPool, [files] {
        for (const auto &f : files)
            if (!QFile::copy(f.first, f.second))
                return false;
        return true;
    }));
}

// Replace the local view only after checking an independent, complete, unedited backup.
// Fetch the current server version again instead of trusting an older comparison snapshot.
void Lem::useServerReport() {
    if (reportActionBlocked() || m_current.isEmpty() || !online())
        return;
    const auto copyId = m_current["_conflict_copy_id"].toString();
    if (copyId.isEmpty() || !m_reports.contains(copyId)) {
        status(
            "Crie primeiro uma cópia independente para preservar as alterações e fotografias locais.");
        return;
    }
    const auto backup = m_reports[copyId].toObject();
    if (m_current["_conflict_copy_seq"].toDouble() != m_current["_edit_seq"].toDouble() ||
        backup["_edit_seq"].toDouble() != 0) {
        status(
            "A cópia ou o original foi alterado entretanto. Crie outra cópia antes de abrir a versão do servidor.");
        return;
    }
    for (auto p : backup["photos"].toArray())
        if (QFileInfo(localPath(QUrl(p.toObject()["path"].toString()))).size() <= 0) {
            status(
                "A cópia tem fotografias em falta. Crie outra cópia completa antes de continuar.");
            return;
        }
    const auto id = m_current.value("id").toString();
    m_busy = true;
    emit changed();
    request("GET", "/api/reports/" + id, {}, [this, id](QByteArray data) {
        auto r = QJsonDocument::fromJson(data).object();
        r["dirty"] = false;
        if (!getPhotos(r, id)) return;
        status(
            "Versão atual do servidor aberta. As alterações anteriores estão no novo rascunho criado como cópia.");
    });
}
