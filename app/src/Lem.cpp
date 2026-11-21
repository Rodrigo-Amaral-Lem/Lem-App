// SPDX-License-Identifier: GPL-3.0-only
// Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

// QML-facing application controller: identity, local reports, user actions and HTTP.
// The same Lem object is implemented across this file and LemOffline.cpp.
// State belongs to the QObject thread; callbacks explicitly reconcile snapshots.
// Start reading at docs/architecture.md for state, persistence and source ownership.

#include "Lem.h"
#include "NetworkSecurity.h"
#include "SafeStorage.h"
#include "SessionStore.h"
#include "EvidenceMedia.h"
#include <cmath>
#include <utility>
#include "Measurements.h"
#include "ReportPdf.h"
#include "ReportBundle.h"
#include <QTemporaryDir>
#include "ReportNaming.h"
#include <QVersionNumber>
#include <QDesktopServices>
#include <QRegularExpression>
#include <QtConcurrent/QtConcurrentRun>
#include <QFile>
#include <QSaveFile>
#include <QDir>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QCryptographicHash>
#include <QImageReader>
#include <QImage>
#include <QTransform>
#include <QBuffer>
#include <QUuid>
#include <QDateTime>
#include <QFileInfo>
#include <QSettings>
#include <QSslSocket>
#include <QTimer>
#include <QNetworkInformation>
#include <QGuiApplication>
#include <QHash>
#ifdef Q_OS_ANDROID
#include <QGuiApplication>
#include <QJniObject>
#include <QJniEnvironment>
#include <QPointer>
#include <QtCore/qcoreapplication_platform.h>
#endif
namespace {
QByteArray json(QJsonObject o) {
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}
// Journals/checkpoints contain complete report snapshots. A valid JSON object can
// still be incomplete; replaying it must not discard answers or evidence metadata.
bool readableReport(const QJsonObject &report) {
    return !report.value("id").toString().isEmpty() &&
           report.value("payload").isObject() &&
           report.value("payload").toObject().value("answers").isObject() &&
           report.value("photos").isArray() && report.value("template").isObject() &&
           report.value("snapshot").isObject();
}
// A malformed 0.5.3 refresh could persist only display/cache defaults under an
// empty key. Recover that exact shape and keep the guard for any real data.
bool emptyRefreshPlaceholder(QJsonObject entry) {
    if (entry["last_access"].toDouble() <= 0 || !entry.value("photos").isArray() ||
        !entry.value("photos").toArray().isEmpty() || entry.value("dirty").toBool()) return false;
    for (const auto key : {"last_access", "photos"}) entry.remove(key);
    for (const auto key : {"offline_photos", "dirty"}) {
        if (entry.contains(key) && entry.value(key) != QJsonValue(false)) return false;
        entry.remove(key);
    }
    for (const auto key : {"id", "status", "template", "snapshot", "charger_id", "contract_id", "presentation"}) {
        if (entry.contains(key) && !entry.value(key).isNull()) return false;
        entry.remove(key);
    }
    return entry.isEmpty();
}
QString uuid() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
QString sessionDigest(const QString &token) {
    return QString::fromLatin1(QCryptographicHash::hash(token.toUtf8(), QCryptographicHash::Sha256).toHex());
}
const QString sessionEnded = QStringLiteral(
    "Sessão expirada ou terminada. Entre novamente. Os rascunhos locais foram conservados.");
} // namespace

// Connect independent notification channels before loading cached data.
// The per-report writer has one worker so older writes cannot finish after newer ones.
Lem::Lem(QString root, QObject *parent) : QObject(parent) {
    m_operationNotify.setSingleShot(true);
    m_operationNotify.setInterval(100);
    connect(&m_operationNotify, &QTimer::timeout, this, &Lem::operationChanged);
    connect(this, &Lem::changed, this, &Lem::operationChanged);
    connect(this, &Lem::localSaveStateChanged, this, &Lem::operationChanged);
    m_orderExpiry.setSingleShot(true);
    m_orderExpiry.setTimerType(Qt::PreciseTimer);
    connect(&m_orderExpiry, &QTimer::timeout, this, &Lem::expireCachedOrders);
    auto *orderTimer = new QTimer(this);
    orderTimer->setInterval(60000);
    connect(orderTimer, &QTimer::timeout, this, [this] {
        if (authenticated() && !m_savingOrder) refreshOrders();
    });
    orderTimer->start();
    m_editPool.setMaxThreadCount(1);
    m_exportPool.setMaxThreadCount(1);
    m_editTimer.setSingleShot(true);
    connect(&m_editTimer, &QTimer::timeout, this, &Lem::writeNextEdits);
    connect(&m_editWatcher, &QFutureWatcher<bool>::finished, this, &Lem::completeEditWrite);
    connect(this, &Lem::changed, this, [this] {
        if (m_notifiedCurrent != m_current) {
            m_notifiedCurrent = m_current;
            emit currentChanged();
        }
        const auto photos = m_current.value("photos").toArray();
        if (m_notifiedPhotos != photos) {
            m_notifiedPhotos = photos;
            emit photosChanged();
        }
        // Status/progress changes must not refilter/rebuild the QML catalogue.
        // changed() also fires for every typed character: compare the small inputs
        // first, and convert/compare the complete catalogue only when they differ.
        QJsonArray reportSummaries;
        QVariantMap localDrafts;
        for (auto it = m_reports.constBegin(); it != m_reports.constEnd(); ++it) {
            const auto r = it.value().toObject();
            if (r["owner_id"] == m_user["id"] && r["dirty"].toBool() && r["correction"].toObject().isEmpty()) {
                localDrafts.insert(r["_order_item"].toString(r["charger_id"].toString()), QVariantMap{{"id", it.key()},
                    {"item", r["_order_item"].toString()}, {"conflict", r["_conflict"].toBool()}, {"closed", r["status"] == "closed"}});
            }
            const auto snapshot = r["snapshot"].toObject();
            reportSummaries.append(
                QJsonArray{r["id"], r["charger_id"], r["status"], r["owner_id"], r["dirty"], r["revision"],
                           r["finished"], r["payload"].toObject()["finished"],
                           snapshot["charger"].toObject()["external_id"], snapshot["contract"].toObject()["name"]});
        }
        if (localDrafts != m_localOrderDrafts) {
            m_localOrderDrafts = localDrafts;
            emit localOrderDraftsChanged();
        }
        if (!m_rowsBuilt || m_rowsChargers != m_chargers || m_rowsReports != reportSummaries) {
            m_rowsBuilt = true;
            m_rowsChargers = m_chargers;
            m_rowsReports = reportSummaries;
            const auto rows = buildChargerRows();
            if (m_chargerRows != rows) {
                m_chargerRows = rows;
                emit chargersChanged();
            }
        }
        if (m_notifiedStations != m_stations) {
            m_notifiedStations = m_stations;
            emit stationsChanged();
        }
        if (m_notifiedTemplate != m_template) {
            m_notifiedTemplate = m_template;
            emit sectionsChanged();
        }
        if (m_notifiedContracts != m_contracts) {
            m_notifiedContracts = m_contracts;
            emit contractsChanged();
        }
    });
    m_root =
        root.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) : root;
    QDir().mkpath(m_root);
    QFile schema(":/shared/checklist-v1.json");
    if (!schema.open(QIODevice::ReadOnly))
        qFatal("Checklist resource missing");
    m_template = QJsonDocument::fromJson(schema.readAll()).object();
#ifdef Q_OS_ANDROID
    auto cameraPoll = new QTimer(this);
    cameraPoll->setInterval(1000);
    connect(cameraPoll, &QTimer::timeout, this, [this]() {
        if (qGuiApp->applicationState() == Qt::ApplicationActive)
            checkCameraResult();
    });
    cameraPoll->start();
#endif
    m_autoSync.setSingleShot(true);
    connect(&m_autoSync, &QTimer::timeout, this, [this]() { startSync(false); });
    m_sessionRetry.setSingleShot(true);
    connect(&m_sessionRetry, &QTimer::timeout, this, [this] {
        if (sessionPending()) refresh();
    });
    if (QNetworkInformation::loadDefaultBackend()) {
        connect(QNetworkInformation::instance(), &QNetworkInformation::reachabilityChanged, this,
                [this](QNetworkInformation::Reachability r) {
                    if (r == QNetworkInformation::Reachability::Online) {
                        m_retryNotBefore = 0;
                        if (sessionPending()) m_sessionRetry.start(0);
                        scheduleSync(1500);
                    }
                });
    }
    if (qGuiApp)
        connect(qGuiApp, &QGuiApplication::applicationStateChanged, this,
                [this](Qt::ApplicationState state) {
                    if (state == Qt::ApplicationActive) {
                        expireCachedOrders();
                        if (sessionPending()) m_sessionRetry.start(0);
                        scheduleSync(1500);
#ifdef Q_OS_ANDROID
                        // CaptureActivity publishes the result before returning to Qt.
                        // Consume it now; the existing poll remains the busy/recovery fallback.
                        checkCameraResult();
#endif
                    } else
                        flushEdits();
                });
    QSettings settings;
    m_server = ServerSettings::load(settings);
    QFile identityFile(m_root + "/last-user.json");
    QJsonObject identity;
    if (identityFile.open(QIODevice::ReadOnly)) {
        identity = QJsonDocument::fromJson(identityFile.readAll()).object();
        if (identity["server"].toString() == m_server)
            m_user = identity["user"].toObject();
    }
    if (!m_user.isEmpty()) {
        const auto digest = identity["session_digest"].toString();
        if (!digest.isEmpty()) {
            const auto record = QJsonDocument::fromJson(SessionStore::load(m_root)).object();
            const auto token = record["token"].toString();
            if (!token.isEmpty() && record["server"].toString() == m_server &&
                record["user_id"] == m_user["id"] && sessionDigest(token) == digest) {
                m_pendingSessionToken = token;
                m_sessionPersisted = true;
                m_sessionRetry.start(0);
            }
        }
        loadUser();
        if (!m_localReadError)
            m_message = sessionPending() ? "A verificar a sessão guardada…"
                                        : "Dados locais disponíveis. Entre para sincronizar.";
    }
}
Lem::~Lem() {
    flushEdits();
    m_editPool.waitForDone();
    m_exportPool.waitForDone();
}

// Changing the server also changes the storage namespace and authentication scope.
// Flush pending edits before clearing the in-memory identity and report state.
void Lem::useDefaultServer() {
    if (switchingBlocked())
        return;
    setServer(ServerSettings::defaultUrl());
    QSettings settings;
    ServerSettings::save(settings, m_server);
}

void Lem::setServer(QString s) {
    if (switchingBlocked())
        return;
    s = s.trimmed();
    while (s.endsWith('/'))
        s.chop(1);
    QUrl u(s);
    if (u.scheme() != "https" &&
        !(u.scheme() == "http" && (u.host() == "localhost" || u.host() == "127.0.0.1"))) {
        status("Use HTTPS; HTTP permitido apenas em localhost para desenvolvimento.");
        return;
    }
    if (s == m_server)
        return;
    if (!flushEdits())
        return;
    clearSession();
    m_server = s;
    resetStationCatalogue();
    m_importInfo = {};
    m_preview = {};
    m_excel.clear();
    m_reportPresentationSave = {};
    m_clientCompatibility = {};
    emit compatibilityChanged();
    m_users = {};
    m_deletedContracts = {};
    emit contractTrashChanged();
    m_user = {};
    m_contracts = {};
    m_chargers = {};
    m_reports = {};
    m_current = {};
    m_contract.clear();
    m_deletedReports.clear();
    QSettings settings;
    ServerSettings::save(settings, s);
    emit changed();
}

// Separate cached data by server hash AND user ID; contract IDs alone are insufficient.
QString Lem::cacheDir() const {
    auto scope = QString::fromLatin1(
        QCryptographicHash::hash(m_server.toUtf8(), QCryptographicHash::Sha256).toHex().left(16));
    return m_root + "/" + scope + "/" + SafeStorage::userDirectory(m_user["id"].toString());
}

// Restore the full checkpoint, then replay only newer journals owned by this user.
// Deleted report IDs prevent an older journal from resurrecting a deleted report.
void Lem::loadUser() {
    resetOrders();
    resetStationCatalogue();
    m_manualSyncRequested = false;
    m_localReadError = false;
    m_localSaveError = false;
    m_autoSync.stop();
    m_syncConflicts.clear();
    m_syncIds.clear();
    m_manualRestoredCorrections.clear();
    m_retryNotBefore = 0;
    m_catalogueTimes = {};
    m_deletedReports.clear();
    m_contracts = {};
    m_chargers = {};
    m_reports = {};
    m_current = {};
    m_catalogues = {};
    m_historicalNotes = {};
    m_contract.clear();
    if (!QDir().mkpath(cacheDir())) m_localReadError = true;
    QFile f(cacheDir() + "/cache.json");
    if (f.open(QIODevice::ReadOnly)) {
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(f.readAll(), &parseError);
        auto o = document.object();
        bool valid = f.error() == QFile::NoError && parseError.error == QJsonParseError::NoError && document.isObject() &&
                     o.value("reports").isObject();
        for (const auto &key : {"contracts", "chargers", "deleted_reports"})
            if (o.contains(key) && !o.value(key).isArray()) valid = false;
        for (const auto &key : {"catalogues", "catalogue_times", "historical_notes"})
            if (o.contains(key) && !o.value(key).isObject()) valid = false;
        if (o.contains("contract") && !o.value("contract").isString()) valid = false;
        if (!valid) { m_localReadError = true; o = {}; }
        m_orders = o["work_orders"].toArray();
        m_planningDay = o["planning_day"].toString();
        emit workOrderListChanged();
        m_contracts = o["contracts"].toArray();
        m_chargers = currentPcveRows(o["chargers"].toArray());
        m_reports = o["reports"].toObject();
        m_contract = o["contract"].toString();
        // Startup opens the preventive catalogue. Restore intervention scopes when
        // their own menu or work order is opened, including offline.
        for (const auto &entry : m_contracts) {
            if (entry.toObject()["id"] == m_contract && entry.toObject().contains("intervention")) {
                m_contract.clear();
                m_chargers = {};
                break;
            }
        }
        m_catalogues = o["catalogues"].toObject();
        m_historicalNotes = o["historical_notes"].toObject();
        for (const auto &key : m_catalogues.keys())
            m_catalogues[key] = currentPcveRows(m_catalogues.value(key).toArray());
        m_catalogueTimes = o["catalogue_times"].toObject();
        const auto stationCache = o["station_catalogue"].toObject();
        if (stationCache["role"] == m_user["role"] && stationCache["rows"].isArray()) {
            m_stations = currentPcveRows(stationCache["rows"].toArray());
            m_stationCatalogueLoaded = true;
        }
        for (auto id : o["deleted_reports"].toArray())
            m_deletedReports.insert(id.toString());
        // Earlier versions could store null/ID-less placeholders (see releasePhotoSync).
        // A real report is always an object stored under its own ID.
        for (const auto &key : m_reports.keys())
            // Only discard empty placeholders; retain non-empty damaged records
            // for recovery instead of silently deleting potentially unsent work.
            if (m_reports.value(key).isNull() ||
                (m_reports.value(key).isObject() && (m_reports.value(key).toObject().isEmpty() ||
                 (key.isEmpty() && emptyRefreshPlaceholder(m_reports.value(key).toObject())))))
                m_reports.remove(key);
            else if (!m_reports.value(key).isObject() ||
                     m_reports.value(key).toObject().value("id").toString() != key ||
                     !readableReport(m_reports.value(key).toObject()))
                m_localReadError = true;
    } else if (QFileInfo::exists(f.fileName())) {
        m_localReadError = true;
    }
    // Small per-report edit journals are newer than an older full-cache checkpoint.
    const QDir edits(cacheDir() + "/edits");
    const QFileInfo editFolder(edits.path());
    if (editFolder.exists() && (!editFolder.isDir() || !edits.isReadable())) m_localReadError = true;
    for (const auto &name : edits.entryList({"*.json"}, QDir::Files | QDir::NoSymLinks)) {
        QFile file(edits.filePath(name));
        if (!file.open(QIODevice::ReadOnly)) {
            m_localReadError = true;
            continue;
        }
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
        const auto report = document.object();
        const auto id = report["id"].toString();
        if (file.error() != QFile::NoError || parseError.error != QJsonParseError::NoError || !document.isObject() || !readableReport(report)) {
            m_localReadError = true;
            continue;
        }
        if (!id.isEmpty() && !m_deletedReports.contains(id) && report["owner_id"] == m_user["id"] &&
            report["_edit_seq"].toDouble() > m_reports[id].toObject()["_edit_seq"].toDouble())
            m_reports[id] = report;
    }
    // Recover validated files committed between batch metadata checkpoints.
    // A .part/QSaveFile temporary is never considered complete evidence.
    for (const auto &id : m_reports.keys()) {
        auto report = m_reports[id].toObject();
        recoverOrderStartConflict(report);
        if (report["_conflict"].toBool() && !report.contains("_conflict_kind") &&
            !report["_order_item"].toString().isEmpty() &&
            report["_conflict_reason"] == "Contrato ou tarefa não atribuídos para esta operação.")
            report["_conflict_kind"] = "order_scope";
        auto photos = report["photos"].toArray();
        for (int i = 0; i < photos.size(); ++i) {
            auto photo = photos[i].toObject();
            if (!photo["uploaded"].toBool() || !photo["path"].toString().isEmpty()) continue;
            const auto path = SafeStorage::photoPath(cacheDir(), photo["id"].toString(), photo["mime"].toString());
            const QFileInfo file(path); const auto expected = qint64(photo["size"].toDouble());
            if (!path.isEmpty() && file.isFile() && !file.isSymLink() && file.size() > 0 && (!expected || expected == file.size())) {
                photo["path"] = QUrl::fromLocalFile(path).toString(); photos[i] = photo;
            }
        }
        report["photos"] = photos; m_reports[id] = report;
    }
    applyContractPresentations();
    // The checkpoint and its edit journals are fully restored before a purge writes them.
    expireCachedOrders();
    if (m_localReadError) storageWritable();
    else {
        emit localSaveStateChanged();
        cleanPhotoCache();
    }
    emit changed();
}

// Checkpoint all local state atomically after flushing small per-report journals.
// Remove journals only after the full checkpoint has committed successfully.
bool Lem::persist() {
    if (m_user.isEmpty() || !flushEdits())
        return false;
    const auto error = writeCheckpoint(m_reports);
    if (!error.isEmpty()) status(error);
    return error.isEmpty();
}

// The caller has flushed pending edits. A failed write leaves the old cache and
// all journals available for recovery. Publish the new snapshot atomically.
QString Lem::writeCheckpoint(const QJsonObject &reports) {
    QSaveFile f(cacheDir() + "/cache.json");
    if (!f.open(QIODevice::WriteOnly))
        return "Não foi possível abrir a cache local para gravação: " + f.errorString();
    QJsonArray removed;
    for (const auto &id : m_deletedReports)
        removed.append(id);
    QJsonObject stationCache;
    if (m_stationCatalogueLoaded)
        stationCache = {{"role", m_user["role"]}, {"rows", m_stations}};
    const auto bytes = json({{"deleted_reports", removed},
                             {"contracts", m_contracts},
                             {"chargers", m_chargers},
                             {"reports", reports},
                             {"contract", m_contract},
                             {"catalogues", m_catalogues},
                             {"work_orders", m_orders}, {"planning_day", m_planningDay},
                             {"historical_notes", m_historicalNotes},
                             {"catalogue_times", m_catalogueTimes},
                             {"station_catalogue", stationCache}});
    if (f.write(bytes) != bytes.size() || !f.commit())
        return "Não foi possível gravar a cache local: " + f.errorString();
    for (auto it = reports.constBegin(); it != reports.constEnd(); ++it)
        QFile::remove(editPath(it.key()));
    return {};
}

// Keep the proposed report out of the editor, catalogue and sync queue until it
// is durable. Flushing first protects unrelated edits even if this write fails.
bool Lem::checkpointReport(const QJsonObject &report, const QString &failureMessage) {
    if (!flushEdits()) return false;
    auto reports = m_reports;
    reports.insert(report.value("id").toString(), report);
    const auto error = writeCheckpoint(reports);
    if (!error.isEmpty()) {
        qWarning().noquote() << error;
        status(failureMessage);
        return false;
    }
    m_reports = reports;
    return true;
}

// Mirror the active report into the report collection and request a full checkpoint.
void Lem::remember() {
    if (!m_current.isEmpty()) {
        m_reports.insert(m_current.value("id").toString(), m_current);
        persist();
    }
    emit changed();
}
void Lem::status(QString s) {
    m_message = s;
    emit changed();
}

