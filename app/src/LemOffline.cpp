// Local durability and synchronization for the Lem controller.
// Reading order: journals -> scheduling -> report/photograph uploads -> catalogue
// refresh -> photograph downloads/cleanup -> ownership/conflict recovery.
// Worker tasks receive value snapshots; shared controller state stays on its thread.
// See docs/architecture.md for ordering and failure cases.

#include "Lem.h"
#include "Checklist.h"
#include "SafeStorage.h"
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QDir>
#include <QJsonDocument>
#include <QDateTime>
#include <QCryptographicHash>
#include <QNetworkInformation>
#include <algorithm>
#include <QtConcurrent/QtConcurrentRun>
#include <QThread>
#include <QUuid>
namespace {
QByteArray compact(QJsonObject o) {
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}
} // namespace
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
    m_pendingEdits[report["id"].toString()] = report;
    emit localSaveStateChanged();
    if (m_activeEditId.isEmpty() && !m_editTimer.isActive())
        m_editTimer.start(20);
}

// Start at most one immutable journal snapshot on the worker pool.
void Lem::writeNextEdits() {
    if (m_flushingEdits || !m_activeEditId.isEmpty() || m_pendingEdits.isEmpty())
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

// Durability boundary for identity changes, full checkpoints and application backgrounding.
// This deliberately waits for the active write, then synchronously saves the latest snapshots.
bool Lem::flushEdits() {
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
    if (m_token.isEmpty() || m_server.startsWith("demo:") || pendingCount() == 0)
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
    if (!report["_defer_photo_sync"].toBool())
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

// Select eligible dirty reports. Manual send releases photograph holds but does not
// override a durable ownership/revision conflict requiring a user decision.
void Lem::startSync(bool manual) {
    if (m_busy || !m_qveCopyTarget.isEmpty() || attachingFiles() || m_syncing || m_refreshing || m_catalogueLoading) {
        scheduleSync(3000);
        return;
    }
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
    for (auto i = m_reports.begin(); i != m_reports.end(); ++i)
        if (i.value().toObject()["_conflict"].toBool())
            m_syncConflicts.insert(i.key());
    QStringList ids;
    for (auto i = m_reports.begin(); i != m_reports.end(); ++i)
        if (i.value().toObject()["dirty"].toBool() && !i.value().toObject()["_conflict"].toBool() &&
            !m_syncConflicts.contains(i.key()) &&
            (manual || !i.value().toObject()["_defer_photo_sync"].toBool()))
            ids << i.key();
    if (ids.isEmpty()) {
        m_syncState =
            pendingCount()
                ? (m_syncConflicts.isEmpty()
                       ? "Fotografias guardadas · envio ao mudar de grupo ou sair do report"
                       : "Existem reports que precisam de atenção")
                : "Sem envios pendentes";
        emit changed();
        return;
    }
    m_syncTotal = ids.size();
    m_syncCompleted = 0;
    m_autoSync.stop();
    m_manualSync = manual;
    m_syncing = true;
    m_syncState = "A enviar em segundo plano · pode continuar a trabalhar";
    emit changed();
    syncNext(ids, 0);
}

// Classify failure before choosing recovery: 410 removes a deleted report; 403/409
// protect conflicting work; 422 needs attention; 401 needs login; other failures retry.
void Lem::syncFailed(int code, QString message) {
    const auto detail = QJsonDocument::fromJson(message.toUtf8()).object()["detail"];
    if (detail.isString())
        message = detail.toString();
    else if (detail.isObject() && detail.toObject()["message"].isString())
        message = detail.toObject()["message"].toString();
    m_syncing = false;
    if (code == 410) {
        const auto id = m_syncReport;
        if (forgetReport(id)) {
            status("Este report foi apagado no servidor; cópia local removida.");
            scheduleSync(1500);
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
        status(m_syncState);
        scheduleSync(1500);
    } else if (rejected) {
        if (code == 409 || code == 403) {
            auto r = m_reports.value(m_syncReport).toObject();
            r["_conflict"] = true;
            r["_conflict_reason"] = message;
            r["_edit_seq"] = r["_edit_seq"].toDouble() + 1;
            m_reports[m_syncReport] = r;
            if (m_current.value("id").toString() == m_syncReport)
                m_current = r;
            saveEdits(r);
            persist();
        }
        m_syncConflicts.insert(m_syncReport);
        m_syncState = "Envio precisa de atenção · dados locais conservados";
        status(m_syncState + ": " + message);
        scheduleSync(1500);
    } else if (code == 401) {
        m_token.clear();
        m_autoSync.stop();
        m_syncState = "Entre novamente para enviar · dados locais conservados";
        status(m_syncState);
    } else {
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

// Merge acknowledgements into the LATEST local report, never replace it with sent data.
// Only matching edit sequences and fully acknowledged photos/deletions clear dirty.
bool Lem::mergeSync(QJsonObject sent, bool complete) {
    const auto id = sent["id"].toString();
    if (!m_reports.contains(id)) {
        syncFailed(409, "Report local indisponível.");
        return false;
    }
    auto latest = m_reports[id].toObject();
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
    }
    if (complete) {
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
    if (sent["status"] == "closed") applyClosedIdentity(sent);
    m_reports[id] = latest;
    if (m_current.value("id").toString() == id)
        m_current = latest;
    if (!persist()) {
        latest["dirty"] = true;
        m_reports[id] = latest;
        if (m_current.value("id").toString() == id)
            m_current = latest;
        m_syncing = false;
        m_syncState = "Falha ao guardar o progresso; envio continua pendente";
        scheduleSync(30000);
        emit changed();
        return false;
    }
    emit changed();
    return true;
}

// For one queued report, compare owner/revision with the server before sending.
// The durable in-flight payload/revision pair identifies a previously accepted PUT
// whose acknowledgement was lost, without accepting unrelated remote edits.
void Lem::syncNext(QStringList ids, int index) {
    m_syncCompleted = index;
    if (index >= ids.size()) {
        m_syncing = false;
        m_retrySeconds = 30;
        m_retryNotBefore = 0;
        m_syncReport.clear();
        m_syncState = pendingCount() ? "Alterações locais pendentes de envio"
                                     : "Todos os envios confirmados pelo servidor";
        if (!m_syncConflicts.isEmpty())
            m_syncState = QString::number(m_syncConflicts.size()) +
                          " report(s) precisam de atenção; restantes envios tratados";
        cleanPhotoCache();
        status(m_syncState);
        scheduleSync(1500);
        return;
    }
    const auto id = ids[index];
    auto r = m_reports.value(id).toObject();
    m_syncReport = id;
    if (r.isEmpty() || !r["dirty"].toBool() || (!m_manualSync && r["_defer_photo_sync"].toBool())) {
        syncNext(ids, index + 1);
        return;
    }
    m_syncState =
        "Report " + QString::number(index + 1) + " / " + QString::number(ids.size()) +
        " · verificar dados no servidor · " +
        r["snapshot"].toObject()["charger"].toObject()["external_id"].toString(id.left(8));
    emit operationChanged();
    r["_ack_deleted"] = QJsonArray{};
    auto send = [this, ids, index](QJsonObject local) {
        const auto id = local["id"].toString();
        if (m_deletedReports.contains(id)) {
            syncNext(ids, index + 1);
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
        syncRequest("PUT", "/api/reports/" + id,
                    compact({{"charger_id", local["charger_id"]},
                             {"base_revision", local["revision"]},
                             {"checklist_version", local["template"].toObject()["version"]},
                             {"load_test_connectors", local["template"].toObject()["load_test_connectors"]},
                             {"status", "draft"},
                             {"payload", local["payload"]}}),
                    [this, ids, index, local](QByteArray data) mutable {
                        const auto saved = QJsonDocument::fromJson(data).object();
                        if (!saved.contains("revision")) {
                            syncFailed(0, "Resposta inválida do servidor.");
                            return;
                        }
                        for (const auto &key : {"revision", "snapshot", "template", "started"})
                            if (saved.contains(key))
                                local[key] = saved[key];
                        if (mergeSync(local))
                            syncPhotos(ids, index, local, 0);
                    });
    };
    const QString path = "/api/reports/" + id + (r["revision"].toInt() == 0 ? "/exists" : "");
    syncRequest("GET", path, {}, [this, ids, index, r, send](QByteArray data) mutable {
        const auto remote = QJsonDocument::fromJson(data).object();
        if (m_deletedReports.contains(r["id"].toString())) {
            syncNext(ids, index + 1);
            return;
        }
        if (r["revision"].toInt() == 0 && !remote["exists"].toBool()) {
            send(r);
            return;
        }
        if (remote["charger_id"] != r["charger_id"]) {
            syncFailed(409, "O carregador do report no servidor é diferente.");
            return;
        }
        if (remote["owner_id"] != r["owner_id"]) {
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
            if (mergeSync(r, true))
                syncNext(ids, index + 1);
            return;
        }
        if (remote["revision"] != r["revision"]) {
            const bool lostAck = remote["payload"] == r["_inflight_payload"] &&
                                 remote["revision"] == r["_inflight_revision"];
            // A photograph DELETE advances the server revision by one. If only its
            // acknowledgement was lost, the server has the answers this device sent,
            // exactly one more revision, and no longer has the photograph queued for
            // removal. Adopt that revision; the retried DELETE is idempotent.
            bool lostDeleteAck = false;
            const auto queuedDeletions = r["deleted_photos"].toArray();
            if (!lostAck && !queuedDeletions.isEmpty() &&
                r.value("_inflight_delete_id") == queuedDeletions.first() &&
                r.value("_inflight_delete_revision") == r["revision"] &&
                remote["revision"].toInt() == r["revision"].toInt() + 1 &&
                (samePayload || remote["payload"] == r["_inflight_payload"])) {
                lostDeleteAck = true;
                const auto pending = queuedDeletions.first().toString();
                for (auto value : remote["photos"].toArray())
                    if (value.toObject()["id"].toString() == pending)
                        lostDeleteAck = false;
            }
            if (!lostAck && !lostDeleteAck) {
                syncFailed(409, "Report alterado noutro dispositivo. Rascunho local conservado.");
                return;
            }
        }
        r["revision"] = remote["revision"];
        if (samePayload) {
            if (mergeSync(r))
                syncPhotos(ids, index, r, 0);
        } else
            send(r);
    });
}

// Process deletions, then missing uploads, then final closure. Each stage continues
// only after acknowledgement; mergeSync preserves edits made during network waits.
void Lem::syncPhotos(QStringList ids, int index, QJsonObject r, int photoIndex) {
    auto deleted = r["deleted_photos"].toArray();
    const auto id = r["id"].toString();
    if (!deleted.isEmpty()) {
        const auto pid = deleted.first().toString();
        // Persist this exact DELETE intent before sending it. Recovery must not
        // infer that another device's deletion was our lost acknowledgement.
        auto latest = m_reports.value(id).toObject();
        latest["_inflight_delete_id"] = pid;
        latest["_inflight_delete_revision"] = r["revision"];
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
                    {}, [this, ids, index, r, deleted, photoIndex, pid](QByteArray data) mutable {
                        const auto saved = QJsonDocument::fromJson(data).object();
                        if (!saved.contains("revision")) {
                            syncFailed(0, "Resposta inválida ao remover fotografia.");
                            return;
                        }
                        deleted.removeAt(0);
                        r["deleted_photos"] = deleted;
                        r["revision"] = saved["revision"];
                        auto ack = r["_ack_deleted"].toArray();
                        ack.append(pid);
                        r["_ack_deleted"] = ack;
                        if (mergeSync(r))
                            syncPhotos(ids, index, r, photoIndex);
                    });
        return;
    }
    auto photos = r["photos"].toArray();
    if (photoIndex < photos.size()) {
        auto p = photos[photoIndex].toObject();
        if (p["uploaded"].toBool()) {
            syncPhotos(ids, index, r, photoIndex + 1);
            return;
        }
        m_syncState = "Report " + QString::number(index + 1) + " / " + QString::number(ids.size()) +
                      " · enviar fotografia " + QString::number(photoIndex + 1) + " / " +
                      QString::number(photos.size()) + " · " + p["checkpoint"].toString();
        emit operationChanged();
        QFile file(localPath(QUrl(p["path"].toString())));
        if (!file.open(QIODevice::ReadOnly)) {
            syncFailed(422, "Fotografia local em falta; report conservado.");
            return;
        }
        multipart(
            "PUT", "/api/reports/" + id + "/photos/" + p["id"].toString(),
            {{"checkpoint", p["checkpoint"].toString()},
             {"base_revision", QString::number(r["revision"].toInt())}},
            p["filename"].toString(), file.readAll(),
            [this, ids, index, r, photos, p, photoIndex](QByteArray data) mutable {
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
                    syncPhotos(ids, index, r, photoIndex + 1);
            },
            [this](int code, QString message) { syncFailed(code, message); });
        return;
    }
    if (r["status"].toString() == "closed") {
        syncRequest("PUT", "/api/reports/" + id,
                    compact({{"charger_id", r["charger_id"]},
                             {"base_revision", r["revision"]},
                             {"checklist_version", r["template"].toObject()["version"]},
                             {"load_test_connectors", r["template"].toObject()["load_test_connectors"]},
                             {"status", "closed"},
                             {"payload", r["payload"]}}),
                    [this, ids, index, r](QByteArray data) mutable {
                        const auto saved = QJsonDocument::fromJson(data).object();
                        if (!saved.contains("revision")) {
                            syncFailed(0, "Resposta inválida ao concluir.");
                            return;
                        }
                        r["revision"] = saved["revision"];
                        if (mergeSync(r, true))
                            syncNext(ids, index + 1);
                    });
    } else if (mergeSync(r, true))
        syncNext(ids, index + 1);
}

// Use the local catalogue for 15 minutes unless explicitly forced.
// Server deletion markers remove stale local copies before installing fresh summaries.
void Lem::refreshCatalogue(QString id, bool force) {
    if (id.isEmpty() || m_token.isEmpty() || m_catalogueLoading)
        return;
    const auto last = m_catalogueTimes[id].toDouble();
    if (!force && m_catalogues.contains(id) && QDateTime::currentSecsSinceEpoch() - last < 15 * 60)
        return;
    m_catalogueLoading = true;
    m_refreshing = true;
    emit changed();
    request(
        "GET", "/api/contracts/" + id + "/chargers", {},
        [this, id](QByteArray data) {
            const auto rows = QJsonDocument::fromJson(data).array();
            for (auto row : rows)
                for (auto removed : row.toObject()["deleted_reports"].toArray())
                    if (m_reports.contains(removed.toString()))
                        if (!forgetReport(removed.toString())) {
                            m_catalogueLoading = false;
                            m_refreshing = false;
                            emit changed();
                            return;
                        }
            m_catalogues[id] = rows;
            m_catalogueTimes[id] = double(QDateTime::currentSecsSinceEpoch());
            if (m_contract == id)
                m_chargers = rows;
            m_catalogueLoading = false;
            m_refreshing = false;
            persist();
            status("Catálogo atualizado. Pode trabalhar sem rede.");
            if (m_contract != id)
                refreshCatalogue(m_contract);
            scheduleSync(1500);
        },
        "application/json",
        [this, id](int, QString) {
            m_catalogueLoading = false;
            m_refreshing = false;
            status("Sem atualização do catálogo; dados locais conservados.");
            if (m_contract != id)
                refreshCatalogue(m_contract);
            scheduleSync(3000);
        });
}

// Explicit refresh never replaces dirty local work. Conflicts have a separate preview/copy flow.
void Lem::refreshReport() {
    if (m_busy || m_current.isEmpty() || !online())
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
        [this](QByteArray data) {
            auto report = QJsonDocument::fromJson(data).object();
            report["dirty"] = false;
            getPhotos(report, 0);
        },
        "application/json",
        [this, id](int code, QString message) {
            m_busy = false;
            if (code == 410) {
                if (forgetReport(id))
                    status("Report apagado no servidor; cópia local removida.");
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

// Share the two-request limit with individual image downloads; do not bypass m_downloads.
void Lem::pumpPhotoBatch() {
    if (!downloadingPhotos())
        return;
    while (!m_photoBatchStopped && !m_photoBatchQueue.isEmpty() && m_downloads.size() < 2) {
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
        m_photoBatchReport.clear();
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
            m_photoBatchStopped = true;
            for (const auto &id : m_photoBatchQueue)
                m_photoBatchPending.remove(id);
            m_photoBatchQueue.clear();
        }
    }
    // Queue advancement is deferred so error callbacks cannot recurse through an entire batch.
    QTimer::singleShot(0, this, [this] { pumpPhotoBatch(); });
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
    if (m_downloads.size() >= 2) {
        status("Já existem duas fotografias a descarregar. Aguarde antes de pedir outra.");
        return;
    }
    const QString path = SafeStorage::photoPath(cacheDir(), photoId, photo["mime"].toString());
    QDir().mkpath(cacheDir() + "/photos");
    m_downloads.insert(photoId);
    emit changed();
    request(
        "GET", "/api/reports/" + reportId + "/photos/" + photoId, {},
        [this, photoId, reportId, path, photo](QByteArray bytes) {
            m_downloads.remove(photoId);
            if (bytes.size() > 20 * 1024 * 1024 ||
                (!photo["digest"].toString().isEmpty() &&
                 QString::fromLatin1(
                     QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()) !=
                     photo["digest"].toString())) {
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
            QSaveFile file(path);
            if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() ||
                !file.commit()) {
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
            if (!persist()) {
                photoDownloadFinished(reportId, photoId, false);
                return;
            }
            cleanPhotoCache();
            if (!downloadingPhotos())
                status("Fotografia disponível no dispositivo.");
            photoDownloadFinished(reportId, photoId, true);
        },
        "application/json",
        [this, photoId, reportId](int, QString) {
            m_downloads.remove(photoId);
            status("Sem ligação para descarregar a fotografia. O report continua disponível.");
            photoDownloadFinished(reportId, photoId, false);
        });
}

// Manual cleanup evicts only fully acknowledged evidence bytes. Report metadata
// and answers remain available offline and visible without a catalogue refresh.
// Protect unpublished/unsent/conflicted work; never delete gallery originals.
void Lem::clearLocalCopies() {
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
    if (m_user.isEmpty() || !m_qveCopyTarget.isEmpty() || attachingFiles() || m_syncing || !m_downloads.isEmpty())
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
        if (r["offline_photos"].toBool() || r["dirty"].toBool() ||
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
    if (switchingBlocked() || m_current.isEmpty() || !online())
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
        r["_conflict_remote"] = QJsonDocument::fromJson(data).object();
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
    if (switchingBlocked() || m_current.isEmpty() || m_user["role"].toString() != "admin" ||
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
    if (switchingBlocked() || m_current.isEmpty() || m_user["role"].toString() != "admin" ||
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
        [this](QByteArray data) {
            auto r = QJsonDocument::fromJson(data).object();
            r["dirty"] = false;
            getPhotos(r, 0);
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
    if (switchingBlocked() || m_current.isEmpty() || m_user.isEmpty())
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
    copy["id"] = id;
    copy["owner_id"] = m_user["id"];
    copy["technician"] = m_user["name"];
    copy["revision"] = 0;
    copy["status"] = "draft";
    copy["dirty"] = true;
    copy["started"] = date;
    copy["last_access"] = double(QDateTime::currentSecsSinceEpoch());
    auto payload = copy["payload"].toObject();
    payload["started"] = date;
    payload.remove("finished");
    payload["source_report_id"] = sourceId;
    copy["payload"] = payload;
    copy = Checklist::correctedDraft(copy);
    QJsonArray photos;
    QList<QPair<QString, QString>> files;
    for (auto value : source["photos"].toArray()) {
        auto p = value.toObject();
        const auto pid = QUuid::createUuid().toString(QUuid::WithoutBraces);
        auto mime = p["mime"].toString();
        if (mime.isEmpty()) mime = QFileInfo(localPath(QUrl(p["path"].toString()))).suffix().toLower() == "png" ? "image/png" : "image/jpeg";
        const auto target = SafeStorage::photoPath(cacheDir(), pid, mime);
        const auto ext = "." + QFileInfo(target).suffix();
        p["mime"] = mime;
        p["filename"] = pid + ext;
        files.append({localPath(QUrl(p["path"].toString())), target});
        p["id"] = pid;
        p["path"] = QUrl::fromLocalFile(target).toString();
        p["uploaded"] = false;
        p.remove("digest");
        photos.append(p);
    }
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
    if (switchingBlocked() || m_current.isEmpty() || !online())
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
    request("GET", "/api/reports/" + id, {}, [this](QByteArray data) {
        auto r = QJsonDocument::fromJson(data).object();
        r["dirty"] = false;
        getPhotos(r, 0);
        status(
            "Versão atual do servidor aberta. As alterações anteriores estão no novo rascunho criado como cópia.");
    });
}
