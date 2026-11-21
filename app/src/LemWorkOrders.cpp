// SPDX-License-Identifier: GPL-3.0-only
#include "Lem.h"
#include "SafeStorage.h"
#include <QDateTime>
#include <QFileInfo>
#include <QJsonDocument>
#include <QUuid>

QString Lem::newOrderId() const {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QJsonArray Lem::retainedOrders(QJsonArray orders) {
    const auto now = QDateTime::currentDateTimeUtc();
    const qint64 retentionMs = 24LL * 60 * 60 * 1000;
    qint64 nextExpiry = 0;
    for (auto i = orders.size(); i > 0; --i) {
        const auto cancelled = QDateTime::fromString(orders.at(i - 1).toObject()["cancelled_at"].toString(), Qt::ISODate);
        if (!cancelled.isValid()) continue;
        const auto remaining = now.msecsTo(cancelled.addMSecs(retentionMs));
        if (remaining <= 0) orders.removeAt(i - 1);
        else if (!nextExpiry || remaining < nextExpiry) nextExpiry = remaining;
    }
    m_orderExpiry.stop();
    if (nextExpiry) m_orderExpiry.start(int(qMin(nextExpiry, retentionMs)));
    return orders;
}

void Lem::expireCachedOrders() {
    const auto kept = retainedOrders(m_orders);
    if (kept.size() == m_orders.size()) return;
    m_orders = kept;
    emit workOrderListChanged();
    emit changed();
    if (!m_localReadError) persist();
}

void Lem::resetOrders() {
    m_orderExpiry.stop();
    ++m_orderRead;
    ++m_candidateRead;
    m_orders = {};
    m_orderCandidates = {};
    m_orderMessage.clear();
    m_orderReadError.clear();
    m_planningDay.clear();
    m_followedReports.clear();
    m_attemptedDownloads.clear();
    m_orderDownload = false;
    m_savingOrder = false;
    emit workOrderListChanged();
    emit workCandidatesChanged();
    emit workOrdersChanged();
}

void Lem::refreshOrders(bool planner) {
    if (!authenticated()) return;
    const auto token = m_token;
    const auto read = ++m_orderRead;
    request("GET", "/api/work-orders?include_cancelled=true", {}, [this, token, read](QByteArray bytes) {
        if (token != m_token || read != m_orderRead) return;
        const auto data = QJsonDocument::fromJson(bytes).object();
        if (!data["orders"].isArray() || !data["today"].isString()) return;
        m_orderReadError.clear();
        const auto orders = retainedOrders(data["orders"].toArray());
        const auto day = data["today"].toString();
        if (m_orders != orders || day != m_planningDay) {
            m_orders = orders;
            for (auto it = m_reports.constBegin(); it != m_reports.constEnd(); ++it)
                if (it.value().toObject()["_conflict_kind"] == "order_scope") m_syncConflicts.remove(it.key());
            scheduleSync(1500);
            m_planningDay = day;
            emit workOrderListChanged();
            emit changed();
            persist();
            if (m_user["role"] == "tech" && !m_refreshing) refresh();
        }
        emit workOrdersChanged();
#ifndef Q_OS_ANDROID
        m_followedReports.clear();
        if (m_user["role"] == "admin") {
            for (const auto &value : m_orders) {
                const auto order = value.toObject();
                if (!order["following"].toBool()) continue;
                for (const auto &entry : order["items"].toArray()) {
                    const auto row = entry.toObject();
                    const auto closed = row["report_id"].toString();
                    if (!closed.isEmpty() && row["available"].toBool()) m_followedReports.insert(closed, {-1, -1});
                    for (const auto &draft : row["drafts"].toArray()) {
                        const auto summary = draft.toObject();
                        m_followedReports.insert(summary["id"].toString(), {summary["revision"].toInt(), summary["photo_count"].toInt(-1)});
                    }
                }
            }
        }
        m_attemptedDownloads.clear();
        receiveFollowedReports();
#endif
    }, "application/json", [this, token, read](int code, QString) {
        if (token != m_token || read != m_orderRead) return;
        m_orderReadError = code == 404 ? "Atualize o servidor para utilizar ordens de trabalho."
                                     : "Não foi possível atualizar as ordens de trabalho.";
        emit workOrdersChanged();
    });
    if (!planner || m_user["role"] != "admin") return;
    loadUsers();
    const auto candidateRead = ++m_candidateRead;
    request("GET", "/api/work-order-candidates", {}, [this, token, candidateRead](QByteArray bytes) {
        if (token != m_token || candidateRead != m_candidateRead) return;
        const auto data = QJsonDocument::fromJson(bytes);
        if (data.isArray() && m_orderCandidates != data.array()) {
            m_orderCandidates = data.array();
            emit workCandidatesChanged();
        }
    }, "application/json", [this, token, candidateRead](int, QString) {
        if (token != m_token || candidateRead != m_candidateRead) return;
        m_orderMessage = "Não foi possível atualizar os IDs disponíveis.";
        emit workOrdersChanged();
    });
}

void Lem::writeOrder(QString id, QString path, QJsonObject body) {
    if (!authenticated() || m_savingOrder) return;
    m_savingOrder = true;
    m_orderMessage.clear();
    emit workOrdersChanged();
    const auto token = m_token;
    request("PUT", "/api/work-orders/" + id + path, QJsonDocument(body).toJson(QJsonDocument::Compact),
        [this, token, id](QByteArray) {
            if (token != m_token) return;
            m_savingOrder = false;
            m_orderMessage = "Alteração guardada.";
            emit workOrderSaved(id);
            emit workOrdersChanged();
            refresh();
            refreshOrders();
        }, "application/json", [this, token](int code, QString error) {
            if (token != m_token) return;
            m_savingOrder = false;
            const auto detail = QJsonDocument::fromJson(error.toUtf8()).object()["detail"].toString();
            m_orderMessage = detail.isEmpty() ? "Não foi possível guardar. Verifique os dados e a ligação." : detail;
            emit workOrdersChanged();
            if (code == 409) refreshOrders(true);
        });
}

void Lem::saveOrder(QVariantMap plan) {
    writeOrder(plan.value("id").toString(), {}, QJsonObject::fromVariantMap(plan));
}
void Lem::cancelOrder(QString id, int revision) {
    writeOrder(id, "/cancel", {{"revision", revision}});
}

void Lem::followOrder(QString id, bool follow) {
    writeOrder(id, "/follow", {{"following", follow}});
}
void Lem::invalidateOrderItem(QString id, QString item, int revision, QString note) {
    writeOrder(id, "/items/" + item + "/invalid", {{"revision", revision}, {"note", note}});
}

bool Lem::orderAllowsDraft(QString target) const {
    for (const auto &value : m_orders) {
        const auto order = value.toObject();
        if (!order["started"].toBool() || !order["completed"].toString().isEmpty()) continue;
        for (const auto &entry : order["items"].toArray()) {
            const auto item = entry.toObject();
            if (item["target"] == target && item["state"] == "pending" && item["available"].toBool())
                return true;
        }
    }
    return false;
}

bool Lem::isOrderRecovery(const QJsonObject &report) const {
    if (report["status"] != "draft" && !report["dirty"].toBool()) return false;
    if (report["work_item_expired"].toBool() || report["work_item_recovery"].toBool()) return true;
    const auto target = report["charger_id"].toString();
    for (const auto &value : m_orders) {
        for (const auto &entry : value.toObject()["items"].toArray()) {
            const auto item = entry.toObject();
            if (item["target"] != target || item["state"] == "pending") continue;
            if (report["_order_item"] == item["id"] && report["id"] != item["report_id"]) return true;
            for (const auto &draft : item["drafts"].toArray())
                if (draft.toObject()["id"] == report["id"]) return true;
        }
    }
    return false;
}

// Prepare the same report editor as the contract catalogue, including local unsent drafts.
// An empty return starts a draft; "!" means that navigation must stay in the order.
QString Lem::prepareOrderReport(QString orderId, QString itemId, QString requestedReport) {
    if (m_busy || attachingFiles() || !m_qveCopyTarget.isEmpty()) return "!";
    for (const auto &value : m_orders) {
        const auto order = value.toObject();
        if (order["id"] != orderId) continue;
        for (const auto &entry : order["items"].toArray()) {
            const auto item = entry.toObject();
            if (item["id"] != itemId || item["state"] == "cancelled") continue;
            if (requestedReport.isEmpty() && (!item["available"].toBool() || item["state"] == "invalid")) continue;
            const auto cid = item["contract_id"].toString(), target = item["target"].toString();
            bool known = false;
            for (const auto &c : m_contracts) if (c.toObject()["id"] == cid) known = true;
            if (!known) { refresh(); return "!"; }
            const auto drafts = item["drafts"].toArray();
            if (!requestedReport.isEmpty()) {
                bool offered = false;
                for (const auto &draft : drafts)
                    if (draft.toObject()["id"] == requestedReport) offered = true;
                const auto local = m_reports.value(requestedReport).toObject();
                const bool ownLocal = local["owner_id"] == m_user["id"] && local["dirty"].toBool() &&
                    local["charger_id"] == target && local["_order_item"] == itemId;
                if (!offered && !ownLocal) return "!";
            } else if (item["state"] == "pending" && !order["started"].toBool()) return "!";
            selectContract(cid);
            if (m_contract != cid) return "!";
            bool listed = false;
            for (const auto &row : m_chargers) if (row.toObject()["id"] == target) listed = true;
            if (!listed) m_chargers.append(QJsonObject{{"id",target},{"contract_id",cid},
                {"station_id",item["station_id"]},{"external_id",item["external_id"]},{"details",item["details"]},
                {"contract_notes",item["contract_notes"]}, {"notes_revision",item["notes_revision"]}});
            if (!requestedReport.isEmpty()) return requestedReport;
            if (!item["report_id"].toString().isEmpty()) return item["report_id"].toString();
            if (!drafts.isEmpty()) {
                for (const auto &draft : drafts)
                    if (draft.toObject()["owner_id"] == m_user["id"]) return draft.toObject()["id"].toString();
                return drafts.first().toObject()["id"].toString();
            }
            for (auto it = m_reports.begin(); it != m_reports.end(); ++it) {
                const auto r = it.value().toObject();
                if (r["_order_item"] == itemId && r["charger_id"] == target && r["owner_id"] == m_user["id"] &&
                    (r["status"] == "draft" || r["dirty"].toBool())) return it.key();
            }
            return {};
        }
    }
    return "!";
}

void Lem::receiveFollowedReports() {
    if (m_orderDownload || !authenticated() || m_user["role"] != "admin" || m_localReadError ||
        localSaveError() || downloadingPhotos() || m_syncing || saving() || m_busy || !m_downloads.isEmpty()) return;
    for (auto it = m_followedReports.cbegin(); it != m_followedReports.cend(); ++it) {
        const auto rid = it.key();
        if (!SafeStorage::component(rid)) continue;
        auto cached = m_reports.value(rid).toObject();
        if (cached["dirty"].toBool() || cached["_conflict"].toBool() || m_pendingEdits.contains(rid)) continue;
        const auto expected = it.value();
        const bool latest = !cached.isEmpty() && (expected.revision < 0 ? cached["status"] == "closed" :
            cached["revision"].toInt() >= expected.revision &&
            (expected.photoCount < 0 || cached["photos"].toArray().size() == expected.photoCount));
        if (!latest) {
            if (m_attemptedDownloads.contains(rid)) continue;
            m_attemptedDownloads.insert(rid);
            m_orderDownload = true;
            const auto token = m_token;
            request("GET", "/api/reports/" + rid, {}, [this, token, rid](QByteArray bytes) {
                if (token != m_token) return;
                m_orderDownload = false;
                auto r = QJsonDocument::fromJson(bytes).object();
                const auto existing = m_reports.value(rid).toObject();
                if (m_followedReports.contains(rid) && validRemoteReport(r, rid) &&
                    !existing["dirty"].toBool() && !existing["_conflict"].toBool() && !m_pendingEdits.contains(rid) &&
                    r["revision"].toInt() >= existing["revision"].toInt()) {
                    r["offline_photos"] = true;
                    r["dirty"] = false;
                    const auto received = withContractPresentation(Checklist::correctedDraft(receivedPhotoPaths(r)));
                    m_reports[rid] = received;
                    if (m_current.value("id") == rid) {
                        m_current = received;
                        m_template = received["template"].toObject();
                    }
                    persist();
                    emit changed();
                }
                QTimer::singleShot(0, this, &Lem::receiveFollowedReports);
            }, "application/json", [this, token](int, QString) {
                if (token != m_token) return;
                m_orderDownload = false;
                QTimer::singleShot(0, this, &Lem::receiveFollowedReports);
            });
            return;
        }
        if (!cached["offline_photos"].toBool()) {
            cached["offline_photos"] = true;
            m_reports[rid] = cached;
            if (m_current.value("id") == rid) m_current["offline_photos"] = true;
            if (!persist()) return;
        }
        for (const auto &value : cached["photos"].toArray()) {
            const auto photo = value.toObject();
            const auto pid = photo["id"].toString();
            if (!photo["uploaded"].toBool() || m_attemptedDownloads.contains(pid) ||
                QFileInfo(QUrl(photo["path"].toString()).toLocalFile()).size() > 0) continue;
            m_attemptedDownloads.insert(pid);
            fetchPhotoForReport(rid, pid);
            return;
        }
    }
}