// Check for a session token, not actual network reachability. HTTP can still fail.
bool Lem::online() {
    if (m_token.isEmpty()) {
        if (sessionPending()) {
            m_sessionRetry.start(0);
            status("Sessão guardada. A aguardar validação no servidor.");
        } else {
            status("Entre com email e password para comunicar com o servidor. Os rascunhos locais foram conservados.");
        }
        return false;
    }
    return true;
}

// Send JSON or another single-body HTTP request and track its operation progress.
// A caller-supplied failure handler owns recovery; otherwise release the busy UI state.
QNetworkReply *Lem::request(QString method, QString path, QByteArray body, Callback cb, QString content,
                  Failure error, bool restoring) {
    QNetworkRequest req(QUrl(m_server + path));
    NetworkSecurity::restrictRedirects(req);
    req.setRawHeader("X-Lem-Client-Version", QCoreApplication::applicationVersion().toUtf8());
    req.setRawHeader("X-Lem-Client-Platform", NetworkSecurity::clientPlatform());
    req.setRawHeader("X-Lem-Work-Orders", "1");
    // The server sends nothing while it renders a PDF/ZIP with many photographs.
    const bool exporting = path.endsWith("/pdf") || path.endsWith("/bundle");
    req.setTransferTimeout(restoring ? 8000 : exporting ? 180000 : 30000);
    req.setHeader(QNetworkRequest::ContentTypeHeader, content);
    const auto requestToken = restoring ? m_pendingSessionToken : m_token;
    const bool sentBearer = !requestToken.isEmpty() &&
                            path != "/api/client-compatibility" && path != "/api/login";
    if (sentBearer)
        req.setRawHeader("Authorization", "Bearer " + requestToken.toUtf8());
    const auto requestServer = m_server;
    auto reply = net.sendCustomRequest(req, method.toUtf8(), body);
    auto responseBody = NetworkSecurity::boundReply(reply, NetworkSecurity::responseLimit(path));
    trackReply(reply, method, path, !body.isEmpty());
    connect(reply, &QNetworkReply::finished, this, [this, reply, cb, error, requestToken, requestServer,
            responseBody, restoring, sentBearer]() {
        responseBody->consume(reply);
        auto data = responseBody->data;
        auto code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (responseBody->exceeded || reply->error() != QNetworkReply::NoError) {
            // Logout can complete locally before its response. Do not revoke a
            // newer session or another server's policy from an obsolete reply.
            const bool sameSession = m_server == requestServer &&
                (restoring ? m_pendingSessionToken : m_token) == requestToken;
            if (code == 426 && sameSession)
                rejectIncompatibleReply(data);
            if (code == 401 && sameSession && sentBearer) {
                clearSession();
                status(sessionEnded);
            }
            QString detail = responseBody->exceeded ? QStringLiteral("Resposta do servidor demasiado grande.") : QString::fromUtf8(data);
            if (detail.isEmpty())
                detail = reply->errorString();
            if (error)
                error(code, detail);
            else {
                m_busy = false;
                status(
                    code == 409
                        ? "Conflito: o servidor tem outra revisão. O rascunho local foi conservado. " +
                              detail
                        : "Não foi possível concluir: " + detail);
            }
        } else
            cb(data);
        reply->deleteLater();
    });
    return reply;
}

// Evidence bypasses the in-memory response accumulator used for JSON/exports.
void Lem::requestPhoto(QString path, QString destination, qint64 limit,
                       std::function<void(QSharedPointer<NetworkSecurity::FileBody>)> cb, Failure error,
                       QString digest, QSharedPointer<NetworkSecurity::FileBody> body,
                       int attempt, std::function<bool()> retryAllowed, bool contractExport) {
    if (!body) body = QSharedPointer<NetworkSecurity::FileBody>::create(destination, limit);
    QNetworkRequest req(QUrl(m_server + path));
    NetworkSecurity::restrictRedirects(req);
    req.setRawHeader("X-Lem-Client-Version", QCoreApplication::applicationVersion().toUtf8());
    req.setRawHeader("X-Lem-Client-Platform", NetworkSecurity::clientPlatform());
    req.setTransferTimeout(path.endsWith("/bundle") || path.endsWith("/pdf") ? 300000 : 60000);
    req.setRawHeader("Accept-Encoding", "identity");
    req.setRawHeader("Authorization", "Bearer " + m_token.toUtf8());
    const auto offset = digest.isEmpty() ? 0 : body->received;
    if (offset > 0) {
        req.setRawHeader("Range", "bytes=" + QByteArray::number(offset) + "-");
        req.setRawHeader("If-Range", "\"" + digest.toUtf8() + "\"");
    }
    body->beginAttempt(offset);
    const auto requestToken = m_token, requestServer = m_server;
    auto reply = net.get(req);
    if (contractExport)
        connect(this, &Lem::contractExportCancelled, reply, &QNetworkReply::abort);
    reply->setReadBufferSize(1024 * 1024);
    connect(reply, &QIODevice::readyRead, reply, [reply, body] { body->consume(reply); });
    trackReply(reply, "GET", path, false);
    connect(reply, &QNetworkReply::finished, this, [this, reply, body, cb, error, requestToken, requestServer,
            path, destination, limit, digest, attempt, retryAllowed, contractExport] {
        body->consume(reply);
        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool sameSession = m_server == requestServer && m_token == requestToken;
        if (!sameSession) {
            error(401, "A sessão mudou durante a transferência."); reply->deleteLater(); return;
        }
        if (code == 426) rejectIncompatibleReply(body->errorData);
        if (code == 401) {
            clearSession();
            status(sessionEnded);
        }
        const bool failed = reply->error() != QNetworkReply::NoError || code >= 400;
        const bool transient = code == 0 || code == 408 || code == 429 || code == 500 || code == 502 || code == 503 || code == 504 || (code == 200 || code == 206);
        if (failed && transient && attempt < 3 && !body->exceeded && !body->writeFailed && !body->invalidRange &&
            (!retryAllowed || retryAllowed())) {
            const int retryAfter = reply->rawHeader("Retry-After").toInt();
            const int delay = retryAfter > 0 ? qBound(1, retryAfter, 30) * 1000 : (500 << attempt);
            status("A retomar transferência · tentativa " + QString::number(attempt + 2) + " / 4");
            QTimer::singleShot(delay, this, [this, path, destination, limit, cb, error, digest, body, attempt,
                                            retryAllowed, requestToken, requestServer, contractExport] {
                if (m_token != requestToken || m_server != requestServer || (retryAllowed && !retryAllowed())) {
                    error(0, "Transferência interrompida. Os ficheiros concluídos foram conservados."); return;
                }
                requestPhoto(path, destination, limit, cb, error, digest, body, attempt + 1, retryAllowed, contractExport);
            });
        } else if (failed && (code == 401 || code == 426)) {
            // A bounded/oversized error body is still an authentication rejection.
            // Deliver its HTTP status to the caller instead of the file-success
            // callback, so queues stop and the user is prompted to sign in/update.
            auto detail = QJsonDocument::fromJson(body->errorData).object()["detail"].toString();
            if (detail.isEmpty()) detail = reply->errorString();
            error(code, detail.left(500));
        } else if (body->exceeded || body->writeFailed || body->invalidRange) cb(body);
        else if (failed) {
            auto detail = QJsonDocument::fromJson(body->errorData).object()["detail"].toString();
            if (detail.isEmpty()) detail = reply->errorString();
            error(code, detail.left(500));
        } else cb(body);
        reply->deleteLater();
    });
}

// Keep the multipart body alive by parenting it to the network reply.
// Photograph uploads and Excel analysis use this separate request path.
void Lem::multipart(QString method, QString path, QList<QPair<QString, QString>> fields,
                    QString filename, QByteArray data, Callback cb, Failure error, QFile *file) {
    auto multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    for (auto field : fields) {
        QHttpPart p;
        p.setHeader(QNetworkRequest::ContentDispositionHeader,
                    "form-data; name=\"" + field.first + "\"");
        p.setBody(field.second.toUtf8());
        multi->append(p);
    }
    QHttpPart p;
    p.setHeader(QNetworkRequest::ContentDispositionHeader,
                "form-data; name=\"file\"; filename=\"upload" +
                    QFileInfo(filename).suffix().prepend('.') + "\"");
    p.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");
    if (file) {
        file->setParent(multi); // The open file survives until the request has finished.
        p.setBodyDevice(file);
    } else
        p.setBody(data);
    multi->append(p);
    QNetworkRequest req(QUrl(m_server + path));
    NetworkSecurity::restrictRedirects(req);
    req.setRawHeader("X-Lem-Client-Version", QCoreApplication::applicationVersion().toUtf8());
    req.setRawHeader("X-Lem-Client-Platform", NetworkSecurity::clientPlatform());
    req.setTransferTimeout(60000);
    req.setRawHeader("Authorization", "Bearer " + m_token.toUtf8());
    const auto requestToken = m_token;
    const auto requestServer = m_server;
    auto reply = net.sendCustomRequest(req, method.toUtf8(), multi);
    auto responseBody = NetworkSecurity::boundReply(reply, NetworkSecurity::responseLimit(path));
    multi->setParent(reply);
    trackReply(reply, method, path, true);
    connect(reply, &QNetworkReply::finished, this, [this, reply, cb, error, requestToken, requestServer, responseBody, file]() {
        if (file) file->close();
        responseBody->consume(reply);
        auto data = responseBody->data;
        auto code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (responseBody->exceeded || reply->error() != QNetworkReply::NoError) {
            const bool sameSession = m_server == requestServer && m_token == requestToken;
            if (code == 426 && sameSession)
                rejectIncompatibleReply(data);
            if (code == 401 && sameSession && !requestToken.isEmpty()) {
                clearSession();
                status(sessionEnded);
            }
            const auto detail = responseBody->exceeded ? QStringLiteral("Resposta do servidor demasiado grande.") : QString::fromUtf8(data) + " " + reply->errorString();
            if (error)
                error(code, detail);
            else {
                m_busy = false;
                status("Não foi possível enviar ficheiro: " + detail);
            }
        } else
            cb(data);
        reply->deleteLater();
    });
}

// Accept only the documented policy schema and re-check its semantic version bounds.
// This is release compatibility, not an authentication/security credential.
bool Lem::applyClientPolicy(const QJsonObject &policy) {
    const QRegularExpression pattern(
        "^(0|[1-9][0-9]{0,5})\\.(0|[1-9][0-9]{0,5})\\.(0|[1-9][0-9]{0,5})$");
    const auto current = QCoreApplication::applicationVersion();
    const auto minimum = policy["minimum_version"].toString();
    const auto latest = policy["latest_version"].toString();
    const auto low = QVersionNumber::fromString(minimum), high = QVersionNumber::fromString(latest);
    const auto version = QVersionNumber::fromString(current);
    const bool inRange = version >= low && version <= high;
    if (policy["policy_version"].toInt() != 1 || policy["current_version"].toString() != current ||
        !pattern.match(current).hasMatch() || !pattern.match(minimum).hasMatch() ||
        !pattern.match(latest).hasMatch() || low > high || !policy["compatible"].isBool() ||
        policy["compatible"].toBool() != inRange || !policy["update_available"].isBool() ||
        policy["update_available"].toBool() != (inRange && version < high)) {
        m_clientCompatibility = {
            {"checked", false},
            {"compatible", false},
            {"message",
             "O servidor não devolveu uma política de versões válida. Atualize o servidor ou confirme o endereço antes de entrar."}};
    } else {
        m_clientCompatibility = policy;
        m_clientCompatibility["checked"] = true;
        const auto url = QUrl(policy["update_url"].toString());
        if (url.scheme() != "https" || url.host().isEmpty() || !url.userInfo().isEmpty())
            m_clientCompatibility["update_url"] = "";
    }
    emit compatibilityChanged();
    return m_clientCompatibility["checked"].toBool() &&
           m_clientCompatibility["compatible"].toBool();
}

void Lem::rejectIncompatibleReply(const QByteArray &body) {
    clearSession();
    applyClientPolicy(QJsonDocument::fromJson(body).object()["detail"].toObject());
    emit changed();
}

void Lem::checkRestoredClientPolicy() {
    const auto token = m_token, server = m_server;
    request("GET", "/api/client-compatibility", {}, [this, token, server](QByteArray data) {
        if (m_token != token || m_server != server)
            return;
        if (!applyClientPolicy(QJsonDocument::fromJson(data).object()) &&
            m_clientCompatibility["checked"].toBool()) {
            clearSession();
            status(m_clientCompatibility["message"].toString());
        }
    }, "application/json", [](int, QString) {});
}

bool Lem::saveLastUser(const QString &digest) const {
    QSaveFile file(m_root + "/last-user.json");
    QJsonObject identity{{"server", m_server}, {"user", m_user}};
    if (!digest.isEmpty())
        identity["session_digest"] = digest;
    const auto bytes = json(identity);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}

bool Lem::saveSession() {
    const auto record = json({{"server", m_server}, {"user_id", m_user.value("id")}, {"token", m_token}});
    const bool stored = SessionStore::save(m_root, record);
    // The identity marker matches only this exact token. A partial write must
    // never restore a prior account/session from a different record.
    const bool recorded = saveLastUser(stored ? sessionDigest(m_token) : QString{});
    if (!(stored && recorded) && (stored || m_sessionPersisted))
        SessionStore::remove(m_root);
    m_sessionPersisted = stored && recorded;
    return m_sessionPersisted;
}

void Lem::clearSession() {
    ++m_stationReportRequest;
    m_stationReports = {};
    resetOrders();
    m_token.clear();
    m_pendingSessionToken.clear();
    m_sessionRetry.stop();
    m_autoSync.stop();
    // If the OS store cannot remove the credential, invalidate its local marker.
    // Keep the identity needed for offline access and for recovering its drafts.
    if (m_sessionPersisted && !SessionStore::remove(m_root))
        saveLastUser();
    m_sessionPersisted = false;
}

// Recheck the server on every login attempt, before sending credentials. Cached
// policy/offline identity never authorizes a login. The server also gates POST /login.
void Lem::login(QString email, QString password) {
    if (switchingBlocked() || !flushEdits())
        return;
    if (QUrl(m_server).scheme() == "https" && !QSslSocket::supportsSsl()) {
        status(
            "Este pacote da app não tem suporte TLS funcional. Recompile incluindo as bibliotecas OpenSSL para a plataforma e consulte o diagnóstico TLS no Qt Creator.");
        return;
    }
    m_busy = true;
    status("A verificar a compatibilidade da versão com o servidor…");
    request(
        "GET", "/api/client-compatibility", {},
        [this, email, password](QByteArray data) {
            if (!applyClientPolicy(QJsonDocument::fromJson(data).object())) {
                m_busy = false;
                status(m_clientCompatibility["message"].toString());
                return;
            }
            status("Versão compatível. A iniciar sessão…");
            request("POST", "/api/login", json({{"email", email}, {"password", password}}),
                    [this](QByteArray data) {
                        const auto o = QJsonDocument::fromJson(data).object();
                        if (o["token"].toString().isEmpty() ||
                            o["user"].toObject()["id"].toString().isEmpty()) {
                            m_busy = false;
                            status("Resposta de autenticação inválida.");
                            return;
                        }
                        m_token = o["token"].toString();
                        m_pendingSessionToken.clear();
                        m_sessionRetry.stop();
                        m_user = o["user"].toObject();
                        m_users = {};
                        m_deletedContracts = {};
                        emit contractTrashChanged();
                        m_importInfo = {};
                        m_preview = {};
                        m_excel.clear();
                        loadUser();
                        const bool remembered = saveSession();
                        m_busy = false;
                        if (m_localReadError) { emit changed(); return; }
                        refresh();
                        scheduleSync(1500);
                        if (!remembered)
                            status("Sessão iniciada. O dispositivo não guardou a sessão para a próxima abertura.");
                    });
        },
        "application/json",
        [this](int, QString) {
            m_busy = false;
            m_clientCompatibility = {
                {"checked", false},
                {"compatible", false},
                {"message",
                 "Não foi possível verificar a compatibilidade com o servidor. Confirme a ligação e a atualização do servidor. Os dados locais continuam disponíveis."}};
            emit compatibilityChanged();
            status(m_clientCompatibility["message"].toString());
        });
}

// Flush local work before clearing the session. This does not discard cached reports.
void Lem::logout() {
    if (switchingBlocked() || !flushEdits())
        return;
    if (authenticated() || sessionPending())
        request(
            "POST", "/api/logout", "{}", [](QByteArray) {}, "application/json",
            [](int, QString) {}, sessionPending());
    clearSession();
    resetStationCatalogue();
    m_importInfo = {};
    m_preview = {};
    m_excel.clear();
    m_reportPresentationSave = {};
    m_passwordReset = {};
    m_contractExportState = {};
    m_users = {};
    m_deletedContracts = {};
    emit contractTrashChanged();
    m_user = {};
    m_current = {};
    m_contracts = {};
    m_chargers = {};
    m_reports = {};
    m_contract.clear();
    m_deletedReports.clear();
    QFile::remove(m_root + "/last-user.json");
    status("Sessão terminada. Os rascunhos permanecem no dispositivo.");
}

// Refresh accessible contracts and force refresh of the selected catalogue.
void Lem::refresh() {
    refreshContracts(false);
}

// An automatic receive pass must not release photo-upload holds, retry a conflict
// as a manual send, or race the user's download or another catalogue operation.
bool Lem::refreshBackgroundCatalogues() {
    if (!authenticated() || localSaveError())
        return true;
    if (switchingBlocked() || m_stationCatalogueLoading || saving())
        return false;
    refreshContracts(false, true);
    return true;
}

void Lem::refreshContracts(bool fromManualSync, bool includeStations) {
    const bool restoring = sessionPending();
    if (m_refreshing)
        return;
    if (m_busy || !m_qveCopyTarget.isEmpty() || attachingFiles() || m_catalogueLoading) {
        if (restoring) m_sessionRetry.start(30000);
        return;
    }
    if (!restoring && !online())
        return;
    m_sessionRetry.stop();
    m_refreshing = true;
    emit changed();
    request(
        "GET", "/api/contracts", {},
        [this, fromManualSync, includeStations, restoring](QByteArray data) {
            const auto document = QJsonDocument::fromJson(data);
            // Only a complete list of identifiable contracts can revoke cached access.
            QSet<QString> allowedContracts;
            bool validContracts = document.isArray();
            for (const auto &entry : document.array()) {
                const auto cid = entry.toObject()["id"].toString();
                if (cid.isEmpty() || allowedContracts.contains(cid)) {
                    validContracts = false;
                    break;
                }
                allowedContracts.insert(cid);
            }
            if (!validContracts) {
                m_refreshing = false;
                if (restoring) m_sessionRetry.start(30000);
                status("Resposta de contratos inválida. Os dados locais continuam disponíveis.");
                return;
            }
            if (restoring) {
                // The catalogue uses the same authentication dependency as other
                // protected requests. Only now may the saved token authorize sync.
                m_token = std::exchange(m_pendingSessionToken, {});
                checkRestoredClientPolicy();
                if (m_localReadError) {
                    m_refreshing = false;
                    emit changed();
                    return;
                }
            }
            const auto previousContracts = m_contracts;
            m_contracts = document.array();
            // Scope lives on each membership. Preserve other contracts offline and
            // their report journals, including when the selected contract changes.
            for (const auto &cid : m_catalogues.keys()) {
                if (!allowedContracts.contains(cid)) {
                    m_catalogues.remove(cid);
                    m_catalogueTimes.remove(cid);
                }
            }
            for (const auto &cid : m_historicalNotes.keys())
                if (!allowedContracts.contains(cid)) m_historicalNotes.remove(cid);
            for (const auto &entry : m_contracts) {
                const auto row = entry.toObject();
                if (!row["order_only"].toBool()) continue;
                const auto cid = row["id"].toString();
                QSet<QString> allowed;
                for (const auto &id : row["order_targets"].toArray()) allowed.insert(id.toString());
                auto history = m_historicalNotes.value(cid).toObject();
                for (const auto &id : history.keys()) if (!allowed.contains(id)) history.remove(id);
                if (history.isEmpty()) m_historicalNotes.remove(cid);
                else m_historicalNotes[cid] = history;
                QJsonArray kept;
                QSet<QString> cached;
                for (const auto &value : m_catalogues.value(cid).toArray()) {
                    const auto id = value.toObject()["id"].toString();
                    if (allowed.contains(id)) { kept.append(value); cached.insert(id); }
                }
                // A server without order_targets cannot confirm the old scope.
                if (cached != allowed || !row["order_targets"].isArray()) m_catalogueTimes.remove(cid);
                if (m_catalogues.contains(cid)) m_catalogues[cid] = kept;
                if (m_contract == cid) m_chargers = kept;
            }
            refreshOrders();
            // An older catalogue reply must not undo an acknowledged settings save.
            for (auto value : previousContracts) {
                const auto contract = value.toObject();
                receiveContractPresentation(contract["id"].toString(), contract["presentation"].toObject());
            }
            applyContractPresentations();
            reconcileContractSync(fromManualSync);
            bool updateStations = false;
            if (m_user["role"] == "tech" && (m_stationCatalogueLoaded || m_stationCatalogueLoading)) {
                QSet<QString> accessible;
                // The independent PCVE catalogue is based on full contract grants.
                // A contract may remain in the order menu after its full grant is revoked.
                for (const auto &value : m_contracts)
                    if (!value.toObject()["order_only"].toBool()) accessible.insert(value.toObject()["id"].toString());
                QSet<QString> previous;
                for (const auto &value : previousContracts)
                    if (!value.toObject()["order_only"].toBool()) previous.insert(value.toObject()["id"].toString());
                updateStations = previous != accessible;
                if (updateStations && !previous.subtract(accessible).isEmpty())
                    resetStationCatalogue();
            }
            // A deleted/unassigned contract must not remain the active catalogue.
            // Preserve report journals and local photographs for offline recovery.
            bool stillAccessible = false;
            for (auto value : m_contracts)
                if (value.toObject()["id"].toString() == m_contract)
                    stillAccessible = true;
            if (!stillAccessible) {
                m_catalogues.remove(m_contract);
                m_catalogueTimes.remove(m_contract);
                m_contract.clear();
                m_chargers = {};
                if (!m_importInfo["station_catalogue"].toBool()) m_preview = {};
            }
            m_refreshing = false;
            persist();
            if (updateStations || includeStations) refreshStations();
            if (restoring) scheduleSync(1500);
            if (!m_contract.isEmpty())
                refreshCatalogue(m_contract, true);
            else
                status("Contratos atualizados.");
        },
        "application/json",
        [this, restoring](int code, QString) {
            m_refreshing = false;
            if (code == 401) {
                status(sessionEnded);
            } else if (code == 426) {
                status(m_clientCompatibility["message"].toString());
            } else if (restoring) {
                m_sessionRetry.start(30000);
                status("Sessão guardada. A aguardar ligação ao servidor. Os dados locais continuam disponíveis.");
            } else {
                status("Sem atualização do catálogo. Os dados locais continuam disponíveis.");
            }
        }, restoring);
}

// Show a cached catalogue immediately, then request a refresh when needed.
void Lem::selectContract(QString id) {
    if (m_busy || !m_qveCopyTarget.isEmpty() || attachingFiles() || m_user.isEmpty())
        return;
    if (id != m_contract) {
        m_contract = id;
        m_chargers = m_catalogues[id].toArray();
        m_current = {};
        if (!m_importInfo["station_catalogue"].toBool()) m_preview = {};
        persist();
        emit changed();
    }
    if (!m_token.isEmpty())
        refreshCatalogue(id);
    else
        status("Catálogo local disponível. Entre para atualizar e sincronizar.");
}

// Ordinary operation/status updates must not rebuild the complete report map.
// Getters remain correct even during a synchronous action before changed() emits.
QVariantMap Lem::current() const {
    if (m_projectedCurrent != m_current) {
        m_projectedCurrent = m_current;
        m_currentProjection = m_current.toVariantMap();
        if (!m_current.isEmpty()) m_currentProjection["identifier"] = ReportNaming::identifier(m_current);
    }
    return m_currentProjection;
}

QVariantMap Lem::photosByCheckpoint() const {
    const auto photos = m_current.value("photos").toArray();
    if (m_projectedPhotos != photos) {
        m_projectedPhotos = photos;
        QMap<QString, QVariantList> groups;
        for (const auto &entry : photos) {
            const auto photo = entry.toObject();
            groups[photo.value("checkpoint").toString()].append(photo.toVariantMap());
        }
        m_photoProjection.clear();
        for (auto it = groups.cbegin(); it != groups.cend(); ++it)
            m_photoProjection.insert(it.key(), it.value());
    }
    return m_photoProjection;
}

// Overlay local report summaries onto server catalogue entries for immediate feedback.
// This is a presentation projection; report answers remain in m_reports.
QVariantList Lem::chargers() const {
    return m_chargerRows;
}

QVariantList Lem::buildChargerRows() const {
    QString contractName;
    for (const auto &value : m_contracts)
        if (value.toObject()["id"] == m_contract) contractName = value.toObject()["name"].toString();
    QHash<QString, QJsonArray> localReports;
    for (auto value : m_reports) {
        const auto r = value.toObject();
        const auto payload = r["payload"].toObject();
        localReports[r["charger_id"].toString()].append(QJsonObject{{"id", r["id"]},
                                                                    {"status", r["status"]},
                                                                    {"owner_id", r["owner_id"]},
                                                                    {"identifier", ReportNaming::identifier(r)},
                                                                    {"started", payload["started"].toString(r["started"].toString())},
                                                                    {"finished", payload["finished"].toString(r["finished"].toString())},
                                                                    {"dirty", r["dirty"]}, {"revision", r["revision"]}});
    }
    QJsonArray out;
    for (auto value : m_chargers) {
        auto c = value.toObject();
        auto reports = c["reports"].toArray();
        QHash<QString, int> positions;
        for (int i = 0; i < reports.size(); ++i)
            positions[reports[i].toObject()["id"].toString()] = i;
        for (auto value : localReports.value(c["id"].toString())) {
            const auto r = value.toObject();
            const auto id = r["id"].toString();
            if (positions.contains(id)) {
                auto summary = reports[positions[id]].toObject();
                const bool pendingOwner = r["dirty"].toBool() && r["owner_id"] == m_user["id"] &&
                                          r["owner_id"] == summary["owner_id"] && summary["status"] != "closed";
                const bool newerLocal = summary.contains("revision") &&
                                        r["revision"].toInt() > summary["revision"].toInt();
                if (pendingOwner || newerLocal) {
                    summary["status"] = r["status"];
                    summary["owner_id"] = r["owner_id"];
                    summary["revision"] = r["revision"];
                    summary["identifier"] = r["identifier"];
                    summary["started"] = r["started"];
                    summary["finished"] = r["finished"];
                }
                if (!summary.contains("finished")) {
                    // Cached summaries from older clients lack separate dates.
                    // Use the local report's frozen context when it is available.
                    summary["identifier"] = r["identifier"];
                    summary["started"] = r["started"];
                    summary["finished"] = r["finished"];
                } else if (summary["identifier"].toString().isEmpty())
                    summary["identifier"] = r["identifier"];
                summary["dirty"] = r["dirty"];
                reports[positions[id]] = summary;
            } else {
                positions[id] = reports.size();
                reports.append(r);
            }
        }
        // Summary-only caches from before the change can still hold the old
        // dated name. Use their catalogue context until the report is received.
        for (int i = 0; i < reports.size(); ++i) {
            auto summary = reports[i].toObject();
            if (!summary.contains("finished") || summary["identifier"].toString().isEmpty()) {
                auto context = summary;
                context["snapshot"] = QJsonObject{{"charger", QJsonObject{{"external_id", c["external_id"]}}},
                                                  {"contract", QJsonObject{{"name", contractName}}}};
                summary["identifier"] = ReportNaming::identifier(context);
                reports[i] = summary;
            }
        }
        c["reports"] = reports;
        out.append(c);
    }
    return out.toVariantList();
}

// Show cached reports immediately, refresh clean observer/stale copies, or create a draft.
// Switching reports also releases photographs deferred until a question-group boundary.
bool Lem::openReport(QString chargerId, QString reportId) {
    if (m_busy || !m_qveCopyTarget.isEmpty() || attachingFiles() || m_user.isEmpty() || m_deletedReports.contains(reportId)) {
        if (reportId.isEmpty())
            status("Não foi possível iniciar a intervenção neste momento. Tente novamente.");
        return false;
    }
    if (!reportId.isEmpty() && reportId != m_current.value("id").toString())
        reportSyncBoundary();
    if (!reportId.isEmpty()) {
        if (m_reports.contains(reportId)) {
            m_current = withContractPresentation(Checklist::correctedDraft(m_reports[reportId].toObject()));
            m_template = m_current["template"].toObject();
            m_current["last_access"] = double(QDateTime::currentSecsSinceEpoch());
            m_reports[reportId] = m_current;
            emit changed();
            // A clean cached draft may have been closed on another device.
            bool stale = (!correctionAllowed(m_current) || m_current["owner_id"] != m_user["id"]);
            for (const auto &row : m_chargers)
                for (const auto &entry : row.toObject()["reports"].toArray()) {
                    const auto summary = entry.toObject();
                    if (summary["id"] == reportId &&
                        (summary["revision"].toInt() > m_current["revision"].toInt() ||
                         summary["owner_id"] != m_current["owner_id"] ||
                         (summary["status"] == "closed" && m_current["status"] != "closed")))
                        stale = true;
                }
            if (!m_token.isEmpty() && !m_current["dirty"].toBool() &&
                m_current["revision"].toInt() > 0 && stale)
                refreshReport();
            return true;
        }
        if (!online())
            return false;
        // The previous report is already checkpointed. It must never remain an
        // editable target while a different report is loading or after a failed GET.
        m_current = {};
        m_template = {};
        m_busy = true;
        emit changed();
        const auto contractId = m_contract, token = m_token;
        request("GET", "/api/reports/" + reportId, {}, [this, reportId, chargerId, contractId, token](QByteArray data) {
            auto report = QJsonDocument::fromJson(data).object();
            if (token != m_token || contractId != m_contract || report["id"] != reportId ||
                (!chargerId.isEmpty() && report["charger_id"] != chargerId) || report["contract_id"] != contractId) {
                m_busy = false;
                status("Não foi possível abrir o relatório selecionado.");
                return;
            }
            report["dirty"] = false;
            getPhotos(report, reportId);
        }, "application/json", [this](int code, QString) {
            m_busy = false;
            status(code == 410 || code == 404 ? "O relatório selecionado já não está disponível."
                                               : "Não foi possível abrir o relatório selecionado.");
        });
        return true;
    }
    if (!storageWritable()) return false;
    QJsonObject charger, contract;
    for (auto v : m_chargers)
        if (v.toObject()["id"].toString() == chargerId)
            charger = v.toObject();
    for (auto v : m_contracts)
        if (v.toObject()["id"].toString() == m_contract)
            contract = v.toObject();
    const auto job = contract["intervention"].toObject();
    if (!job.isEmpty() && !job["report_id"].toString().isEmpty()) {
        status("Esta intervenção já tem um relatório. Abra o relatório existente.");
        return false;
    }
    QString plannedItem;
    for (const auto &entry : m_orders) {
        const auto order = entry.toObject();
        for (const auto &value : order["items"].toArray()) {
            const auto item = value.toObject();
            if (item["target"] != chargerId || item["state"] != "pending") continue;
            if (!job.isEmpty() && !order["started"].toBool()) {
                status("A ordem desta intervenção ainda não começou.");
                return false;
            }
            if (order["started"].toBool()) plannedItem = item["id"].toString();
            if (!item["drafts"].toArray().isEmpty()) {
                status("Este ID já tem um rascunho. Abra o relatório existente ou peça a transferência ao administrador.");
                return false;
            }
        }
    }
    if (contract["order_only"].toBool() && !orderAllowsDraft(chargerId)) {
        status("Esta tarefa não está disponível para iniciar um relatório.");
        return false;
    }
    if (charger.isEmpty() || contract.isEmpty()) {
        status("Carregador indisponível para iniciar a intervenção.");
        return false;
    }
    QFile f(":/shared/checklist-v1.json");
    if (!f.open(QIODevice::ReadOnly)) {
        status("Checklist indisponível.");
        return false;
    }
    auto details = charger["details"].toObject();
    details["external_id"] = charger["external_id"];
    if (charger.contains("station_id")) details["station_id"] = charger["station_id"];
    QString checklistError;
    const auto configured = Checklist::forCharger(QJsonDocument::fromJson(f.readAll()).object(), details, &checklistError);
    if (!checklistError.isEmpty()) { status(checklistError); return false; }
    QJsonObject answers;
    const QMap<QString, QString> identityFields{{"EST.02", "manufacturer"}, {"EST.03", "model"}, {"EST.04", "serial"}};
    for (auto it = identityFields.begin(); it != identityFields.end(); ++it)
        if (!details[it.value()].toString().isEmpty())
            answers[it.key()] = QJsonObject{{"value", details[it.value()]}, {"notes", ""}};
    answers["FIN.02"] = QJsonObject{{"value", m_user["name"]}, {"notes", ""}};
    const auto started = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    QJsonObject snapshot{{"charger", details}, {"contract", contract}};
    if (!job.isEmpty()) {
        snapshot["contract"] = job["contract"];
        snapshot["intervention"] = QJsonObject{{"id", job["id"]}, {"kind", job["kind"]}, {"reference", job["reference"]}};
    }
    QJsonObject draft{{"id", uuid()},
                            {"charger_id", chargerId},
                            {"contract_id", m_contract},
                            {"presentation", contract["presentation"]},
                            {"owner_id", m_user["id"]},
                            {"revision", 0},
                            {"status", "draft"},
                            {"dirty", true},
                            {"payload", QJsonObject{{"answers", answers}, {"started", started}}},
                            {"photos", QJsonArray{}},
                            {"template", configured},
                            {"snapshot", snapshot},
                            {"started", started}};
    if (!plannedItem.isEmpty()) draft["_order_item"] = plannedItem;
    if (!checkpointReport(draft, "O novo rascunho não foi criado. Tente novamente após verificar o armazenamento."))
        return false;
    // Only leave the previous report once we can open the new one.
    reportSyncBoundary();
    m_current = draft;
    m_template = configured;
    status("Novo rascunho guardado no dispositivo.");
    emit draftCreated(draft["id"].toString());
    return true;
}

// Despite the historical name, install report/photo METADATA without downloading images.
// Recover available private file paths, then checkpoint the selected report.
bool Lem::validRemoteReport(const QJsonObject &report, const QString &expectedId) {
    return !expectedId.isEmpty() && report.value("id") == expectedId &&
           readableReport(report) && !report.value("charger_id").toString().isEmpty() &&
           report.value("owner_id").isString() && report.value("revision").toInt() > 0 &&
           (report.value("status") == "draft" || report.value("status") == "closed");
}

bool Lem::getPhotos(QJsonObject report, const QString &expectedId) {
    if (!validRemoteReport(report, expectedId)) {
        m_busy = false;
        status("Resposta de report inválida. Os dados locais foram conservados.");
        return false;
    }
    if (const auto item = report.value("work_item_id"); item.isString())
        report["_order_item"] = item;
    else if (const auto local = m_reports.value(expectedId).toObject(); local.contains("_order_item"))
        report["_order_item"] = local["_order_item"];
    m_syncConflicts.remove(report["id"].toString());
    // Metadata first; remote originals are downloaded only on explicit demand.
    report["offline_photos"] =
        m_reports.value(report["id"].toString()).toObject()["offline_photos"].toBool();
    report = receivedPhotoPaths(report);
    m_current = withContractPresentation(Checklist::correctedDraft(report));
    m_template = m_current["template"].toObject();
    m_busy = false;
    remember();
    status("Report disponível. Fotografias antigas podem ser descarregadas quando necessário.");
    return true;
}

QJsonObject Lem::receivedPhotoPaths(QJsonObject report) const {
    auto photos = report["photos"].toArray();
    for (int i = 0; i < photos.size(); ++i) {
        auto p = photos[i].toObject();
        const QString path = SafeStorage::photoPath(cacheDir(), p["id"].toString(), p["mime"].toString());
        p["uploaded"] = true;
        p["path"] = !path.isEmpty() && QFileInfo(path).size() > 0 ? QUrl::fromLocalFile(path).toString() : QString();
        photos[i] = p;
    }
    report["photos"] = photos;
    report["last_access"] = double(QDateTime::currentSecsSinceEpoch());
    return report;
}

// Hot input path: update memory and queue a small journal instead of rewriting the cache.
// _edit_seq distinguishes newer local typing from an older in-flight server snapshot.
void Lem::answer(QString key, QString value, QString notes) {
    if (!storageWritable()) return;
    if (copyLocksCheckpoint(m_current["id"].toString(), key) || m_busy || m_current.isEmpty() || m_current["_conflict"].toBool() ||
        m_current["status"].toString() == "closed" || (!correctionAllowed(m_current) || m_current["owner_id"] != m_user["id"]))
        return;
    const auto signedPhoto = m_current["payload"].toObject()["signatures"].toObject()[key].toObject()["photo_id"].toString();
    if (!signedPhoto.isEmpty() && m_current["payload"].toObject()["answers"].toObject()[key].toObject()["value"].toString() != value) {
        removePhoto(signedPhoto);
        if (m_current["payload"].toObject()["signatures"].toObject().contains(key)) return;
    }
    auto payload = m_current["payload"].toObject();
    auto answers = payload["answers"].toObject();
    auto nextAnswer = answers[key].toObject();
    const auto previous = nextAnswer;
    const bool valueChanged = previous["value"].toString() != value;
    const bool wasNA = Measurements::notApplicable(previous["value"].toString());
    const bool isNA = Measurements::notApplicable(value);
    const auto previousNotes = previous["notes"].toString();
    // The editor displays TPM for an old untouched suggestion. Treat that
    // display alias as the old note during a status action, so it can clear.
    if (key == "EST.15" && MaintenanceRules::untouched(previous) && notes == Checklist::paymentNote(previousNotes))
        notes = previousNotes;
    // Provenance, not wording alone, identifies a removable automatic note.
    if (notes != previousNotes) nextAnswer.remove("suggested_notes");
    nextAnswer["notes"] = notes;
    if (valueChanged && (wasNA || isNA || value.isEmpty() || previous["value"] == "OK" || previous["value"] == "NOK"))
        MaintenanceRules::clearSuggestion(nextAnswer);
    nextAnswer["value"] = value;
    if (!value.trimmed().isEmpty()) nextAnswer.remove("previous_value");
    if (isNA || value.isEmpty()) {
        nextAnswer.remove("assessment"); nextAnswer.remove("auto_assessment");
    }
    if (isNA && !wasNA)
        MaintenanceRules::suggest(nextAnswer, Checklist::naNotesDefault(m_current["template"].toObject(), key));
    if (answers[key].toObject() == nextAnswer)
        return;
    if (answers[key].toObject()["value"].toString() != value) {
        auto signatures = payload["signatures"].toObject();
        signatures.remove(key); // A changed name requires signing again.
        payload["signatures"] = signatures;
    }
    answers[key] = nextAnswer;
    if (m_template["version"].toInt() >= 7 && valueChanged &&
        (key == "MAN.11" || key == "MAN.12" || key == "MAN.18"))
        answers = MaintenanceRules::apply(answers, true);
    payload["answers"] = answers;
    m_current["payload"] = payload;
    m_current["dirty"] = true;
    m_current["_edit_seq"] = m_current["_edit_seq"].toDouble() + 1;
    queueEdits(m_current);
    m_reports[m_current.value("id").toString()] = m_current;
    if (json(payload).size() > LEM_PAYLOAD_UTF8_BYTES)
        status("Report demasiado grande. Reduza o texto das observações antes de concluir.");
    else
        emit changed();
    scheduleSync();
}
// Focus is an explicit request to replace a generated observation. A custom
// observation, even one with identical wording, has no marker and is preserved.
void Lem::beginNotesEdit(QString key) {
    const auto currentAnswer = m_current["payload"].toObject()["answers"].toObject()[key].toObject();
    if (MaintenanceRules::untouched(currentAnswer)) answer(key, currentAnswer["value"].toString(), "");
}
// Manual assessment is independent of the technician's mandatory reading.
void Lem::setAssessment(QString key, QString assessment) {
    if (!storageWritable()) return;
    if (copyLocksCheckpoint(m_current["id"].toString(), key) || m_busy || m_current.isEmpty() ||
        m_current["_conflict"].toBool() || m_current["status"] != "draft" ||
        (!correctionAllowed(m_current) || m_current["owner_id"] != m_user["id"]) || (assessment != "OK" && assessment != "NOK")) return;
    bool allowed = false;
    for (auto section : m_template["sections"].toArray())
        for (auto item : section.toObject()["items"].toArray())
            if (item.toObject()["id"] == key && item.toObject()["reading_required"].toBool()) allowed = true;
    if (!allowed) return;
    auto payload = m_current["payload"].toObject();
    auto answers = payload["answers"].toObject();
    auto answer = answers[key].toObject();
    if (answer["assessment"] == assessment && !Measurements::notApplicable(answer["value"].toString())) return;
    if (answer["assessment"] != assessment || Measurements::notApplicable(answer["value"].toString()))
        MaintenanceRules::clearSuggestion(answer);
    if (Measurements::notApplicable(answer["value"].toString())) answer["value"] = "";
    answer["value"] = answer["value"].toString();
    answer["notes"] = answer["notes"].toString();
    answer["assessment"] = assessment;
    answer.remove("auto_assessment");
    answers[key] = answer;
    // A manual OK cannot override a measured failure; custom notes still survive.
    if (m_template["version"].toInt() >= 7) answers = MaintenanceRules::apply(answers, false);
    payload["answers"] = answers;
    m_current["payload"] = payload;
    m_current["dirty"] = true;
    m_current["_edit_seq"] = m_current["_edit_seq"].toDouble() + 1;
    queueEdits(m_current);
    m_reports[m_current["id"].toString()] = m_current;
    emit changed(); scheduleSync();
}
QString Lem::localPath(QUrl url) const {
    return url.isLocalFile() ? url.toLocalFile() : url.toString();
}
void Lem::attach(QString key, QUrl url) {
    attachToReport(m_current.value("id").toString(), key, url);
}

// Validate and copy an image into private storage before adding it to the report journal.
// Use the captured reportId, which may differ from the report currently on screen.
bool Lem::attachToReport(QString reportId, QString key, QUrl url) {
    auto report = m_current.value("id").toString() == reportId
                      ? m_current
                      : m_reports.value(reportId).toObject();
    if (copyLocksCheckpoint(reportId, key) || m_busy || report.isEmpty() || report["_conflict"].toBool() ||
        report["status"].toString() == "closed" || !correctionAllowed(report) || report["owner_id"] != m_user["id"]) {
        status("Não é possível adicionar fotografias a este report.");
        return false;
    }
    bool known = false;
    // A recovered camera result may belong to a different, frozen report.
    for (auto section : report["template"].toObject()["sections"].toArray())
        for (auto item : section.toObject()["items"].toArray())
            if (item.toObject()["id"].toString() == key)
                known = true;
    if (!known) {
        status("Item de report desconhecido.");
        return false;
    }
    QFile f(localPath(url));
    if (!f.open(QIODevice::ReadOnly)) {
        status("Não foi possível ler a fotografia.");
        return false;
    }
    auto bytes = f.read(60 * 1024 * 1024 + 1);
    if (bytes.size() > 60 * 1024 * 1024) {
        status("Máximo 60 MiB por vídeo.");
        return false;
    }
    const auto videoMime = EvidenceMedia::videoMime(bytes);
    if (videoMime.isEmpty() && bytes.size() > 20 * 1024 * 1024) {
        status("Máximo 20 MiB por fotografia."); return false;
    }
    QString ext;
    if (!videoMime.isEmpty()) {
        if (!EvidenceMedia::durationValid(bytes)) {
            status("Vídeo inválido ou superior a 120 segundos. Use MP4 ou 3GP.");
            return false;
        }
        ext = videoMime == "video/3gpp" ? ".3gp" : ".mp4";
    } else {
        QBuffer input(&bytes);
        input.open(QIODevice::ReadOnly);
        QImageReader reader(&input);
        reader.setAutoTransform(true);
        const auto size = reader.size();
        if (!size.isValid() || qint64(size.width()) * size.height() > 40000000) {
            status("Imagem inválida ou superior a 40 MP.");
            return false;
        }
        const auto format = reader.format().toLower();
        const bool jpeg = format == "jpeg" || format == "jpg";
        const bool png = format == "png";
        if ((jpeg && !EvidenceMedia::completeJpeg(bytes)) || (png && !EvidenceMedia::completePng(bytes))) {
            status("Fotografia incompleta. Selecione o original.");
            return false;
        }
        const auto image = reader.read();
        if (image.isNull()) {
            status("Não foi possível ler a fotografia completa.");
            return false;
        }
        ext = png ? ".png" : ".jpg";
        if (!jpeg && !png) {
            QByteArray converted;
            QBuffer output(&converted);
            output.open(QIODevice::WriteOnly);
            if (!image.save(&output, "JPEG", 92)) {
                status("Não foi possível converter a fotografia.");
                return false;
            }
            bytes = converted;
        }
    }
    if (bytes.size() > (videoMime.isEmpty() ? 20 : 60) * 1024 * 1024) {
        status("Limite por ficheiro: 20 MiB por fotografia ou 60 MiB por vídeo."); return false;
    }
    auto existing = report["photos"].toArray();
    qint64 total = bytes.size();
    for (auto value : existing) total += value.toObject()["size"].toDouble();
    if (existing.size() >= 200 || total > 400 * 1024 * 1024) {
        status("Limite por report: 200 evidências e 400 MiB."); return false;
    }
    QString id = uuid(), path = cacheDir() + "/photos/" + id + ext;
    QDir().mkpath(cacheDir() + "/photos");
    QSaveFile dest(path);
    if (!dest.open(QIODevice::WriteOnly)) {
        status("Não foi possível guardar a evidência.");
        return false;
    }
    if (dest.write(bytes) != bytes.size()) {
        status("Não foi possível guardar a evidência.");
        return false;
    }
    if (!dest.commit()) {
        status("Não foi possível guardar a evidência.");
        return false;
    }
    auto photos = report["photos"].toArray();
    photos.append(QJsonObject{{"id", id},
                              {"checkpoint", key},
                              {"filename", id + ext},
                              {"mime", videoMime.isEmpty() ? (ext == ".png" ? QString("image/png") : QString("image/jpeg")) : videoMime},
                              {"path", QUrl::fromLocalFile(path).toString()},
                              {"uploaded", false},
                              {"size", int(bytes.size())}});
    report["photos"] = photos;
    report["dirty"] = true;
    report["_defer_photo_sync"] = true;
    report["_edit_seq"] = report["_edit_seq"].toDouble() + 1;
    // Commit only this report's small journal; avoid rewriting every cached report.
    if (!saveEdits(report)) {
        QFile::remove(path);
        return false;
    }
    m_reports[reportId] = report;
    if (m_current.value("id").toString() == reportId)
        m_current = report;
    m_photoBytes += bytes.size();
    m_syncState = "Fotografias guardadas · envio ao mudar de grupo ou sair do report";
    status("Evidência guardada no dispositivo. Será enviada ao mudar de grupo ou sair do report.");
    return true;
}

// Image edits create a fresh evidence identity: an acknowledged server attachment is
// immutable. Persist the replacement and deletion tombstone before unlinking it.
void Lem::cropPhoto(QString photoId, qreal x, qreal y, qreal width, qreal height) {
    editPhoto(photoId, 0, x, y, width, height);
}

void Lem::rotatePhoto(QString photoId) {
    editPhoto(photoId, 1, 0, 0, 1, 1);
}

bool Lem::editPhoto(QString photoId, int quarterTurns, qreal x, qreal y, qreal width, qreal height) {
    if (reportActionBlocked(false) || m_current.isEmpty() || m_current["_conflict"].toBool() ||
        m_current["status"].toString() != "draft" || (!correctionAllowed(m_current) || m_current["owner_id"] != m_user["id"]))
        return false;
    const auto payload = m_current["payload"].toObject();
    for (const auto field : {"signatures", "legacy_signatures"}) {
        for (const auto &entry : payload[field].toObject()) {
            if (entry.toObject()["photo_id"] != photoId) continue;
            status(QLatin1StringView(field) == "legacy_signatures"
                       ? "A imagem da assinatura histórica deve ser conservada."
                       : "Para alterar a assinatura, assine novamente em FIN.02.");
            return false;
        }
    }
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) || !std::isfinite(height) ||
        x < 0 || y < 0 || width <= 0 || height <= 0 || x + width > 1.000001 || y + height > 1.000001 ||
        quarterTurns < 0 || quarterTurns > 3)
        return false;
    QJsonObject old;
    for (auto entry : m_current["photos"].toArray())
        if (entry.toObject()["id"].toString() == photoId) old = entry.toObject();
    if (copyLocksCheckpoint(m_current["id"].toString(), old["checkpoint"].toString())) return false;
    const auto source = localPath(QUrl(old["path"].toString()));
    if (old.isEmpty() || old["mime"].toString().startsWith("video/") || !QFileInfo::exists(source)) return false;
    // Opening and saving without changes (including four preview turns) must not
    // recompress a JPEG, create a tombstone or schedule an unnecessary upload.
    if (quarterTurns == 0 && x == 0 && y == 0 && width == 1 && height == 1) return true;
    if (!flushEdits()) return false;
    const auto reportId = m_current["id"].toString(), newId = uuid();
    const auto mime = old["mime"].toString() == "image/png" || QFileInfo(source).suffix() == "png"
        ? QString("image/png") : QString("image/jpeg");
    const auto target = SafeStorage::photoPath(cacheDir(), newId, mime);
    m_busy = true;
    m_localOperationTitle = "A editar fotografia";
    status(m_localOperationTitle);
    auto watcher = new QFutureWatcher<qint64>(this);
    connect(watcher, &QFutureWatcher<qint64>::finished, this,
            [this, watcher, reportId, photoId, newId, target, mime, old, source] {
        const auto size = watcher->result(); watcher->deleteLater();
        m_busy = false; m_localOperationTitle.clear();
        auto report = m_reports.value(reportId).toObject();
        auto photos = report["photos"].toArray(); int index = -1;
        for (int i = 0; i < photos.size(); ++i)
            if (photos[i].toObject()["id"].toString() == photoId) index = i;
        if (size <= 0 || index < 0 || report["status"].toString() != "draft" ||
            !correctionAllowed(report) || report["owner_id"] != m_user["id"] || report["_conflict"].toBool()) {
            QFile::remove(target); status("Não foi possível editar. Fotografia original conservada."); return;
        }
        qint64 total = size;
        for (auto value : photos)
            if (value.toObject()["id"].toString() != photoId) total += value.toObject()["size"].toDouble();
        if (total > 400 * 1024 * 1024) {
            QFile::remove(target); status("Limite de 400 MiB por report. Fotografia original conservada."); return;
        }
        auto replacement = old;
        replacement["id"] = newId; replacement["mime"] = mime;
        replacement["filename"] = QFileInfo(target).fileName();
        replacement["path"] = QUrl::fromLocalFile(target).toString();
        replacement["size"] = double(size); replacement["uploaded"] = false;
        replacement.remove("digest"); photos[index] = replacement;
        auto deleted = report["deleted_photos"].toArray();
        if (!deleted.contains(photoId)) deleted.append(photoId);
        report["photos"] = photos; report["deleted_photos"] = deleted;
        report["dirty"] = true; report["_defer_photo_sync"] = true;
        report["_edit_seq"] = report["_edit_seq"].toDouble() + 1;
        if (!saveEdits(report)) { QFile::remove(target); return; }
        m_reports[reportId] = report;
        if (m_current["id"].toString() == reportId) m_current = report;
        m_photoBytes += size;
        const QFileInfo original(source);
        // Never delete an original outside Lem's private evidence directory.
        if (original.canonicalPath() == QFileInfo(cacheDir() + "/photos").canonicalFilePath() &&
            (original.fileName() == photoId + ".jpg" || original.fileName() == photoId + ".png")) {
            const auto oldSize = original.size();
            if (QFile::remove(source)) m_photoBytes = qMax(qint64(0), m_photoBytes - oldSize);
        }
        status("Fotografia editada. Envio ao mudar de grupo ou sair do report.");
    });
    watcher->setFuture(QtConcurrent::run(&m_editPool, [source, target, x, y, width, height, mime, quarterTurns]() -> qint64 {
        QImageReader reader(source); reader.setAutoTransform(true);
        const auto dimensions = reader.size();
        if (!dimensions.isValid() || qint64(dimensions.width()) * dimensions.height() > 40000000) return -1;
        auto image = reader.read(); if (image.isNull()) return -1;
        // Match the preview: decode EXIF, rotate, then crop in the rotated space.
        // One encoding and one journal replacement cover the entire edit.
        if (quarterTurns) image = image.transformed(QTransform().rotate(quarterTurns * 90));
        const int left = qBound(0, int(std::floor(x * image.width())), image.width() - 1);
        const int top = qBound(0, int(std::floor(y * image.height())), image.height() - 1);
        const int right = qBound(left + 1, int(std::ceil((x + width) * image.width())), image.width());
        const int bottom = qBound(top + 1, int(std::ceil((y + height) * image.height())), image.height());
        const auto edited = image.copy(left, top, right - left, bottom - top);
        QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly);
        if (!edited.save(&buffer, mime == "image/png" ? "PNG" : "JPEG", 92) || bytes.size() > 20 * 1024 * 1024) return -1;
        QSaveFile file(target);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) return -1;
        return bytes.size();
    }));
    return true;
}

void Lem::openVideo(QString photoId) {
    for (auto entry : m_current["photos"].toArray()) {
        const auto photo = entry.toObject();
        if (photo["id"].toString() != photoId || !photo["mime"].toString().startsWith("video/")) continue;
        const auto url = QUrl(photo["path"].toString());
        if (!QFileInfo::exists(localPath(url)) || !QDesktopServices::openUrl(url))
            status("Não foi possível abrir o vídeo. Instale um leitor de vídeo ou descarregue a evidência.");
        return;
    }
}

// Persist removal and its local tombstone before deleting private image files.
// This is also used when the server confirms a report was permanently deleted.
bool Lem::forgetReport(QString id) {
    if (!flushEdits())
        return false;
    const auto oldReports = m_reports, oldCurrent = m_current, oldCatalogues = m_catalogues;
    const auto oldChargers = m_chargers;
    const auto oldDeleted = m_deletedReports;
    const auto photos = m_reports.value(id).toObject()["photos"].toArray();
    m_reports.remove(id);
    m_deletedReports.insert(id);
    if (m_current.value("id").toString() == id)
        m_current = {};
    auto filter = [id](QJsonArray rows) {
        for (int i = 0; i < rows.size(); ++i) {
            auto c = rows[i].toObject();
            auto reports = c["reports"].toArray();
            for (int n = reports.size() - 1; n >= 0; --n)
                if (reports[n].toObject()["id"].toString() == id)
                    reports.removeAt(n);
            c["reports"] = reports;
            rows[i] = c;
        }
        return rows;
    };
    m_chargers = filter(m_chargers);
    for (const auto &key : m_catalogues.keys())
        m_catalogues[key] = filter(m_catalogues[key].toArray());
    if (!persist()) {
        m_reports = oldReports;
        m_current = oldCurrent;
        m_catalogues = oldCatalogues;
        m_chargers = oldChargers;
        m_deletedReports = oldDeleted;
        return false;
    }
    QFile::remove(editPath(id));
    m_syncConflicts.remove(id);
    for (auto value : photos) {
        const auto p = value.toObject();
        const QFileInfo f(localPath(QUrl(p["path"].toString())));
        const auto pid = p["id"].toString();
        if (f.canonicalPath() == QFileInfo(cacheDir() + "/photos").canonicalFilePath() &&
            (f.fileName() == pid + ".jpg" || f.fileName() == pid + ".png" || f.fileName() == pid + ".mp4" || f.fileName() == pid + ".3gp")) {
            const auto size = f.size();
            if (QFile::remove(f.absoluteFilePath()))
                m_photoBytes = qMax(qint64(0), m_photoBytes - size);
        }
    }
    emit changed();
    return true;
}

// Never-uploaded drafts can be removed offline; server-backed deletions need a revision.
// The administrator privilege to delete closed reports is separate from draft ownership.
void Lem::deleteReport(QString id) {
    if (!correctionAllowed(m_current)) return;
    if (id == m_qveCopyTarget || id == m_attachReport) return;
    if (id != m_current.value("id").toString() || reportActionBlocked())
        return;
    const auto r = m_current;
    const bool admin = m_user["role"].toString() == "admin";
    if (r.isEmpty() ||
        (!admin && (r["status"].toString() != "draft" || r["owner_id"] != m_user["id"] || isOrderRecovery(r)))) {
        status("Não tem permissão para apagar este report.");
        return;
    }
    if (r["revision"].toInt() == 0 && !r.contains("_inflight_revision")) {
        if (forgetReport(id))
            status("Rascunho local e fotografias apagados.");
        return;
    }
    if (!online())
        return;
    m_busy = true;
    emit changed();
    request("DELETE",
            "/api/reports/" + id + "?base_revision=" + QString::number(r["revision"].toInt()) +
                (r["correction"].toObject()["state"] == "editing" ? "&correction_only=true" : ""), {},
            [this, id](QByteArray bytes) {
                const auto ack = QJsonDocument::fromJson(bytes).object();
                m_busy = false;
                if (ack["id"].toString() != id || !ack["deleted"].toBool()) {
                    status("O servidor não confirmou a remoção. Report conservado.");
                    return;
                }
                if (forgetReport(id))
                    status("Report e fotografias apagados definitivamente.");
            });
}

// Queue server deletion even when an upload was not acknowledged.
// A lost upload response may mean the server already has the photograph.
void Lem::removePhoto(QString photoId) {
    if (m_busy || m_current.isEmpty() || m_current["_conflict"].toBool() ||
        m_current["status"].toString() != "draft" || (!correctionAllowed(m_current) || m_current["owner_id"] != m_user["id"]))
        return;
    auto photos = m_current["photos"].toArray();
    int found = -1;
    for (int i = 0; i < photos.size(); ++i)
        if (photos[i].toObject()["id"].toString() == photoId) {
            found = i;
            break;
        }
    if (found < 0)
        return;
    const auto removed = photos[found].toObject();
    if (copyLocksCheckpoint(m_current["id"].toString(), removed["checkpoint"].toString())) return;
    photos.removeAt(found);
    const auto previous = m_current;
    const auto reportId = m_current.value("id").toString();
    const auto previousReports = m_reports;
    // Queue even unacknowledged uploads: a lost upload response may have left a server copy.
    auto deleted = m_current["deleted_photos"].toArray();
    if (!deleted.contains(photoId))
        deleted.append(photoId);
    auto payload = m_current["payload"].toObject();
    // A migrated correction can contain a historical signature at a renamed
    // checkpoint. Identify it by the cloned evidence ID, even with no local file.
    for (const auto field : {"signatures", "legacy_signatures"}) {
        const auto references = payload.value(field).toObject();
        auto kept = references;
        for (auto it = references.begin(); it != references.end(); ++it)
            if (it.value().toObject()["photo_id"] == photoId) kept.remove(it.key());
        if (kept != references) payload[field] = kept;
    }
    m_current["payload"] = payload;
    m_current["photos"] = photos;
    m_current["deleted_photos"] = deleted;
    m_current["dirty"] = true;
    m_current["_edit_seq"] = m_current["_edit_seq"].toDouble() + 1;
    m_reports[reportId] = m_current;
    if (!persist()) {
        m_current = previous;
        m_reports = previousReports;
        emit changed();
        return;
    }
    removeCachedPhoto(removed);
    emit changed();
    status("Fotografia removida do rascunho. A remoção será enviada automaticamente.");
    scheduleSync();
}

// Called only after durable removal, including a retry once an upload releases its handle.
void Lem::removeCachedPhoto(const QJsonObject &photo) {
    // Only remove our private cache copy, never the original in the user's gallery.
    const auto photoId = photo["id"].toString();
    const QFileInfo cached(localPath(QUrl(photo["path"].toString())));
    if (cached.canonicalPath() == QFileInfo(cacheDir() + "/photos").canonicalFilePath() &&
        (cached.fileName() == photoId + ".jpg" || cached.fileName() == photoId + ".png" || cached.fileName() == photoId + ".mp4" || cached.fileName() == photoId + ".3gp")) {
        const auto bytes = cached.size();
        if (QFile::remove(cached.absoluteFilePath()))
            m_photoBytes = qMax(qint64(0), m_photoBytes - bytes);
    }
}

// Android delegates to the installed camera through a small Java Activity.
// Capture the report/checkpoint identity now, rather than using the active page on return.
void Lem::takePhoto(QString key) { takeCapture(key, false); }
void Lem::takeVideo(QString key) { takeCapture(key, true); }
void Lem::takeCapture(QString key, bool video) {
#ifdef Q_OS_ANDROID
    if (m_busy || m_cameraPending || m_current.isEmpty() || m_current["_conflict"].toBool() ||
        m_current["status"].toString() == "closed" || (!correctionAllowed(m_current) || m_current["owner_id"] != m_user["id"]))
        return;
    checkCameraResult();
    if (QFile::exists(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
                      "/camera-result.json")) {
        status("Existe uma captura pendente. Entre na conta do report original para a recuperar.");
        return;
    }
    m_cameraPending = true;
    const auto report = m_current.value("id").toString();
    QPointer<Lem> guard(this);
    QNativeInterface::QAndroidApplication::runOnAndroidMainThread([guard, report, key, video]() {
        auto activity = QNativeInterface::QAndroidApplication::context();
        const auto launched = QJniObject::callStaticMethod<jboolean>(
            "pt/lem/app/CaptureActivity", "launch",
            "(Landroid/app/Activity;Ljava/lang/String;Ljava/lang/String;Z)Z",
            activity.object<jobject>(), QJniObject::fromString(report).object<jstring>(),
            QJniObject::fromString(key).object<jstring>(), jboolean(video));
        if (!launched) {
            if (guard)
                QMetaObject::invokeMethod(
                    guard.data(),
                    [guard]() {
                        if (guard) {
                            guard->m_cameraPending = false;
                            guard->status(
                                "Não foi possível abrir a câmara. Use Escolher fotografia existente.");
                        }
                    },
                    Qt::QueuedConnection);
        }
    });
#else
    Q_UNUSED(key);
    Q_UNUSED(video);
    status("Captura pela aplicação de câmara disponível em Android.");
#endif
}

// Consume the Android file mailbox after returning to the active application.
// Delete a successful temporary capture only after the private report copy was saved.
void Lem::checkCameraResult() {
#ifdef Q_OS_ANDROID
    if (m_busy)
        return;
    const auto cache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QFile result(cache + "/camera-result.json");
    if (!result.open(QIODevice::ReadOnly))
        return;
    auto data = QJsonDocument::fromJson(result.readAll()).object();
    result.close();
    m_cameraPending = false;
    if (data.isEmpty()) {
        result.remove();
        status("Resposta inválida da câmara.");
        return;
    }
    if (!data["error"].toString().isEmpty()) {
        result.remove();
        status(data["error"].toString());
        return;
    }
    const auto reportId = data["reportId"].toString();
    // The report was deleted while the capture was pending: it can never be delivered,
    // and keeping the result would block the camera on this device permanently.
    if (!m_user.isEmpty() && m_deletedReports.contains(reportId)) {
        const auto captured = QFileInfo(data["path"].toString()).canonicalFilePath();
        const auto directory = QFileInfo(cache + "/camera").canonicalFilePath();
        if (!captured.isEmpty() && !directory.isEmpty() && captured.startsWith(directory + '/'))
            QFile::remove(captured);
        result.remove();
        status("O report desta captura foi apagado; fotografia descartada.");
        return;
    }
    // Wait until the original user's cached report is available after recreation/login.
    if (m_user.isEmpty() || !m_reports.contains(reportId))
        return;
    const auto source = QFileInfo(data["path"].toString()).canonicalFilePath();
    const auto directory = QFileInfo(cache + "/camera").canonicalFilePath();
    if (source.isEmpty() || directory.isEmpty() || !source.startsWith(directory + '/')) {
        result.remove();
        status("Fotografia da câmara em falta.");
        return;
    }
    if (attachToReport(reportId, data["checkpoint"].toString(), QUrl::fromLocalFile(source))) {
        QFile::remove(source);
        result.remove();
    } else {
        // Keep the source for diagnosis/recovery without retrying a rejected image every second.
        QFile::remove(cache + "/camera-result.failed.json");
        result.rename(cache + "/camera-result.failed.json");
    }
#endif
}

int Lem::pendingCount() const {
    int n = 0;
    for (auto v : m_reports)
        if (v.toObject()["dirty"].toBool())
            n++;
    return n;
}

// Validate the local checklist and evidence, then close locally before background sending.
// The server independently validates closure after photograph uploads are acknowledged.
bool Lem::desktopCorrections() const {
    return NetworkSecurity::clientPlatform() == "windows" || NetworkSecurity::clientPlatform() == "linux";
}

bool Lem::desktopStationManagement() const {
    return desktopCorrections() || NetworkSecurity::clientPlatform() == "macos";
}

bool Lem::canManageStations() const {
    return desktopStationManagement() && authenticated() && m_user["role"] == "admin";
}

void Lem::resetStationCatalogue() {
    ++m_stationRequest;
    m_stations = {};
    m_stationCatalogueLoaded = false;
    m_stationCatalogueLoading = false;
    m_stationCatalogueMessage.clear();
    m_stationEditResult = {};
    ++m_stationReportRequest;
    m_stationReports = {};
}

void Lem::refreshStations() {
    if (!authenticated()) return;
    const auto generation = ++m_stationRequest;
    m_stationCatalogueLoading = true;
    m_stationCatalogueMessage.clear();
    emit changed();
    request("GET", "/api/stations", {}, [this, generation](QByteArray data) {
        if (generation != m_stationRequest) return;
        m_stationCatalogueLoading = false;
        if (!authenticated()) {
            emit changed();
            return;
        }
        const auto document = QJsonDocument::fromJson(data);
        const auto rows = document.array();
        QSet<QString> ids;
        bool valid = document.isArray();
        for (const auto &value : rows) {
            const auto row = value.toObject();
            const auto id = row["id"].toString();
            if (id.isEmpty() || ids.contains(id) || row["external_id"].toString().isEmpty() ||
                row["updated"].toString().isEmpty() || !row["details"].isObject()) {
                valid = false;
                break;
            }
            ids.insert(id);
        }
        if (valid) {
            applyStationRecords(rows, true);
            m_stationCatalogueLoaded = true;
            persist();
        } else {
            m_stationCatalogueMessage = "A resposta da base de IDs de PCVEs é inválida.";
        }
        emit changed();
    }, "application/json", [this, generation](int code, QString) {
        if (generation != m_stationRequest) return;
        m_stationCatalogueLoading = false;
        if (code == 403) {
            resetStationCatalogue();
            persist();
        }
        if (code == 404)
            m_stationCatalogueMessage = "Atualize o servidor para consultar a base de IDs de PCVEs.";
        else if (m_stationCatalogueLoaded)
            m_stationCatalogueMessage = "Sem ligação à base de IDs de PCVEs. A mostrar os dados guardados.";
        else
            m_stationCatalogueMessage = "Não foi possível carregar a base de IDs de PCVEs.";
        emit changed();
    });
}

void Lem::loadStationReports(QString stationId) {
    const auto requestId = ++m_stationReportRequest;
    m_stationReports = {{"id", stationId}};
    if (stationId.isEmpty()) {
        emit changed();
        return;
    }
    QSet<QString> contracts;
    for (const auto &value : m_contracts) contracts.insert(value.toObject()["id"].toString());
    // Physical and membership UUIDs are distinct. Older cached reports can
    // predate both the top-level station link and its copy in the frozen snapshot.
    QHash<QString, QString> membershipStations;
    for (const auto &cid : contracts) {
        for (const auto &value : m_catalogues.value(cid).toArray()) {
            const auto row = value.toObject();
            if (!row["station_id"].toString().isEmpty())
                membershipStations.insert(row["id"].toString(), row["station_id"].toString());
        }
    }
    QString externalId;
    for (const auto &value : m_stations) {
        const auto row = value.toObject();
        if (row["id"] == stationId) { externalId = row["external_id"].toString(); break; }
    }
    QJsonArray cached, pending;
    for (const auto &value : std::as_const(m_reports)) {
        const auto report = value.toObject();
        const auto snapshot = report["snapshot"].toObject();
        const auto contract = snapshot["contract"].toObject();
        const auto cid = report["contract_id"].toString(contract["id"].toString());
        if (!contracts.contains(cid) || !correctionAllowed(report) ||
            m_deletedReports.contains(report["id"].toString())) continue;
        const auto charger = snapshot["charger"].toObject();
        auto physicalId = report["station_id"].toString(charger["station_id"].toString());
        if (physicalId.isEmpty())
            physicalId = membershipStations.value(report["charger_id"].toString());
        // Use the immutable spreadsheet ID only when all UUID links are missing.
        const bool belongs = physicalId.isEmpty()
            ? !externalId.isEmpty() && charger["external_id"] == externalId
            : physicalId == stationId;
        if (!belongs) continue;
        const QJsonObject row{{"id", report["id"]}, {"charger_id", report["charger_id"]},
                              {"contract_id", cid}, {"contract", snapshot["intervention"].toObject()["reference"].toString(contract["name"].toString())},
                              {"status", report["status"]}, {"started", report["started"]},
                              {"identifier", ReportNaming::identifier(report)}};
        cached.append(row);
        if (report["dirty"].toBool() && !report["revision"].toInt() && report["owner_id"] == m_user["id"])
            pending.append(row);
    }
    if (!authenticated()) {
        m_stationReports["rows"] = cached;
        m_stationReports["message"] = "Sem ligação validada. A mostrar os relatórios guardados neste dispositivo.";
        emit changed();
        return;
    }
    m_stationReports["message"] = "A carregar relatórios…";
    emit changed();
    request("GET", "/api/stations/" + QString::fromLatin1(QUrl::toPercentEncoding(stationId)) + "/reports", {},
            [this, requestId, pending](QByteArray data) {
                if (requestId != m_stationReportRequest) return;
                const auto doc = QJsonDocument::fromJson(data);
                auto rows = doc.array();
                QSet<QString> ids;
                for (const auto &entry : rows) ids.insert(entry.toObject()["id"].toString());
                for (const auto &entry : pending)
                    if (!ids.contains(entry.toObject()["id"].toString())) rows.append(entry);
                m_stationReports["rows"] = rows;
                m_stationReports["message"] = !doc.isArray() ? "Resposta inválida do servidor."
                    : rows.isEmpty() ? "Sem relatórios associados." : "";
                emit changed();
            }, "application/json", [this, requestId, cached](int code, QString) {
                if (requestId != m_stationReportRequest) return;
                if (!code || code >= 500) m_stationReports["rows"] = cached;
                m_stationReports["message"] = (!code || code >= 500) && !cached.isEmpty()
                    ? "Servidor indisponível. A mostrar os relatórios guardados neste dispositivo."
                    : code == 404 ? "Ficha indisponível ou servidor sem suporte para o histórico."
                                                          : "Não foi possível consultar os relatórios.";
                emit changed();
            });
}

void Lem::detachCharger(QString contractId, QString chargerId) {
    if (!authenticated() || m_user["role"] != "admin" || contractId != m_contract ||
        contractId.isEmpty() || chargerId.isEmpty() || m_busy || m_catalogueLoading) return;
    m_busy = true;
    emit changed();
    request("DELETE", "/api/contracts/" + QString::fromLatin1(QUrl::toPercentEncoding(contractId)) +
            "/chargers/" + QString::fromLatin1(QUrl::toPercentEncoding(chargerId)), {},
            [this, contractId, chargerId](QByteArray) {
                m_busy = false;
                auto rows = m_catalogues[contractId].toArray();
                auto history = m_historicalNotes.value(contractId).toObject();
                for (qsizetype i = rows.size(); i-- > 0;) {
                    if (rows.at(i).toObject()["id"] == chargerId)
                        retainContractNote(history, rows.takeAt(i).toObject());
                }
                m_historicalNotes[contractId] = history;
                m_catalogues[contractId] = rows;
                if (m_contract == contractId) m_chargers = rows;
                m_catalogueTimes.remove(contractId);
                persist();
                if (m_contract == contractId) refreshCatalogue(contractId, true);
                refreshStations();
                status("PCVE retirado do contrato.");
            }, "application/json", [this](int, QString) {
                m_busy = false;
                status("Não foi possível retirar o PCVE. Atualize o contrato e tente novamente.");
            });
}

void Lem::retainContractNote(QJsonObject &history, const QJsonObject &row) {
    const auto id = row["id"].toString();
    const auto revision = row["notes_revision"].toInt(-1);
    if (!id.isEmpty() && revision >= 0 && row["contract_notes"].isString()
        && history.value(id).toObject()["notes_revision"].toInt(-1) < revision)
        history[id] = QJsonObject{{"contract_notes", row["contract_notes"]}, {"notes_revision", revision}};
}

QVariantMap Lem::historicalContractNotes(QString contractId, QString chargerId) const {
    if (m_user.isEmpty() || contractId.isEmpty() || contractId != m_contract) return {};
    return m_historicalNotes.value(contractId).toObject().value(chargerId).toObject().toVariantMap();
}

void Lem::loadContractNotes(QString contractId, QString chargerId) {
    exchangeContractNotes(contractId, chargerId, {});
}

void Lem::saveContractNotes(QString contractId, QString chargerId, int baseRevision, QString notes) {
    if (m_user["role"] != "admin" || baseRevision < 0) return;
    exchangeContractNotes(contractId, chargerId, {{"base_revision", baseRevision}, {"notes", notes}});
}

void Lem::exchangeContractNotes(const QString &cid, const QString &id, const QJsonObject &edit) {
    const bool saving = !edit.isEmpty();
    if (id.isEmpty() || cid.isEmpty() || cid != m_contract || !authenticated()) {
        emit contractNotesFailed(cid, id, 0, "Sem ligação ao servidor.", saving);
        return;
    }
    // A late reply from a previous account/server must not update the new user's cache.
    const auto session = [this, token = m_token, server = m_server] {
        return m_token == token && m_server == server;
    };
    const auto path = "/api/contracts/" + QString::fromLatin1(QUrl::toPercentEncoding(cid))
                    + "/chargers/" + QString::fromLatin1(QUrl::toPercentEncoding(id)) + "/notes";
    request(saving ? "PUT" : "GET", path, saving ? json(edit) : QByteArray{},
        [this, cid, id, saving, session](QByteArray data) {
            if (!session()) return;
            auto record = QJsonDocument::fromJson(data).object();
            if (record["id"] != id || record["contract_id"] != cid || !record["contract_notes"].isString()
                || record["notes_revision"].toInt(-1) < 0 || !record["attached"].isBool()) {
                emit contractNotesFailed(cid, id, 0, "Confirmação de notas inválida.", saving);
                return;
            }
            // Update only this membership. Detached history can still be read
            // without reintroducing the ID into the active contract list.
            auto rows = m_catalogues.value(cid).toArray();
            auto history = m_historicalNotes.value(cid).toObject();
            const auto prior = history.value(id).toObject();
            if (prior["notes_revision"].toInt(-1) > record["notes_revision"].toInt()) {
                record["contract_notes"] = prior["contract_notes"];
                record["notes_revision"] = prior["notes_revision"];
            }
            bool listed = false;
            for (qsizetype i = 0; i < m_contracts.size(); ++i) {
                auto scope = m_contracts[i].toObject();
                auto job = scope["intervention"].toObject();
                if (job["id"] != id || job["notes_revision"].toInt() > record["notes_revision"].toInt()) continue;
                job["notes_revision"] = record["notes_revision"];
                job["notes"] = record["contract_notes"];
                scope["intervention"] = job;
                m_contracts[i] = scope;
            }
            for (qsizetype i = 0; i < rows.size(); ++i) {
                auto row = rows[i].toObject();
                if (row["id"] != id) continue;
                listed = true;
                if (row["notes_revision"].toInt(-1) > record["notes_revision"].toInt()) {
                    record["contract_notes"] = row["contract_notes"];
                    record["notes_revision"] = row["notes_revision"];
                } else if (row["notes_revision"] != record["notes_revision"]) {
                    row["contract_notes"] = record["contract_notes"];
                    row["notes_revision"] = record["notes_revision"];
                    rows[i] = row;
                    m_catalogues[cid] = rows;
                    if (m_contract == cid) m_chargers = rows;
                    persist();
                    emit changed();
                }
                break;
            }
            if (!listed && cid == m_contract && prior["notes_revision"] != record["notes_revision"]) {
                retainContractNote(history, record);
                m_historicalNotes[cid] = history;
                persist();
            }
            emit contractNotesReceived(record.toVariantMap(), saving);
        }, "application/json", [this, cid, id, saving, session](int code, const QString &body) {
            if (!session()) return;
            const auto detail = QJsonDocument::fromJson(body.toUtf8()).object()["detail"].toString();
            const auto fallback = code == 422 ? QStringLiteral("Texto inválido. Use até 32 767 caracteres.")
                                              : QStringLiteral("Não foi possível contactar o servidor.");
            emit contractNotesFailed(cid, id, code, detail.isEmpty()
                ? fallback : detail, saving);
        });
}

void Lem::saveStation(QString id, QString baseUpdated, QVariantMap fields) {
    if (!canManageStations() || m_busy || id.isEmpty() || baseUpdated.isEmpty() || fields.isEmpty()) return;
    m_busy = true;
    m_stationEditResult = {{"id", id}, {"state", "saving"}};
    emit changed();
    const auto fail = [this, id](const QString &message) {
        m_busy = false;
        m_stationEditResult = {{"id", id}, {"state", "error"}, {"message", message}};
        emit changed();
    };
    request("PATCH", "/api/stations/" + QString::fromLatin1(QUrl::toPercentEncoding(id)),
            json({{"base_updated", baseUpdated}, {"fields", QJsonObject::fromVariantMap(fields)}}),
            [this, id, fail](QByteArray data) {
                const auto row = QJsonDocument::fromJson(data).object();
                if (row["id"] != id || row["external_id"].toString().isEmpty() ||
                    row["updated"].toString().isEmpty() || !row["details"].isObject()) {
                    fail("A confirmação do servidor é inválida. Atualize a ficha antes de repetir.");
                    return;
                }
                applyStationRecords(QJsonArray{row});
                persist();
                m_busy = false;
                m_stationEditResult = {{"id", id}, {"state", "saved"}};
                status("Ficha PCVE guardada.");
            }, "application/json", [this, fail](int code, const QString &body) {
                const auto detail = QJsonDocument::fromJson(body.toUtf8()).object()["detail"].toString();
                if (code == 409) {
                    fail("A ficha foi atualizada entretanto. Reveja a versão atual antes de guardar de novo.");
                    refreshStations();
                } else {
                    fail(detail.isEmpty() ? QString("Não foi possível guardar a ficha PCVE.") : detail);
                }
            });
}

bool Lem::correctionAllowed(const QJsonObject &report) const {
    if (isOrderRecovery(report)) return false;
    if (report["status"] == "draft" && report["template"].toObject()["frozen_upload"].toBool()) return false;
    const auto correction = report["correction"].toObject();
    return correction["state"] != "editing" || (desktopCorrections() && m_user["role"] == "admin" &&
        correction["editor_id"] == m_user["id"]);
}

void Lem::beginReportCorrection() {
    if (!desktopCorrections() || m_user["role"] != "admin" || reportActionBlocked() ||
        m_current["status"] != "closed" || m_current["dirty"].toBool() ||
        m_current["revision"].toInt() < 1 || !online() || !flushEdits()) return;
    const auto original = m_current;
    const auto sourceId = original["id"].toString();
    auto requestId = original["_correction_request_id"].toString();
    if (requestId.isEmpty()) requestId = uuid();
    m_current["_correction_request_id"] = requestId;
    m_reports[sourceId] = m_current;
    if (!persist()) return;
    m_busy = true;
    status("A preparar cópia de correção; o original mantém-se disponível…");
    request("POST", "/api/reports/" + sourceId + "/corrections",
        json({{"id", requestId}, {"base_revision", original["revision"]}}),
        [this, original, requestId](QByteArray data) {
            auto report = QJsonDocument::fromJson(data).object();
            if (!validRemoteReport(report, requestId) || report["correction"].toObject()["state"] != "editing") {
                m_busy = false; status("Resposta inválida; repita a abertura da correção para recuperar a cópia."); return;
            }
            const auto directory = cacheDir();
            auto watcher = new QFutureWatcher<void>(this);
            connect(watcher, &QFutureWatcher<void>::finished, this, [this, watcher, report, original, requestId] {
                watcher->deleteLater();
                auto source = m_reports.value(original["id"].toString()).toObject();
                source.remove("_correction_request_id");
                if (!source.isEmpty()) m_reports[original["id"].toString()] = source;
                auto ready = report; ready["dirty"] = false;
                if (!getPhotos(ready, requestId)) return;
                status("Cópia de correção aberta. O original só será substituído ao guardar a correção completa.");
            });
            watcher->setFuture(QtConcurrent::run(&m_editPool, [original, report, directory] {
                const auto mapping = report["correction"].toObject()["photo_map"].toObject();
                for (auto value : original["photos"].toArray()) {
                    const auto photo = value.toObject();
                    const auto target = SafeStorage::photoPath(directory, mapping[photo["id"].toString()].toString(), photo["mime"].toString());
                    if (target.isEmpty() || QFileInfo::exists(target)) continue;
                    const auto local = QUrl(photo["path"].toString()).toLocalFile();
                    if (local.isEmpty()) continue;
                    QFile input(local);
                    QSaveFile output(target);
                    if (input.open(QIODevice::ReadOnly) && output.open(QIODevice::WriteOnly) &&
                        ReportBundle::copy(input, output)) output.commit();
                }
            }));
        }, "application/json", [this](int, QString detail) {
            m_busy = false; status("Não foi possível abrir a correção. O original foi conservado. " + detail);
        });
}

bool Lem::setCorrectionTimes(QString started, QString finished) {
    if (!correctionAllowed(m_current) || m_current.value("correction").toObject()["state"] != "editing" ||
        m_current.value("status") != "draft" || m_busy || m_current.value("_conflict").toBool()) {
        status("A correção não está disponível para alterar as datas neste momento.");
        return false;
    }
    const auto start = QDateTime::fromString(started.trimmed(), Qt::ISODate);
    const auto end = QDateTime::fromString(finished.trimmed(), Qt::ISODate);
    if (!start.isValid() || !end.isValid() || start.timeSpec() == Qt::LocalTime ||
        end.timeSpec() == Qt::LocalTime || end < start) {
        status("Indique início e fim com fuso horário (ex.: 2026-11-16T09:30:00+00:00). O fim não pode ser anterior ao início.");
        return false;
    }
    auto payload = m_current["payload"].toObject();
    payload["started"] = start.toUTC().toString(Qt::ISODate);
    payload["finished"] = end.toUTC().toString(Qt::ISODate);
    auto corrected = m_current;
    corrected["payload"] = payload;
    corrected["started"] = payload["started"];
    corrected["finished"] = payload["finished"];
    corrected["dirty"] = true;
    corrected["_edit_seq"] = corrected["_edit_seq"].toDouble() + 1;
    if (!checkpointReport(corrected, "Não foi possível guardar as datas. As datas anteriores foram conservadas. Tente novamente após verificar o armazenamento."))
        return false;
    m_current = corrected;
    status("Datas da correção guardadas.");
    return true;
}

bool Lem::receiveReplacement(const QString &oldId, const QString &newId) {
    auto old = m_reports.value(oldId).toObject();
    if (old.isEmpty()) return true;
    if (!old["dirty"].toBool()) return forgetReport(oldId);
    old["_conflict"] = true;
    old["_replacement_id"] = newId;
    old["_conflict_reason"] = "O original foi substituído por uma correção. As suas alterações locais foram conservadas para consulta ou cópia.";
    m_reports[oldId] = old;
    if (m_current["id"] == oldId) m_current = old;
    m_syncConflicts.insert(oldId);
    saveEdits(old);
    return persist();
}

void Lem::finish() {
    if (!m_qveCopyTarget.isEmpty() || attachingFiles()) { status("Aguarde o fim da cópia ou da seleção de evidências antes de concluir."); return; }
    if (m_busy || m_current.isEmpty() || m_current["_conflict"].toBool() ||
        (!correctionAllowed(m_current) || m_current["owner_id"] != m_user["id"]) || m_current["status"].toString() == "closed")
        return;
    auto answers = m_current["payload"].toObject()["answers"].toObject();
    if (m_template["version"].toInt() >= 7) answers = MaintenanceRules::apply(answers, false);
    auto photos = m_current["photos"].toArray();
    QStringList errors;
    for (const auto &entry : photos) {
        const auto photo = entry.toObject();
        const auto mime = photo["mime"].toString();
        if (!photo["uploaded"].toBool() && mime.startsWith("image/") &&
            !EvidenceMedia::readableStoredPhoto(QUrl(photo["path"].toString()).toLocalFile(), mime))
            errors << photo["checkpoint"].toString() + ": substitua a fotografia incompleta pelo original";
    }
    if (m_template["version"].toInt() >= 7 && MaintenanceRules::reading(answers, "MAN.11") == 0)
        errors << "MAN.11: o calibre configurado deve ser superior a zero";
    const QMap<QString, QString> identityFields{{"EST.02", "manufacturer"}, {"EST.03", "model"}, {"EST.04", "serial"}};
    for (auto s : m_template["sections"].toArray())
        for (auto i : s.toObject()["items"].toArray()) {
            auto item = i.toObject();
            auto id = item["id"].toString();
            auto a = answers.value(id).toObject();
            auto value = a["value"].toString().trimmed();
            const auto measurementKey = m_template["version"].toInt() < 7 && Measurements::milliamp(id) ? Measurements::canonical(id) : id;
            if (Measurements::digits(measurementKey) && !value.isEmpty()) {
                bool valid = false;
                const auto formatted = Measurements::format(measurementKey, value, &valid);
                if (!valid) errors << id + ": valor numérico inválido";
                else {
                    value = formatted;
                    a["value"] = formatted;
                    answers[id] = a;
                }
            }
            const auto signature = m_current["payload"].toObject()["signatures"].toObject()[id].toObject();
            if (item["signature_required"].toBool() && signature.isEmpty())
                errors << id + ": falta assinatura do técnico";
            if (!signature.isEmpty()) {
                bool signatureFound = false;
                for (auto entry : photos) {
                    const auto p = entry.toObject();
                    if (p["id"] == signature["photo_id"] && p["checkpoint"] == id && p["mime"] == "image/png") signatureFound = true;
                }
                if (!signatureFound || signature["signed_by"].toString() != value)
                    errors << id + ": assinatura inválida; assine novamente";
            }
            if (m_template["version"].toInt() >= 4 && identityFields.contains(id) && !Measurements::notApplicable(value) &&
                value != m_current["snapshot"].toObject()["charger"].toObject()[identityFields.value(id)].toString().trimmed() && value.size() > 500)
                errors << id + ": máximo 500 caracteres";
            const auto notesLimit = item["kind"] == "notes" ? LEM_GENERAL_NOTE_CHARACTERS : LEM_ANSWER_CHARACTERS;
            if (a["notes"].toString().toUcs4().size() > notesLimit ||
                a["value"].toString().toUcs4().size() > LEM_ANSWER_CHARACTERS)
                errors << id + ": texto demasiado longo";
            if (item["kind"] == "status" && !a["value"].toString().isEmpty() &&
                !QStringList{"OK", "NOK", "NA"}.contains(a["value"].toString()))
                errors << id + ": escolha OK, NOK ou N/A";
            if (value.isEmpty() && !item["optional"].toBool())
                errors << id + ": falta resposta";
            if (item["reading_required"].toBool() && (value.compare("OK", Qt::CaseInsensitive) == 0 || value.compare("NOK", Qt::CaseInsensitive) == 0))
                errors << id + ": falta valor medido";
            if (!item["notes_disabled"].toBool() && (value == "NOK" || a["assessment"] == "NOK") && a["notes"].toString().trimmed().isEmpty())
                errors << id + ": falta observação";
            if (item["na_notes_required"].toBool() && Measurements::notApplicable(value) && a["notes"].toString().trimmed().isEmpty())
                errors << id + ": N/A precisa de observação";
            bool found = false;
            for (auto p : photos)
                if (p.toObject()["checkpoint"].toString() == id && !p.toObject()["mime"].toString().startsWith("video/"))
                    found = true;
            if (item["photo_required"].toBool() && (item["photo_always"].toBool() || !Measurements::notApplicable(value)) && !found)
                errors << id + ": falta fotografia";
        }
    if (!errors.isEmpty()) {
        status("Não é possível concluir: " + errors.mid(0, 10).join("; "));
        return;
    }
    const auto previous = m_current;
    const auto previousReports = m_reports;
    const auto previousChargers = m_chargers;
    const auto previousCatalogues = m_catalogues;
    const auto previousStations = m_stations;
    m_current.remove("_defer_photo_sync");
    m_current["status"] = "closed";
    auto payload = m_current["payload"].toObject();
    payload["answers"] = answers;
    const bool correction = m_current["correction"].toObject()["state"] == "editing";
    if (!correction) {
        if (previous["started"].isString()) payload["started"] = previous["started"];
        payload["finished"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    }
    const auto start = QDateTime::fromString(correction ? payload["started"].toString() :
        previous["started"].toString(payload["started"].toString()), Qt::ISODate);
    const auto end = QDateTime::fromString(payload["finished"].toString(), Qt::ISODate);
    if (!start.isValid() || !end.isValid() || start.timeSpec() == Qt::LocalTime ||
        end.timeSpec() == Qt::LocalTime || end < start) {
        m_current = previous;
        status("Verifique o relógio e as datas do report. O fim não pode ser anterior ao início.");
        return;
    }
    if (json(payload).size() > LEM_PAYLOAD_UTF8_BYTES) {
        m_current = previous;
        status("Report demasiado grande. Reduza o texto das observações antes de concluir.");
        return;
    }
    m_current["payload"] = payload;
    m_current["dirty"] = true;
    m_current["_edit_seq"] = m_current["_edit_seq"].toDouble() + 1;
    if (!correction) applyClosedIdentity(m_current);
    if (m_template["version"].toInt() >= 4) {
        auto snapshot = m_current["snapshot"].toObject();
        auto details = snapshot["charger"].toObject();
        if (!m_current["revision"].toInt() && !correction)
            m_current["_station_identity_at_start"] = stationIdentity(details);
        for (auto it = identityFields.begin(); it != identityFields.end(); ++it) {
            const auto value = answers[it.key()].toObject()["value"].toString().trimmed();
            if (!value.isEmpty() && !Measurements::notApplicable(value)) details[it.value()] = value;
        }
        snapshot["charger"] = details;
        m_current["snapshot"] = snapshot;
    }
    m_reports[m_current.value("id").toString()] = m_current;
    if (!persist()) {
        m_current = previous;
        m_reports = previousReports;
        m_chargers = previousChargers;
        m_catalogues = previousCatalogues;
        m_stations = previousStations;
        emit changed();
        return;
    }
    emit changed();
    status("Report concluído no dispositivo. Pode continuar; envio automático em segundo plano.");
    if (!m_token.isEmpty())
        sync();
}
QString Lem::defaultExport(QString kind) const {
    return QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) +
                               "/" + ReportNaming::identifier(m_current) +
                               (kind == "pdf" ? ".pdf" : kind == "recovery" ? "_recuperacao.zip" : ".zip"))
        .toString();
}

// Validate before opening a document picker: Android providers may create an empty
// document at selection time. Local PDF generation does not require a session.
bool Lem::prepareExport(QString kind) {
    if (m_busy || m_current.isEmpty()) {
        status("Abra um report antes de exportar e aguarde a operação em curso.");
        return false;
    }
    if (kind == "recovery")
        return m_current["_conflict"].toBool() && m_current["owner_id"] == m_user["id"] && !m_current["_order_item"].toString().isEmpty();
    if (m_current["status"].toString() != "closed") {
        status("Conclua o report antes de exportar PDF ou ZIP.");
        return false;
    }
    if (m_current["correction"].toObject()["state"] == "editing") {
        status("Aguarde a confirmação da correção pelo servidor antes de exportar."); return false;
    }
    if (kind == "pdf") {
        const auto problem = ReportPdf::validate(m_current);
        if (problem.isEmpty())
            return true;
        if (!m_token.isEmpty() && !m_current["dirty"].toBool())
            return true;
        status(problem);
        return false;
    }
    if (kind == "bundle") {
        if (ReportPdf::validate(m_current).isEmpty()) return true;
        if (!online())
            return false;
        if (m_current["dirty"].toBool()) {
            status(
                "Faltam evidências neste dispositivo. Sincronize o report e descarregue-as antes de exportar o ZIP.");
            return false;
        }
        return true;
    }
    return false;
}
void Lem::exportSaved(const QString &kind, const QUrl &target, bool local) {
    QString message =
        (local ? (kind == "recovery" ? "Cópia de recuperação guardada: " : kind == "bundle" ? "ZIP exportado localmente: " : "PDF exportado localmente: ") : "Exportado: ") + target.toDisplayString();
    // Called on the GUI thread only after the writer has closed/committed the file.
    // Qt uses Android's content URI/FileProvider support, or the desktop file association.
    if (kind == "pdf" && !QDesktopServices::openUrl(target))
        message += "\nO PDF foi guardado, mas não foi possível abrir o leitor. "
                   "Abra-o a partir dos ficheiros do dispositivo.";
    status(message);
}
void Lem::exportReport(QString kind, QUrl target) {
    if (!prepareExport(kind))
        return;
    if (kind == "recovery") { exportPreparedReport(kind, target); return; }
    const auto report = withContractPresentation(m_current);
    const auto cid = report["contract_id"].toString();
    const auto rid = report["id"].toString();
    const bool serverReport = report["revision"].toInt() > 0;
    if (m_token.isEmpty() || (!serverReport && cid.isEmpty())) {
        exportPreparedReport(kind, target);
        return;
    }
    // A valid contract presentation does not prove that this UUID survived
    // an administrator correction. Read identity, evidence and style together.
    m_busy = true;
    status("A confirmar o relatório e o formato do contrato…");
    const auto path = serverReport ? "/api/reports/" + rid : "/api/contracts/" + cid + "/presentation";
    request("GET", path, {}, [this, rid, cid, serverReport, kind, target](QByteArray bytes) {
        m_busy = false;
        if (m_current["id"] != rid) { emit changed(); return; }
        const auto received = QJsonDocument::fromJson(bytes).object();
        const auto style = serverReport ? received["presentation"].toObject() : received;
        const auto resolved = serverReport ? received["contract_id"].toString() : cid;
        if (resolved.isEmpty() || (!cid.isEmpty() && cid != resolved) ||
            (style["issuer"] != "Lem" && style["issuer"] != "CSMTEC") ||
            (serverReport && (!validRemoteReport(received, rid) || received["charger_id"] != m_current["charger_id"] ||
             (received["status"] != "closed" && !(m_current["dirty"].toBool() &&
               received["owner_id"] == m_current["owner_id"] && received["revision"] == m_current["revision"]))))) {
            status("Não foi possível confirmar o relatório. A exportação foi cancelada.");
            return;
        }
        if (serverReport && !m_current["dirty"].toBool()) {
            // A matching frozen revision can gain evidence from another device.
            // Keep the local historical snapshot and previously downloaded paths.
            if (received["revision"] == m_current["revision"])
                m_current["photos"] = reconcilePhotos(m_current["photos"].toArray(), received["photos"].toArray(), {});
            else if (!getPhotos(received, rid))
                return;
        }
        m_current["contract_id"] = resolved;
        m_reports[rid] = m_current;
        if (m_pendingEdits.contains(rid)) m_pendingEdits[rid] = m_current;
        receiveContractPresentation(resolved, style);
        if (persist()) exportPreparedReport(kind, target);
    }, "application/json", [this, rid, cid, kind, target](int code, QString detail) {
        m_busy = false;
        if (m_current["id"] != rid) { emit changed(); return; }
        if (code == 410) {
            const auto successor = QJsonDocument::fromJson(detail.toUtf8()).object()["detail"].toObject()["replacement_id"].toString();
            const auto charger = m_current["charger_id"].toString();
            if (!successor.isEmpty()) {
                if (receiveReplacement(rid, successor) && !m_reports.value(rid).toObject()["dirty"].toBool())
                    openReport(charger, successor);
                status("Relatório substituído. Consulte a correção antes de exportar.");
            } else {
                if (!m_current["dirty"].toBool()) forgetReport(rid);
                status("O relatório foi apagado no servidor. A exportação foi cancelada.");
            }
        } else if (!cid.isEmpty() && (code == 0 || code >= 500)) {
            exportPreparedReport(kind, target);
        } else status("Não foi possível confirmar o relatório e o formato do contrato: " + detail);
    });
}

void Lem::exportPreparedReport(QString kind, QUrl target) {
    if (!prepareExport(kind)) return;
    if (kind == "recovery" || ((kind == "pdf" || kind == "bundle") && ReportPdf::validate(m_current).isEmpty())) {
        if (!flushEdits())
            return;
        auto report = withContractPresentation(m_current);
        if (report["technician"].toString().isEmpty() && report["owner_id"] == m_user["id"])
            report["technician"] = m_user["name"];
        if (report["technician"].toString().isEmpty())
            report["technician"] =
                report["payload"].toObject()["answers"].toObject()[report["template"].toObject()["version"].toInt() >= 3 ? "FIN.02" : "PG04.02"].toObject()["value"];
        const auto destination = localPath(target);
        m_busy = true;
        m_localOperationTitle = kind == "recovery" ? "A guardar cópia de recuperação" : kind == "bundle" ? "A gerar e guardar ZIP neste dispositivo" : "A gerar e guardar PDF neste dispositivo";
        status(m_localOperationTitle);
        auto watcher = new QFutureWatcher<QString>(this);
        connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, target, kind] {
            const auto error = watcher->result();
            watcher->deleteLater();
            m_busy = false;
            m_localOperationTitle.clear();
            if (error.isEmpty())
                exportSaved(kind, target, true);
            else
                status(error);
        });
        watcher->setFuture(QtConcurrent::run(&m_editPool, [report, destination, kind] {
            QString error;
            if (kind == "bundle" || kind == "recovery") ReportBundle::write(report, destination, &error, kind == "recovery");
            else ReportPdf::write(report, destination, &error);
            return error;
        }));
        return;
    }
    // Large exports are staged to disk, never accumulated as a report-sized QByteArray.
    auto staged = QSharedPointer<QTemporaryDir>::create(cacheDir() + "/export-XXXXXX");
    if (!staged->isValid()) { status("Não foi possível preparar o ficheiro de exportação."); return; }
    // No open temporary-file handle: it would prevent QSaveFile's replacement on Windows.
    const auto stagedPath = staged->filePath("download");
    m_busy = true;
    emit changed();
    requestPhoto("/api/reports/" + m_current.value("id").toString() + "/" + kind,
        stagedPath, NetworkSecurity::responseLimit("/" + kind),
        [this, target, kind, staged, stagedPath](QSharedPointer<NetworkSecurity::FileBody> body) {
            if (body->writeFailed || body->exceeded || body->invalidRange ||
                (kind == "pdf" && !body->prefix.startsWith("%PDF-")) ||
                (kind == "bundle" && !body->prefix.startsWith("PK")) || !body->commit()) {
                m_busy = false;
                status(body->exceeded ? "A exportação excede o limite de 500 MiB." : "Não foi possível receber a exportação completa. O destino foi conservado.");
                return;
            }
            m_localOperationTitle = "A guardar exportação";
            status(m_localOperationTitle);
            auto watcher = new QFutureWatcher<QString>(this);
            connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, target, kind, staged] {
                const auto error = watcher->result(); watcher->deleteLater();
                m_busy = false; m_localOperationTitle.clear();
                if (error.isEmpty()) exportSaved(kind, target);
                else status(error);
            });
            const auto destination = localPath(target);
            watcher->setFuture(QtConcurrent::run(&m_editPool, [stagedPath, destination] {
                QFile source(stagedPath);
                if (!source.open(QIODevice::ReadOnly)) return QString("Não foi possível ler a exportação recebida.");
                if (destination.startsWith("content:")) {
                    QFile output(destination);
                    if (!output.open(QIODevice::WriteOnly) || !ReportBundle::copy(source, output) || !output.flush())
                        return QString("Não foi possível guardar o documento selecionado.");
                } else {
                    QSaveFile output(destination);
                    if (!output.open(QIODevice::WriteOnly) || !ReportBundle::copy(source, output) || !output.commit())
                        return QString("Não foi possível guardar o ficheiro.");
                }
                return QString();
            }));
        }, [this, staged](int, QString detail) {
            m_busy = false; status("Exportação interrompida: " + detail + ". Pode repetir; o destino foi conservado.");
        });
}

// A contract export owns its queue and worker. It never changes m_current or
// m_busy, and one report at a time bounds memory, disk staging and server load.
void Lem::exportContract(QString cid, QString kind, QUrl directory) {
    if (!desktopCorrections() || m_contractExportJob || (kind != "pdf" && kind != "bundle")) return;
    if (!online()) return;
    QString name;
    for (const auto value : m_contracts)
        if (value.toObject()["id"].toString() == cid) name = value.toObject()["name"].toString();
    if (name.isEmpty() || !SafeStorage::component(cid) || !directory.isLocalFile() ||
        !QFileInfo(directory.toLocalFile()).isDir()) {
        status("Escolha um contrato ativo e uma pasta local para exportar."); return;
    }
    QTemporaryDir output(QDir(directory.toLocalFile()).filePath(
        ReportNaming::component(name, "Contrato", 40) + (kind == "pdf" ? "_PDF_" : "_ZIP_") + "XXXXXX"));
    if (!output.isValid()) { status("Não foi possível criar a pasta de exportação."); return; }
    output.setAutoRemove(false); // Completed files survive cancellation, failures and app restarts.
    auto job = QSharedPointer<ContractExportJob>::create();
    job->contractId = cid; job->contractName = name; job->kind = kind; job->directory = output.path();
    job->token = m_token; job->server = m_server; m_contractExportJob = job;
    // Capture pending closures before requesting the catalogue. A sync ACK can
    // clear dirty while that older catalogue still lists the report as a draft.
    for (const auto value : m_reports) {
        const auto report = withContractPresentation(value.toObject());
        if (report["contract_id"].toString() == cid && report["status"] == "closed" &&
            report["owner_id"] == m_user["id"] && report["dirty"].toBool() &&
            !report["_conflict"].toBool() && !report.contains("_contract_unavailable") &&
            SafeStorage::component(report["id"].toString()) && report["correction"].toObject()["state"] != "editing")
            job->localClosures.append(report);
    }
    updateContractExportState("A obter os reports fechados do contrato…");
    auto fail = [this, job](int, QString detail) {
        if (m_contractExportJob == job) finishContractExport(detail.left(500));
    };
    auto reply = request("GET", "/api/contracts/" + cid + "/presentation", {},
        [this, job, fail](QByteArray data) {
            if (job->cancelled) { finishContractExport(); return; }
            job->presentation = QJsonDocument::fromJson(data).object();
            if (job->presentation["issuer"] != "Lem" && job->presentation["issuer"] != "CSMTEC") {
                finishContractExport("Formato do contrato inválido."); return;
            }
            auto reply = request("GET", "/api/contracts/" + job->contractId + "/chargers?include_detached=true", {},
                [this, job](QByteArray data) {
                    if (job->cancelled) { finishContractExport(); return; }
                    const auto doc = QJsonDocument::fromJson(data);
                    if (!doc.isArray()) { finishContractExport("Lista de reports inválida."); return; }
                    QSet<QString> included;
                    for (const auto row : doc.array()) {
                        for (const auto value : row.toObject()["reports"].toArray()) {
                            const auto report = value.toObject(); const auto id = report["id"].toString();
                            if (report["status"] != "closed" || included.contains(id)) continue;
                            if (!SafeStorage::component(id)) { finishContractExport("Identificação de report inválida."); return; }
                            job->queue.append(QJsonObject{{"id", id}}); included.insert(id);
                        }
                    }
                    // Also include the owner's locally closed, pending work. Never
                    // let another user's stale local draft hide a server closure.
                    for (const auto value : job->localClosures) {
                        auto report = value.toObject();
                        const auto id = report["id"].toString();
                        if (included.contains(id)) continue;
                        report["presentation"] = job->presentation;
                        job->queue.append(QJsonObject{{"id", id}, {"local", report}}); included.insert(id);
                    }
                    job->localClosures = {};
                    nextContractExport();
                }, "application/json", fail);
            connect(this, &Lem::contractExportCancelled, reply, &QNetworkReply::abort);
        }, "application/json", fail);
    connect(this, &Lem::contractExportCancelled, reply, &QNetworkReply::abort);
}

void Lem::updateContractExportState(QString detail) {
    const auto job = m_contractExportJob; if (!job) return;
    m_contractExportState = {{"active", true}, {"contract_id", job->contractId}, {"contract_name", job->contractName}, {"kind", job->kind},
        {"total", job->queue.size()}, {"processed", job->index}, {"saved", job->saved}, {"failed", job->failed},
        {"cancelling", job->cancelled}, {"directory", job->directory},
        {"directory_url", QUrl::fromLocalFile(job->directory).toString()}, {"message", detail}};
    emit changed();
}

void Lem::cancelContractExport() {
    const auto job = m_contractExportJob; if (!job) return;
    job->cancelled = true;
    updateContractExportState("A cancelar. Os ficheiros concluídos serão conservados.");
    // Only replies explicitly owned by this batch are connected to this signal.
    emit contractExportCancelled();
}

void Lem::nextContractExport() {
    const auto job = m_contractExportJob; if (!job) return;
    if (job->cancelled || job->index >= job->queue.size()) { finishContractExport(); return; }
    if (job->token != m_token || job->server != m_server) {
        finishContractExport("A sessão mudou. Entre novamente para exportar os restantes reports."); return;
    }
    const auto item = job->queue[job->index].toObject(); const auto id = item["id"].toString();
    updateContractExportState(QString("A exportar %1 / %2 · %3 guardados · %4 falhas")
        .arg(job->index + 1).arg(job->queue.size()).arg(job->saved).arg(job->failed));
    if (item.contains("local")) { renderContractExport(item["local"].toObject(), true); return; }
    auto reply = request("GET", "/api/reports/" + id, {}, [this, job, id](QByteArray data) {
        if (job->cancelled) { finishContractExport(); return; }
        auto report = QJsonDocument::fromJson(data).object();
        if (!readableReport(report) || report["id"].toString() != id ||
            report["contract_id"].toString() != job->contractId || report["status"] != "closed" ||
            report["correction"].toObject()["state"] == "editing") {
            completeContractExportItem(id, {}, {}, "O report já não está fechado/disponível neste contrato.", {}); return;
        }
        // Reuse only immutable evidence IDs from the same report, never cached
        // answers/status. A stale observer draft cannot downgrade this snapshot.
        QMap<QString, QJsonObject> cached;
        for (const auto value : m_reports.value(id).toObject()["photos"].toArray()) {
            const auto photo = value.toObject(); cached[photo["id"].toString()] = photo;
        }
        auto photos = report["photos"].toArray();
        for (int i = 0; i < photos.size(); ++i) {
            auto photo = photos[i].toObject(); const auto local = cached.value(photo["id"].toString());
            photo.remove("path");
            if (!local.isEmpty() && local["mime"] == photo["mime"] && QFileInfo::exists(localPath(QUrl(local["path"].toString()))))
                photo["path"] = local["path"];
            photos[i] = photo;
        }
        report["photos"] = photos;
        renderContractExport(report, false);
    }, "application/json", [this, id](int code, QString detail) {
        if (code == 401 || code == 423 || code == 426) { finishContractExport(detail.left(500)); return; }
        completeContractExportItem(id, {}, {}, detail.left(500), {});
    });
    connect(this, &Lem::contractExportCancelled, reply, &QNetworkReply::abort);
}

void Lem::renderContractExport(QJsonObject report, bool localOnly) {
    const auto job = m_contractExportJob; if (!job) return;
    if (job->cancelled) { finishContractExport(); return; }
    const auto problem = ReportPdf::validate(report);
    if (!problem.isEmpty()) {
        if (localOnly) completeContractExportItem(report["id"].toString(), ReportNaming::identifier(report), {}, problem, "local");
        else downloadContractExport(report);
        return;
    }
    // Local work uses a separate pool, so a large ZIP cannot delay saving answers.
    const auto staged = QSharedPointer<QTemporaryDir>::create(job->directory + "/.tmp-XXXXXX");
    if (!staged->isValid()) {
        completeContractExportItem(report["id"].toString(), {}, {}, "Não foi possível preparar o ficheiro.", "local"); return;
    }
    const auto path = staged->filePath("export");
    auto watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, job, report, staged, path, localOnly] {
        const auto error = watcher->result(); watcher->deleteLater();
        if (m_contractExportJob != job) return;
        // Evidence may be evicted/removed during an unrelated operation. Fall back
        // to the server without publishing an incomplete local file.
        if (!error.isEmpty() && !localOnly && !job->cancelled) { downloadContractExport(report); return; }
        completeContractExportItem(report["id"].toString(), ReportNaming::identifier(report), path, error, "local");
    });
    watcher->setFuture(QtConcurrent::run(&m_exportPool, [report, path, kind = job->kind] {
        QString error;
        if (kind == "pdf") ReportPdf::write(report, path, &error);
        else ReportBundle::write(report, path, &error);
        return error;
    }));
}

void Lem::downloadContractExport(QJsonObject report) {
    const auto job = m_contractExportJob; if (!job) return;
    const auto id = report["id"].toString(), name = ReportNaming::identifier(report);
    auto staged = QSharedPointer<QTemporaryDir>::create(job->directory + "/.tmp-XXXXXX");
    if (!staged->isValid()) { completeContractExportItem(id, name, {}, "Não foi possível preparar a descarga.", "server"); return; }
    const auto path = staged->filePath("export");
    requestPhoto("/api/reports/" + id + "/" + job->kind, path, NetworkSecurity::responseLimit("/" + job->kind),
        [this, job, id, name, staged, path](QSharedPointer<NetworkSecurity::FileBody> body) {
            QString error;
            if (body->writeFailed || body->exceeded || body->invalidRange ||
                (job->kind == "pdf" ? !body->prefix.startsWith("%PDF-") : !body->prefix.startsWith("PK")) || !body->commit())
                error = "Exportação incompleta ou inválida. Nenhum ficheiro parcial foi publicado.";
            completeContractExportItem(id, name, path, error, "server");
        }, [this, id, name, staged](int code, QString detail) {
            if (code == 401 || code == 423 || code == 426) { finishContractExport(detail.left(500)); return; }
            completeContractExportItem(id, name, {}, detail.left(500), "server");
        }, {}, {}, 0, [this, job] { return m_contractExportJob == job && !job->cancelled; }, true);
}

void Lem::completeContractExportItem(QString id, QString name, QString staged, QString error, QString source) {
    const auto job = m_contractExportJob; if (!job) return;
    if (job->cancelled) { finishContractExport(); return; }
    QString relative = name + (job->kind == "pdf" ? ".pdf" : ".zip");
    if (error.isEmpty()) {
        // Reports for the same charger and contract share a public filename.
        // A collision gets its own internal-ID subfolder.
        if (QFileInfo::exists(QDir(job->directory).filePath(relative))) {
            const auto folder = "report-" + id;
            if (!SafeStorage::component(id) || !QDir(job->directory).mkdir(folder)) error = "Nome de exportação já existente.";
            else relative = folder + "/" + relative;
        }
        if (error.isEmpty() && !QFile::rename(staged, QDir(job->directory).filePath(relative)))
            error = "Não foi possível guardar o ficheiro. O destino existente foi conservado.";
    }
    if (error.isEmpty()) ++job->saved; else ++job->failed;
    job->results.append(QJsonObject{{"id", id}, {"identifier", name}, {"status", error.isEmpty() ? "saved" : "failed"},
        {"file", error.isEmpty() ? relative : QString()}, {"source", source}, {"error", error}});
    ++job->index;
    QTimer::singleShot(0, this, &Lem::nextContractExport);
}

void Lem::finishContractExport(QString detail) {
    const auto job = m_contractExportJob; if (!job) return;
    const bool partial = job->cancelled || !detail.isEmpty() || job->index < job->queue.size();
    const auto message = QString("%1: %2 ficheiros guardados, %3 falhas, %4 por exportar.%5")
        .arg(partial ? "Exportação interrompida" : "Exportação concluída").arg(job->saved).arg(job->failed)
        .arg(job->queue.size() - job->index).arg(detail.isEmpty() ? QString() : " " + detail);
    updateContractExportState(message);
    m_contractExportState["active"] = false;
    m_contractExportState["state"] = partial ? "interrupted" : job->failed ? "partial" : "complete";
    QJsonArray remaining;
    for (int i = job->index; i < job->queue.size(); ++i) remaining.append(job->queue[i].toObject()["id"]);
    QJsonObject summary{{"contract_id", job->contractId}, {"kind", job->kind}, {"message", message},
                        {"results", job->results}, {"remaining", remaining}};
    QSaveFile manifest(QDir(job->directory).filePath("resumo-exportacao.json"));
    const auto bytes = QJsonDocument(summary).toJson(QJsonDocument::Indented);
    if (QFileInfo::exists(manifest.fileName()) || !manifest.open(QIODevice::WriteOnly) ||
        manifest.write(bytes) != bytes.size() || !manifest.commit())
        m_contractExportState["message"] = message + " Não foi possível guardar o resumo na pasta.";
    m_contractExportJob.clear();
    emit changed();
}

void Lem::createContract(QString company, int year, int semester, QString kind) {
    if (m_busy || !online())
        return;
    m_busy = true;
    emit changed();
    request("POST", "/api/contracts",
            json({{"company", company}, {"year", year}, {"semester", semester}, {"kind", kind}}),
            [this](QByteArray) {
                m_busy = false;
                refresh();
            });
}

void Lem::deleteContract(QString id, QString confirmation) {
    if (switchingBlocked() || m_user["role"].toString() != "admin" ||
        confirmation != "APAGAR" || id.isEmpty() || !online() || !flushEdits())
        return;
    m_busy = true;
    emit changed();
    request("POST", "/api/contracts/" + id + "/trash", json({{"confirmation", confirmation}}),
            [this, id](QByteArray) {
                suspendContractSync(id, "Contrato em espera de eliminação durante 30 dias. Sincronização suspensa até recuperação pelo administrador.");
                if (m_contract == id) {
                    m_current = {};
                    m_contract.clear();
                    m_chargers = {};
                    m_preview = {};
                }
                m_catalogues.remove(id);
                m_historicalNotes.remove(id);
                m_catalogueTimes.remove(id);
                for (int i = m_contracts.size() - 1; i >= 0; --i)
                    if (m_contracts[i].toObject()["id"].toString() == id)
                        m_contracts.removeAt(i);
                m_busy = false;
                persist();
                status("Contrato apagado. Pode recuperá-lo na Gestão durante 30 dias.");
            });
}

void Lem::loadDeletedContracts() {
    if (switchingBlocked() || m_user["role"].toString() != "admin" || !online())
        return;
    m_deletedContracts = {};
    emit contractTrashChanged();
    m_busy = true;
    emit changed();
    request("GET", "/api/admin/deleted-contracts", {}, [this](QByteArray data) {
        m_deletedContracts = QJsonDocument::fromJson(data).array();
        m_busy = false;
        emit contractTrashChanged();
        emit changed();
    });
}

void Lem::restoreContract(QString id) {
    if (switchingBlocked() || m_user["role"].toString() != "admin" || id.isEmpty() || !online())
        return;
    m_busy = true;
    emit changed();
    request("POST", "/api/contracts/" + id + "/restore", "{}", [this, id](QByteArray) {
        for (int i = m_deletedContracts.size() - 1; i >= 0; --i)
            if (m_deletedContracts[i].toObject()["id"].toString() == id)
                m_deletedContracts.removeAt(i);
        m_busy = false;
        emit contractTrashChanged();
        status("Contrato recuperado com os seus carregadores, reports e fotografias.");
        refresh();
    });
}

// Keep the workbook bytes in memory for analysis, preview and eventual commit.
void Lem::analyseExcel(QUrl url) {
    beginExcelImport(url, false);
}

void Lem::analyseStationExcel(QUrl url) {
    if (canManageStations()) beginExcelImport(url, true);
}

void Lem::analyseContractIds(QUrl url, QString contractId) {
    if (!contractId.isEmpty() && contractId == m_contract)
        beginExcelImport(url, false, contractId);
}

void Lem::beginExcelImport(QUrl url, bool stationsOnly, QString idsContract) {
    if (m_busy || m_user["role"] != "admin" || !online())
        return;
    QFile f(localPath(url));
    if (!f.open(QIODevice::ReadOnly)) {
        status("Não foi possível abrir o Excel.");
        return;
    }
    const auto excel = f.read(20 * 1024 * 1024 + 1);
    if (excel.size() > 20 * 1024 * 1024) {
        status("Máximo 20 MB.");
        return;
    }
    if (excel.isEmpty() || f.error() != QFile::NoError) {
        status("Não foi possível ler o Excel.");
        return;
    }
    m_excel = excel;
    m_excelName = "import.xlsx";
    m_excelImportPath.clear();
    m_importInfo = {};
    m_preview = {};
    m_busy = true;
    status("A analisar o Excel…");
    multipart("POST", "/api/imports/analyse", {}, m_excelName, m_excel, [this, stationsOnly, idsContract](QByteArray data) {
        m_importInfo = QJsonDocument::fromJson(data).object();
        if (stationsOnly || !idsContract.isEmpty()) {
            if (stationsOnly) m_importInfo["station_catalogue"] = true;
            else m_importInfo["ids_contract"] = idsContract;
            m_importInfo["batch_id"] = uuid();
            m_busy = false;
            status("Excel analisado. Pré-visualize os IDs antes de importar.");
            return;
        }
        QString company;
        for (auto c : m_contracts)
            if (c.toObject()["id"].toString() == m_contract)
                company = c.toObject()["company"].toString();
        request("GET",
                "/api/companies/" + QString::fromLatin1(QUrl::toPercentEncoding(company)) +
                    "/import-profile",
                {}, [this](QByteArray profile) {
                    m_importInfo["saved_profile"] = QJsonDocument::fromJson(profile).object();
                    m_importInfo["batch_id"] = uuid();
                    m_busy = false;
                    status("Excel analisado. Pré-visualize os carregadores antes de importar.");
                });
    });
}

// Resolve cached contract identity and overlay the newest received presentation.
QJsonObject Lem::withContractPresentation(QJsonObject report) const {
    auto cid = report["contract_id"].toString();
    if (cid.isEmpty()) cid = report["snapshot"].toObject()["contract"].toObject()["id"].toString();
    const auto chargerId = report["charger_id"].toString();
    if (cid.isEmpty() && !chargerId.isEmpty()) {
        QSet<QString> matches;
        for (const auto &key : m_catalogues.keys())
            for (auto charger : m_catalogues[key].toArray())
                if (charger.toObject()["id"].toString() == chargerId && !key.isEmpty()) matches.insert(key);
        // Older caches stored only the selected charger's catalogue.
        for (auto charger : m_chargers) {
            const auto row = charger.toObject();
            if (row["id"].toString() != chargerId) continue;
            const auto found = row["contract_id"].toString(m_contract);
            if (!found.isEmpty()) matches.insert(found);
        }
        if (matches.size() == 1) cid = *matches.cbegin();
    }
    if (!cid.isEmpty()) report["contract_id"] = cid;
    for (auto entry : m_contracts) {
        const auto contract = entry.toObject();
        const auto style = contract["presentation"].toObject();
        if (contract["id"].toString() == cid && !style.isEmpty() &&
            style["revision"].toInt() >= report["presentation"].toObject()["revision"].toInt()) {
            report["contract_id"] = cid;
            report["presentation"] = style;
        }
    }
    return report;
}

void Lem::applyContractPresentations() {
    // Presentation is contract metadata: never change answers, owner or dirty/revision state.
    for (const auto &id : m_reports.keys()) m_reports[id] = withContractPresentation(m_reports[id].toObject());
    for (const auto &id : m_pendingEdits.keys()) m_pendingEdits[id] = withContractPresentation(m_pendingEdits[id].toObject());
    if (!m_current.isEmpty()) m_current = withContractPresentation(m_current);
}

void Lem::receiveContractPresentation(const QString &cid, const QJsonObject &style) {
    if (style["issuer"] != "Lem" && style["issuer"] != "CSMTEC") return;
    for (int i = 0; i < m_contracts.size(); ++i) {
        auto contract = m_contracts[i].toObject();
        if (contract["id"].toString() != cid ||
            style["revision"].toInt() < contract["presentation"].toObject()["revision"].toInt()) continue;
        contract["presentation"] = style;
        contract["report_logos"] = style["client_logos"];
        m_contracts[i] = contract;
    }
    applyContractPresentations();
    const auto apply = [this, &cid, &style](QJsonObject report) {
        report = withContractPresentation(report);
        if (report["contract_id"].toString() == cid &&
            style["revision"].toInt() >= report["presentation"].toObject()["revision"].toInt()) report["presentation"] = style;
        return report;
    };
    for (const auto &id : m_reports.keys()) m_reports[id] = apply(m_reports[id].toObject());
    for (const auto &id : m_pendingEdits.keys()) m_pendingEdits[id] = apply(m_pendingEdits[id].toObject());
    if (!m_current.isEmpty()) m_current = apply(m_current);
}

void Lem::saveReportPresentation(QString issuer, QString primary, QString secondary, int revision) {
    const auto cid = m_contract;
    if (!canSaveReportPresentation()) {
        const auto message = m_user["role"] != "admin" ? "Só um administrador pode guardar o formato dos reports." :
            !authenticated() ? "Inicie sessão para guardar o formato no servidor." :
            cid.isEmpty() ? "Selecione o contrato antes de guardar o formato." :
            "Aguarde a operação em curso e volte a guardar o formato.";
        setPresentationSaveState(cid, "error", message);
        return;
    }
    QJsonArray logos;
    if (!primary.isEmpty()) logos.append(primary);
    if (!secondary.isEmpty()) logos.append(secondary);
    m_busy = true;
    setPresentationSaveState(cid, "saving", "A guardar o formato no servidor…");
    // A successful HTTP response is not sufficient: read the committed settings
    // independently, including after a lost acknowledgement or a safe retry.
    const auto confirm = [this, cid, issuer, logos, revision](int acknowledgedRevision) {
        setPresentationSaveState(cid, "saving", "A confirmar o formato guardado no servidor…");
        request("GET", "/api/contracts/" + cid + "/presentation", {},
            [this, cid, issuer, logos, revision, acknowledgedRevision](QByteArray data) {
                const auto saved = QJsonDocument::fromJson(data).object();
                const auto savedIssuer = saved["issuer"].toString();
                const int savedRevision = saved["revision"].toInt(-1);
                QJsonArray savedLogos;
                for (auto logo : saved["client_logos"].toArray()) savedLogos.append(logo.toObject()["id"]);
                const bool valid = (savedIssuer == "Lem" || savedIssuer == "CSMTEC") &&
                    savedRevision >= qMax(revision, acknowledgedRevision) && saved["client_logos"].isArray();
                m_busy = false;
                if (!valid || savedIssuer != issuer || savedLogos != logos) {
                    if (valid) { receiveContractPresentation(cid, saved); persist(); }
                    setPresentationSaveState(cid, "error", valid ?
                        "Gravação não confirmada. O servidor mantém " + savedIssuer + ". Verifique as opções e volte a guardar." :
                        "Resposta inválida ao confirmar o formato. Atualize o servidor e tente novamente.");
                    return;
                }
                receiveContractPresentation(cid, saved);
                m_preview = {};
                if (!persist()) {
                    setPresentationSaveState(cid, "warning", "Guardado no servidor: " + issuer + ". Falhou a gravação neste dispositivo; volte a atualizar o contrato.");
                    return;
                }
                setPresentationSaveState(cid, "saved", "Guardado no servidor: " + issuer + ".");
                refresh();
            }, "application/json", [this, cid](int, QString) {
                m_busy = false;
                setPresentationSaveState(cid, "error", "Não foi possível confirmar a gravação no servidor. Atualize o contrato antes de exportar ou volte a guardar.");
            });
    };
    request("PUT", "/api/contracts/" + cid + "/presentation",
        json({{"issuer", issuer}, {"client_logos", logos}, {"base_revision", revision}}),
        [confirm](QByteArray data) {
            confirm(QJsonDocument::fromJson(data).object()["revision"].toInt(-1));
        }, "application/json", [this, cid, confirm](int code, QString message) {
            if (code == 0 || code == 409 || code >= 500) { confirm(-1); return; }
            m_busy = false;
            const auto detail = QJsonDocument::fromJson(message.toUtf8()).object()["detail"].toString();
            setPresentationSaveState(cid, "error", "Não foi possível guardar o formato no servidor. " +
                (detail.isEmpty() ? "Atualize o contrato e confirme a sessão e a ligação." : detail));
        });
}

void Lem::setPresentationSaveState(const QString &cid, const QString &state, const QString &message) {
    m_reportPresentationSave = QJsonObject{{"contract_id", cid}, {"state", state}, {"message", message}};
    status(message);
}

QVariantList Lem::reportBrands() const {
    QFile file(":/branding/report-logos.json");
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).array().toVariantList();
}

void Lem::previewExcel(QString sheet, int row, QVariantMap columns, QString primary, QString secondary, QString issuer, int brandingRevision) {
    if (m_contract.isEmpty() || m_importInfo["station_catalogue"].toBool()) return;
    QJsonObject mapping{
        {"sheet", sheet}, {"header_row", row}, {"columns", QJsonObject::fromVariantMap(columns)}};
    QJsonArray logos;
    if (!primary.isEmpty()) logos.append(primary);
    if (!secondary.isEmpty()) logos.append(secondary);
    mapping["report_logos"] = logos;
    if (!issuer.isEmpty()) mapping["report_issuer"] = issuer;
    if (brandingRevision >= 0) mapping["branding_revision"] = brandingRevision;
    previewImport("/api/contracts/" + m_contract + "/import", mapping);
}

void Lem::previewContractIds(QString contractId, QString sheet, int row, QVariantMap columns) {
    if (contractId.isEmpty() || contractId != m_contract || m_importInfo["ids_contract"] != contractId) return;
    previewImport("/api/contracts/" + contractId + "/import",
                  {{"sheet", sheet}, {"header_row", row}, {"columns", QJsonObject::fromVariantMap(columns)}});
}

void Lem::previewStationExcel(QString sheet, int row, QVariantMap columns) {
    if (!canManageStations() || !m_importInfo["station_catalogue"].toBool()) return;
    previewImport("/api/stations/import",
                  {{"sheet", sheet}, {"header_row", row}, {"columns", QJsonObject::fromVariantMap(columns)}});
}

void Lem::previewImport(const QString &path, QJsonObject mapping) {
    if (m_busy || m_user["role"] != "admin" || m_excel.isEmpty() || !online()) return;
    m_mapping = mapping;
    m_excelImportPath = path;
    m_preview = {};
    m_busy = true;
    status("A preparar a pré-visualização…");
    multipart("POST", path, {{"mapping", QString::fromUtf8(json(m_mapping))}, {"commit", "false"}},
              m_excelName, m_excel, [this, path](QByteArray data) {
                  m_busy = false;
                  const auto preview = QJsonDocument::fromJson(data).object();
                  if (!preview["records"].isArray() || !preview["errors"].isArray() ||
                      (path.startsWith("/api/contracts/") && QUuid(preview["operation_id"].toString()).isNull())) {
                      status("O servidor devolveu uma pré-visualização inválida.");
                      return;
                  }
                  m_preview = preview;
                  status("Pré-visualização pronta. Confira os IDs antes de importar.");
              });
}

void Lem::commitExcel() {
    const bool stationsOnly = m_importInfo["station_catalogue"].toBool();
    const auto path = stationsOnly ? QString("/api/stations/import") : "/api/contracts/" + m_contract + "/import";
    if (m_busy || m_user["role"] != "admin" || m_excelImportPath != path ||
        (stationsOnly ? !canManageStations() : m_contract.isEmpty()) ||
        m_preview["records"].toArray().isEmpty() || !m_preview["errors"].toArray().isEmpty() || !online())
        return;
    m_busy = true;
    status("A importar o Excel…");
    QList<QPair<QString, QString>> fields{{"mapping", QString::fromUtf8(json(m_mapping))}, {"commit", "true"}};
    if (!stationsOnly) fields.append({"operation_id", m_preview["operation_id"].toString()});
    multipart("POST", path, fields,
              m_excelName, m_excel, [this, stationsOnly](QByteArray data) {
                  m_busy = false;
                  const auto outcome = QJsonDocument::fromJson(data).object();
                  if (outcome["created"].toInt(-1) < 0 || outcome["updated"].toInt(-1) < 0) {
                      status("Confirmação da importação inválida. Repita a confirmação desta pré-visualização.");
                      return;
                  }
                  m_preview = {};
                  m_importInfo = {};
                  m_excelImportPath.clear();
                  m_excel.clear();
                  m_catalogueTimes = {};
                  const auto summary = QString("Importação concluída. %1 criados, %2 atualizados.")
                         .arg(outcome["created"].toInt()).arg(outcome["updated"].toInt());
                  status(summary);
                  emit excelImported(summary);
                  refreshStations();
                  if (!stationsOnly) refresh();
              });
}
void Lem::createUser(QString email, QString name, QString password, QString role) {
    if (m_busy || m_user["role"].toString() != "admin" ||
        (role != "admin" && role != "tech") || !online())
        return;
    m_busy = true;
    emit changed();
    request("POST", "/api/users",
            json({{"email", email}, {"name", name}, {"password", password}, {"role", role}}),
            [this](QByteArray data) {
                m_busy = false;
                Q_UNUSED(data);
                loadUsers();
            });
}
bool Lem::changeOwnPassword(QString current, QString replacement, QString confirmation) {
    // Count Unicode characters as the server does, including supplementary characters.
    const auto length = replacement.toUcs4().size();
    if (!authenticated() || m_busy || current.isEmpty() || current.toUcs4().size() > 200 ||
        length < 12 || length > 200 || confirmation != replacement)
        return false;

    m_busy = true;
    emit changed();
    const auto id = m_user.value("id").toString();
    const auto finish = [this, token = m_token](bool saved, const QString &message) {
        m_busy = false;
        emit changed();
        if (m_token == token)
            emit ownPasswordChangeFinished(saved, message);
    };
    request("PUT", "/api/account/password",
        json({{"current_password", current}, {"new_password", replacement}, {"confirmation", confirmation}}),
        [id, finish](QByteArray data) {
            const auto ack = QJsonDocument::fromJson(data).object();
            const bool saved = ack["ok"].toBool() && ack["user_id"].toString() == id;
            finish(saved, saved ? "Password alterada. A sessão neste dispositivo continua ativa."
                                : "O servidor não confirmou a alteração. Verifique a password antes de repetir.");
        }, "application/json", [finish](int code, QString) {
            // Only display local messages. Older or misconfigured servers may echo inputs.
            QString message;
            switch (code) {
            case 403: message = "Password atual incorreta."; break;
            case 404: case 405: message = "Atualize o servidor para poder alterar a sua password."; break;
            case 422: message = "Verifique a nova password e a confirmação."; break;
            case 429: message = "Demasiadas tentativas. Aguarde um minuto."; break;
            default: message = "Não foi possível confirmar a alteração. Verifique a ligação e a password antes de repetir.";
            }
            finish(false, message);
        });
    return true;
}

bool Lem::resetTechnicianPassword(QString id, QString password) {
    if (m_busy || m_user["role"] != "admin" || !online()) return false;
    bool technician = false;
    for (const auto value : m_users) {
        const auto user = value.toObject();
        if (user["id"].toString() == id && user["role"] == "tech") technician = true;
    }
    if (!technician || password.size() < 12 || password.size() > 200) {
        status("Escolha um técnico e uma password entre 12 e 200 caracteres."); return false;
    }
    m_busy = true;
    m_passwordReset = {{"state", "saving"}, {"user_id", id}, {"message", "A repor a password do técnico…"}};
    emit changed();
    request("PUT", "/api/users/" + id + "/password", json({{"password", password}}),
        [this, id](QByteArray data) {
            const auto ack = QJsonDocument::fromJson(data).object();
            const bool saved = ack["ok"].toBool() && ack["user_id"].toString() == id;
            m_busy = false;
            const auto message = saved ? "Password do técnico alterada. Deve entrar novamente com a nova password."
                                       : "Não foi possível confirmar a alteração da password.";
            m_passwordReset = {{"state", saved ? "saved" : "error"}, {"user_id", id}, {"message", message}};
            status(message);
        }, "application/json", [this, id](int code, QString) {
            // Do not reflect request/validation bodies into UI or logs: credentials
            // are never part of the durable account list or operation result.
            m_busy = false;
            const auto message = code == 404 ? "Técnico inexistente ou servidor sem suporte. Atualize a lista e o servidor."
                : code == 403 ? "Operação recusada. Apenas administradores podem repor passwords de técnicos."
                : code == 401 ? "Sessão expirada. Entre novamente."
                : "Não foi possível confirmar a alteração. Verifique a ligação antes de tentar novamente.";
            m_passwordReset = {{"state", "error"}, {"user_id", id}, {"message", message}};
            status(message);
        });
    return true;
}

void Lem::assignUser(QString id) {
    if (m_busy || !online() || m_contract.isEmpty())
        return;
    m_busy = true;
    emit changed();
    request("PUT", "/api/contracts/" + m_contract + "/users/" + id, "{}", [this](QByteArray) {
        m_busy = false;
        status("Técnico associado ao contrato.");
    });
}

// Build fictitious local data under a separate demo server/user scope; no live API calls.
void Lem::demo() {
    if (switchingBlocked() || !flushEdits())
        return;
    clearSession();
    m_server = "demo://local";
    m_clientCompatibility = {};
    emit compatibilityChanged();
    m_user = {{"id", "demonstracao"},
              {"name", "Técnico de demonstração"},
              {"role", "admin"},
              {"email", "demo@local"}};
    loadUser();
    if (m_localReadError) return;
    QString cid = "demo-contract", chargerId = "demo-charger";
    m_contract = cid;
    m_contracts = QJsonArray{QJsonObject{{"id", cid},
                                         {"name", "2026_1ºS_Exemplo_TRE"},
                                         {"company", "Exemplo"},
                                         {"year", 2026},
                                         {"semester", 1},
                                         {"kind", "TRE"}}};
    m_chargers =
        QJsonArray{QJsonObject{{"id", chargerId},
                               {"contract_id", cid},
                               {"external_id", "PCVE-DEMO-001"},
                               {"details", QJsonObject{{"model", "DC 60 kW"},
                                                       {"serial", "000012"},
                                                       {"address", "Dados fictícios"},
                                                       {"cpe", "CPE-DEMO"},
                                                       {"manufacturer", "Exemplo"},
                                                       {"power", "60 kW"}}},
                               {"reports", QJsonArray{}}}};
    m_catalogues[cid] = m_chargers;
    const auto charger = m_chargers.first().toObject();
    m_stations = QJsonArray{QJsonObject{{"id", "demo-station"}, {"external_id", charger["external_id"]},
                                       {"details", charger["details"]}}};
    m_stationCatalogueLoaded = true;
    if (openReport(chargerId, ""))
        status("DEMONSTRAÇÃO LOCAL • dados fictícios; sem ligação ao servidor.");
}

// Administrator account listing for management; loadReportOwners lists eligible owners instead.
void Lem::loadUsers() {
    if (m_busy || !online() || m_user["role"].toString() != "admin")
        return;
    m_busy = true;
    emit changed();
    request("GET", "/api/users", {}, [this](QByteArray data) {
        m_users = QJsonDocument::fromJson(data).array();
        for (int i = 0; i < m_users.size(); i++) {
            auto u = m_users[i].toObject();
            u["label"] = u["name"].toString() + " • " + u["email"].toString() +
                         (u["role"].toString() == "admin" ? " • Administrador" : " • Técnico");
            m_users[i] = u;
        }
        m_busy = false;
        status("Utilizadores atualizados.");
    });
}

QVariantList Lem::operationTransfers() const {
    QVariantList out;
    for (const auto &t : m_transfers)
        out.append(t);
    return out;
}

// Choose one summary for the status panel. Batch fractions and byte fractions are distinct.
QVariantMap Lem::operationStatus() const {
    const bool active = !m_transfers.isEmpty() || m_syncing || downloadingPhotos() || saving() ||
                        m_busy || m_refreshing || !m_qveCopyTarget.isEmpty() || attachingFiles();
    QString title;
    double progress = 0;
    bool unknown = active;
    if (downloadingPhotos()) {
        title = m_photoBatchState;
        progress = m_photoBatchTotal ? double(m_photoBatchDone) / m_photoBatchTotal : 0;
        unknown = false;
    } else if (m_syncing) {
        title = m_syncState;
        progress = m_syncTotal ? double(m_syncCompleted) / m_syncTotal : 0;
        unknown = false;
    } else if (!m_transfers.isEmpty()) {
        const auto t = m_transfers.first();
        title = t["title"].toString();
        progress = t["progress"].toDouble();
        unknown = t["indeterminate"].toBool();
    } else if (!m_qveCopyTarget.isEmpty())
        title = m_copyScope == "QVE" ? "A copiar QVE e evidências" : "A copiar medições e evidências";
    else if (attachingFiles())
        title = "A adicionar evidências selecionadas";
    else if (m_busy || m_refreshing)
        title = m_localOperationTitle.isEmpty() ? "A processar dados recebidos do servidor"
                                                : m_localOperationTitle;
    else if (saving())
        title = m_localSaveError ? "Falha na gravação local · nova tentativa pendente"
                                 : "A guardar respostas no dispositivo";
    else {
        title = m_lastTransfer.isEmpty() ? "Sem transferências em curso"
                                         : m_lastTransfer["title"].toString();
        progress = m_lastTransfer["success"].toBool() ? 1 : 0;
    }
    return {{"active", active},           {"title", title},
            {"detail", m_message},        {"progress", qBound(0.0, progress, 1.0)},
            {"indeterminate", unknown},   {"reportsDone", m_syncCompleted},
            {"reportsTotal", m_syncTotal}};
}

// Track transport bytes separately from domain progress. An uploaded body may still
// be awaiting server validation/commit; throttle notifications without rebuilding QML models.
void Lem::trackReply(QNetworkReply *reply, QString method, QString path, bool upload) {
    const auto id = ++m_transferSerial;
    QString title = "Comunicar com o servidor", context;
    if (path.contains("/photos/"))
        title = method == "GET"      ? "Descarregar fotografia"
                : method == "DELETE" ? "Remover fotografia do servidor"
                                     : "Enviar fotografia";
    else if (path.startsWith("/api/reports/"))
        title = method == "GET"      ? (path.endsWith("/pdf")      ? "Gerar e descarregar PDF"
                                        : path.endsWith("/bundle") ? "Preparar e descarregar ZIP"
                                                                   : "Carregar dados do report")
                : method == "DELETE" ? "Apagar report no servidor"
                                     : "Enviar respostas do report";
    else if (path == "/api/login")
        title = "Iniciar sessão";
    else if (path.endsWith("/chargers"))
        title = "Descarregar catálogo de carregadores";
    else if (path == "/api/contracts")
        title = method == "GET" ? "Descarregar contratos" : "Criar contrato";
    else if (path.contains("/imports/analyse"))
        title = "Enviar e analisar Excel";
    else if (path.endsWith("/import"))
        title = "Validar/importar carregadores do Excel";
    else if (path.contains("/users"))
        title = method == "GET" ? "Carregar utilizadores" : "Guardar utilizador/acesso";
    else if (path.endsWith("/import-profile"))
        title = "Carregar perfil de importação";
    else if (path == "/api/logout")
        title = "Terminar sessão";
    if (path.startsWith("/api/reports/")) {
        const auto rid = path.section('/', 3, 3).section('?', 0, 0);
        const auto report = m_reports.value(rid).toObject();
        context = report["snapshot"].toObject()["charger"].toObject()["external_id"].toString(
            rid.left(8));
        if (path.contains("/photos/")) {
            const auto pid = path.section('/', 5, 5).section('?', 0, 0);
            for (auto p : report["photos"].toArray())
                if (p.toObject()["id"].toString() == pid)
                    context += " · " + p.toObject()["checkpoint"].toString();
        }
    }
    m_transfers[id] = {{"title", title},
                       {"context", context},
                       {"direction", upload ? "upload" : "download"},
                       {"done", 0.0},
                       {"total", -1.0},
                       {"progress", 0.0},
                       {"indeterminate", true},
                       {"waiting", false}};
    emit operationChanged();
    auto update = [this, id, upload](qint64 done, qint64 total) {
        if (!m_transfers.contains(id))
            return;
        auto &t = m_transfers[id];
        const bool waiting = upload && total > 0 && done >= total;
        t["done"] = double(done);
        t["total"] = double(total);
        t["progress"] = total > 0 ? qBound(0.0, double(done) / total, 1.0) : 0;
        t["waiting"] = waiting;
        t["indeterminate"] = total <= 0 || waiting;
        if (!m_operationNotify.isActive())
            m_operationNotify.start();
    };
    if (upload)
        connect(reply, &QNetworkReply::uploadProgress, this, update);
    else
        connect(reply, &QNetworkReply::downloadProgress, this, update);
    connect(reply, &QNetworkReply::finished, this, [this, reply, id] {
        if (!m_transfers.contains(id))
            return;
        m_lastTransfer = m_transfers.take(id);
        const bool ok = reply->error() == QNetworkReply::NoError;
        m_lastTransfer["success"] = ok;
        m_lastTransfer["title"] =
            m_lastTransfer["title"].toString() + (ok ? " · concluído" : " · falhou");
        emit operationChanged();
    });
}
