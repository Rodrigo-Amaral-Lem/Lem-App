// QML-facing application controller: identity, local reports, user actions and HTTP.
// The same Lem object is implemented across this file and LemOffline.cpp.
// State belongs to the QObject thread; callbacks explicitly reconcile snapshots.
// Start reading at docs/architecture.md for state, persistence and source ownership.

#include "Lem.h"
#include "NetworkSecurity.h"
#include "SafeStorage.h"
#include "EvidenceMedia.h"
#include <cmath>
#include "Measurements.h"
#include "ReportPdf.h"
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
QString uuid() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
} // namespace

// Connect independent notification channels before loading cached data.
// The per-report writer has one worker so older writes cannot finish after newer ones.
Lem::Lem(QString root, QObject *parent) : QObject(parent) {
    m_operationNotify.setSingleShot(true);
    m_operationNotify.setInterval(100);
    connect(&m_operationNotify, &QTimer::timeout, this, &Lem::operationChanged);
    connect(this, &Lem::changed, this, &Lem::operationChanged);
    connect(this, &Lem::localSaveStateChanged, this, &Lem::operationChanged);
    m_editPool.setMaxThreadCount(1);
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
        for (auto it = m_reports.constBegin(); it != m_reports.constEnd(); ++it) {
            const auto r = it.value().toObject();
            reportSummaries.append(
                QJsonArray{r["id"], r["charger_id"], r["status"], r["owner_id"], r["dirty"]});
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
    if (QNetworkInformation::loadDefaultBackend()) {
        connect(QNetworkInformation::instance(), &QNetworkInformation::reachabilityChanged, this,
                [this](QNetworkInformation::Reachability r) {
                    if (r == QNetworkInformation::Reachability::Online) {
                        m_retryNotBefore = 0;
                        scheduleSync(1500);
                    }
                });
    }
    if (qGuiApp)
        connect(qGuiApp, &QGuiApplication::applicationStateChanged, this,
                [this](Qt::ApplicationState state) {
                    if (state == Qt::ApplicationActive) {
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
    QFile session(m_root + "/last-user.json");
    if (session.open(QIODevice::ReadOnly)) {
        auto o = QJsonDocument::fromJson(session.readAll()).object();
        if (o["server"].toString() == m_server)
            m_user = o["user"].toObject();
    }
    if (!m_user.isEmpty()) {
        loadUser();
        m_message = "Dados locais disponíveis. Entre para sincronizar.";
    }
}
Lem::~Lem() {
    flushEdits();
    m_editPool.waitForDone();
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
    m_server = s;
    m_clientCompatibility = {};
    emit compatibilityChanged();
    m_token.clear();
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
    m_autoSync.stop();
    m_syncConflicts.clear();
    m_retryNotBefore = 0;
    m_catalogueTimes = {};
    m_deletedReports.clear();
    m_contracts = {};
    m_chargers = {};
    m_reports = {};
    m_current = {};
    m_catalogues = {};
    m_contract.clear();
    QDir().mkpath(cacheDir());
    QFile f(cacheDir() + "/cache.json");
    if (f.open(QIODevice::ReadOnly)) {
        auto o = QJsonDocument::fromJson(f.readAll()).object();
        m_contracts = o["contracts"].toArray();
        m_chargers = o["chargers"].toArray();
        m_reports = o["reports"].toObject();
        m_contract = o["contract"].toString();
        m_catalogues = o["catalogues"].toObject();
        m_catalogueTimes = o["catalogue_times"].toObject();
        for (auto id : o["deleted_reports"].toArray())
            m_deletedReports.insert(id.toString());
        // Earlier versions could store null/ID-less placeholders (see releasePhotoSync).
        // A real report is always an object stored under its own ID.
        for (const auto &key : m_reports.keys())
            // Only discard empty placeholders; retain non-empty damaged records
            // for recovery instead of silently deleting potentially unsent work.
            if (m_reports.value(key).isNull() ||
                (m_reports.value(key).isObject() && m_reports.value(key).toObject().isEmpty()))
                m_reports.remove(key);
    }
    // Small per-report edit journals are newer than an older full-cache checkpoint.
    const QDir edits(cacheDir() + "/edits");
    for (const auto &name : edits.entryList({"*.json"}, QDir::Files | QDir::NoSymLinks)) {
        QFile file(edits.filePath(name));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const auto report = QJsonDocument::fromJson(file.readAll()).object();
        const auto id = report["id"].toString();
        if (!id.isEmpty() && !m_deletedReports.contains(id) && report["owner_id"] == m_user["id"] &&
            report["_edit_seq"].toDouble() > m_reports[id].toObject()["_edit_seq"].toDouble())
            m_reports[id] = report;
    }
    cleanPhotoCache();
    emit changed();
}

// Checkpoint all local state atomically after flushing small per-report journals.
// Remove journals only after the full checkpoint has committed successfully.
bool Lem::persist() {
    if (m_user.isEmpty() || !flushEdits())
        return false;
    QSaveFile f(cacheDir() + "/cache.json");
    if (!f.open(QIODevice::WriteOnly)) {
        status("Não foi possível gravar o cache local: " + f.errorString());
        return false;
    }
    QJsonArray removed;
    for (const auto &id : m_deletedReports)
        removed.append(id);
    f.write(json({{"deleted_reports", removed},
                  {"contracts", m_contracts},
                  {"chargers", m_chargers},
                  {"reports", m_reports},
                  {"contract", m_contract},
                  {"catalogues", m_catalogues},
                  {"catalogue_times", m_catalogueTimes}}));
    if (!f.commit()) {
        status("Erro ao gravar os dados locais: " + f.errorString());
        return false;
    }
    for (auto it = m_reports.begin(); it != m_reports.end(); ++it) {
        const auto name = QString::fromLatin1(
            QCryptographicHash::hash(it.key().toUtf8(), QCryptographicHash::Sha256).toHex());
        QFile::remove(cacheDir() + "/edits/" + name + ".json");
    }
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
        status(
            "Entre com email e password para comunicar com o servidor. Os rascunhos locais foram conservados.");
        return false;
    }
    return true;
}

// Send JSON or another single-body HTTP request and track its operation progress.
// A caller-supplied failure handler owns recovery; otherwise release the busy UI state.
void Lem::request(QString method, QString path, QByteArray body, Callback cb, QString content,
                  Failure error) {
    QNetworkRequest req(QUrl(m_server + path));
    NetworkSecurity::restrictRedirects(req);
    req.setRawHeader("X-Lem-Client-Version", QCoreApplication::applicationVersion().toUtf8());
    // The server sends nothing while it renders a PDF/ZIP with many photographs.
    const bool exporting = path.endsWith("/pdf") || path.endsWith("/bundle");
    req.setTransferTimeout(exporting ? 180000 : 30000);
    req.setHeader(QNetworkRequest::ContentTypeHeader, content);
    if (!m_token.isEmpty())
        req.setRawHeader("Authorization", "Bearer " + m_token.toUtf8());
    const auto requestToken = m_token;
    const auto requestServer = m_server;
    auto reply = net.sendCustomRequest(req, method.toUtf8(), body);
    auto responseBody = NetworkSecurity::boundReply(reply, NetworkSecurity::responseLimit(path));
    trackReply(reply, method, path, !body.isEmpty());
    connect(reply, &QNetworkReply::finished, this, [this, reply, cb, error, requestToken, requestServer, responseBody]() {
        responseBody->consume(reply);
        auto data = responseBody->data;
        auto code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (responseBody->exceeded || reply->error() != QNetworkReply::NoError) {
            // Logout can complete locally before its response. Do not revoke a
            // newer session or another server's policy from an obsolete reply.
            const bool sameSession = m_server == requestServer && m_token == requestToken;
            if (code == 426 && sameSession)
                rejectIncompatibleReply(data);
            if (code == 401 && sameSession) {
                m_token.clear();
                emit changed();
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
}

// Keep the multipart body alive by parenting it to the network reply.
// Photograph uploads and Excel analysis use this separate request path.
void Lem::multipart(QString method, QString path, QList<QPair<QString, QString>> fields,
                    QString filename, QByteArray data, Callback cb, Failure error) {
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
    p.setBody(data);
    multi->append(p);
    QNetworkRequest req(QUrl(m_server + path));
    NetworkSecurity::restrictRedirects(req);
    req.setRawHeader("X-Lem-Client-Version", QCoreApplication::applicationVersion().toUtf8());
    req.setTransferTimeout(60000);
    req.setRawHeader("Authorization", "Bearer " + m_token.toUtf8());
    const auto requestToken = m_token;
    const auto requestServer = m_server;
    auto reply = net.sendCustomRequest(req, method.toUtf8(), multi);
    auto responseBody = NetworkSecurity::boundReply(reply, NetworkSecurity::responseLimit(path));
    multi->setParent(reply);
    trackReply(reply, method, path, true);
    connect(reply, &QNetworkReply::finished, this, [this, reply, cb, error, requestToken, requestServer, responseBody]() {
        responseBody->consume(reply);
        auto data = responseBody->data;
        auto code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (responseBody->exceeded || reply->error() != QNetworkReply::NoError) {
            const bool sameSession = m_server == requestServer && m_token == requestToken;
            if (code == 426 && sameSession)
                rejectIncompatibleReply(data);
            if (code == 401 && sameSession) {
                m_token.clear();
                emit changed();
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
    if (!m_clientCompatibility["compatible"].toBool()) {
        m_token.clear();
        m_autoSync.stop();
    }
    emit compatibilityChanged();
    return m_clientCompatibility["checked"].toBool() &&
           m_clientCompatibility["compatible"].toBool();
}

void Lem::rejectIncompatibleReply(const QByteArray &body) {
    applyClientPolicy(QJsonDocument::fromJson(body).object()["detail"].toObject());
    m_token.clear();
    m_autoSync.stop();
    emit changed();
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
                        m_user = o["user"].toObject();
                        m_users = {};
                        m_deletedContracts = {};
                        emit contractTrashChanged();
                        m_importInfo = {};
                        m_preview = {};
                        m_excel.clear();
                        loadUser();
                        QSaveFile f(m_root + "/last-user.json");
                        if (f.open(QIODevice::WriteOnly)) {
                            f.write(json({{"server", m_server}, {"user", m_user}}));
                            f.commit();
                        }
                        m_busy = false;
                        refresh();
                        scheduleSync(1500);
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
    m_autoSync.stop();
    if (!m_token.isEmpty())
        request(
            "POST", "/api/logout", "{}", [](QByteArray) {}, "application/json",
            [](int, QString) {});
    m_token.clear();
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
    if (m_busy || !m_qveCopyTarget.isEmpty() || attachingFiles() || m_refreshing || m_catalogueLoading || !online())
        return;
    m_refreshing = true;
    emit changed();
    request(
        "GET", "/api/contracts", {},
        [this](QByteArray data) {
            m_contracts = QJsonDocument::fromJson(data).array();
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
                m_preview = {};
            }
            m_refreshing = false;
            persist();
            if (!m_contract.isEmpty())
                refreshCatalogue(m_contract, true);
            else
                status("Contratos atualizados.");
        },
        "application/json",
        [this](int, QString) {
            m_refreshing = false;
            status("Sem atualização do catálogo. Os dados locais continuam disponíveis.");
        });
}

// Show a cached catalogue immediately, then request a refresh when needed.
void Lem::selectContract(QString id) {
    if (m_busy || !m_qveCopyTarget.isEmpty() || attachingFiles() || m_user.isEmpty())
        return;
    if (id != m_contract) {
        m_contract = id;
        m_chargers = m_catalogues[id].toArray();
        m_current = {};
        m_preview = {};
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
    QHash<QString, QJsonArray> localReports;
    for (auto value : m_reports) {
        const auto r = value.toObject();
        localReports[r["charger_id"].toString()].append(QJsonObject{{"id", r["id"]},
                                                                    {"status", r["status"]},
                                                                    {"owner_id", r["owner_id"]},
                                                                    {"dirty", r["dirty"]}});
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
                summary["status"] = r["status"];
                summary["owner_id"] = r["owner_id"];
                summary["dirty"] = r["dirty"];
                reports[positions[id]] = summary;
            } else {
                positions[id] = reports.size();
                reports.append(r);
            }
        }
        c["reports"] = reports;
        out.append(c);
    }
    return out.toVariantList();
}

// Open a cached report without a request, fetch uncached metadata, or create a draft.
// Switching reports also releases photographs deferred until a question-group boundary.
void Lem::openReport(QString chargerId, QString reportId) {
    if (m_busy || !m_qveCopyTarget.isEmpty() || attachingFiles() || m_user.isEmpty() || m_deletedReports.contains(reportId))
        return;
    if (reportId != m_current.value("id").toString())
        reportSyncBoundary();
    if (!reportId.isEmpty()) {
        if (m_reports.contains(reportId)) {
            m_current = Checklist::correctedDraft(m_reports[reportId].toObject());
            m_template = m_current["template"].toObject();
            m_current["last_access"] = double(QDateTime::currentSecsSinceEpoch());
            m_reports[reportId] = m_current;
            emit changed();
            return;
        }
        if (!online())
            return;
        m_busy = true;
        emit changed();
        request("GET", "/api/reports/" + reportId, {}, [this](QByteArray data) {
            auto report = QJsonDocument::fromJson(data).object();
            report["dirty"] = false;
            getPhotos(report, 0);
        });
        return;
    }
    QJsonObject charger, contract;
    for (auto v : m_chargers)
        if (v.toObject()["id"].toString() == chargerId)
            charger = v.toObject();
    for (auto v : m_contracts)
        if (v.toObject()["id"].toString() == m_contract)
            contract = v.toObject();
    if (charger.isEmpty())
        return;
    QFile f(":/shared/checklist-v1.json");
    if (!f.open(QIODevice::ReadOnly)) {
        status("Checklist indisponível.");
        return;
    }
    m_template = QJsonDocument::fromJson(f.readAll()).object();
    auto details = charger["details"].toObject();
    details["external_id"] = charger["external_id"];
    if (charger.contains("station_id")) details["station_id"] = charger["station_id"];
    QString checklistError;
    auto configured = Checklist::forCharger(m_template, details, &checklistError);
    if (!checklistError.isEmpty()) { status(checklistError); return; }
    m_template = configured;
    QJsonObject answers;
    const QMap<QString, QString> identityFields{{"EST.02", "manufacturer"}, {"EST.03", "model"}, {"EST.04", "serial"}};
    for (auto it = identityFields.begin(); it != identityFields.end(); ++it)
        if (!details[it.value()].toString().isEmpty())
            answers[it.key()] = QJsonObject{{"value", details[it.value()]}, {"notes", ""}};
    answers["FIN.02"] = QJsonObject{{"value", m_user["name"]}, {"notes", ""}};
    m_current = {{"id", uuid()},
                 {"charger_id", chargerId},
                 {"owner_id", m_user["id"]},
                 {"revision", 0},
                 {"status", "draft"},
                 {"dirty", true},
                 {"payload",
                  QJsonObject{{"answers", answers},
                              {"started", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}}},
                 {"photos", QJsonArray{}},
                 {"template", m_template},
                 {"snapshot", QJsonObject{{"charger", details}, {"contract", contract}}},
                 {"started", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
    remember();
    status("Novo rascunho guardado no dispositivo.");
}

// Despite the historical name, install report/photo METADATA without downloading images.
// Recover available private file paths, then checkpoint the selected report.
void Lem::getPhotos(QJsonObject report, int index) {
    Q_UNUSED(index);
    m_syncConflicts.remove(report["id"].toString());
    // Metadata first; remote originals are downloaded only on explicit demand.
    report["offline_photos"] =
        m_reports.value(report["id"].toString()).toObject()["offline_photos"].toBool();
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
    m_current = Checklist::correctedDraft(report);
    m_template = m_current["template"].toObject();
    m_busy = false;
    remember();
    status("Report disponível. Fotografias antigas podem ser descarregadas quando necessário.");
}

// Hot input path: update memory and queue a small journal instead of rewriting the cache.
// _edit_seq distinguishes newer local typing from an older in-flight server snapshot.
void Lem::answer(QString key, QString value, QString notes) {
    if (copyLocksCheckpoint(m_current["id"].toString(), key) || m_busy || m_current.isEmpty() || m_current["_conflict"].toBool() ||
        m_current["status"].toString() == "closed" || m_current["owner_id"] != m_user["id"])
        return;
    const auto signedPhoto = m_current["payload"].toObject()["signatures"].toObject()[key].toObject()["photo_id"].toString();
    if (!signedPhoto.isEmpty() && m_current["payload"].toObject()["answers"].toObject()[key].toObject()["value"].toString() != value) {
        removePhoto(signedPhoto);
        if (m_current["payload"].toObject()["signatures"].toObject().contains(key)) return;
    }
    auto payload = m_current["payload"].toObject();
    auto answers = payload["answers"].toObject();
    auto nextAnswer = answers[key].toObject();
    nextAnswer["value"] = value;
    nextAnswer["notes"] = notes;
    if (Measurements::notApplicable(value)) nextAnswer.remove("assessment");
    if (answers[key].toObject() == nextAnswer)
        return;
    if (answers[key].toObject()["value"].toString() != value) {
        auto signatures = payload["signatures"].toObject();
        signatures.remove(key); // A changed name requires signing again.
        payload["signatures"] = signatures;
    }
    answers[key] = nextAnswer;
    payload["answers"] = answers;
    m_current["payload"] = payload;
    m_current["dirty"] = true;
    m_current["_edit_seq"] = m_current["_edit_seq"].toDouble() + 1;
    queueEdits(m_current);
    m_reports[m_current.value("id").toString()] = m_current;
    emit changed();
    scheduleSync();
}
// Manual assessment is independent of the technician's mandatory reading.
void Lem::setAssessment(QString key, QString assessment) {
    if (copyLocksCheckpoint(m_current["id"].toString(), key) || m_busy || m_current.isEmpty() ||
        m_current["_conflict"].toBool() || m_current["status"] != "draft" ||
        m_current["owner_id"] != m_user["id"] || (assessment != "OK" && assessment != "NOK")) return;
    bool allowed = false;
    for (auto section : m_template["sections"].toArray())
        for (auto item : section.toObject()["items"].toArray())
            if (item.toObject()["id"] == key && item.toObject()["reading_required"].toBool()) allowed = true;
    if (!allowed) return;
    auto payload = m_current["payload"].toObject();
    auto answers = payload["answers"].toObject();
    auto answer = answers[key].toObject();
    if (answer["assessment"] == assessment && !Measurements::notApplicable(answer["value"].toString())) return;
    if (Measurements::notApplicable(answer["value"].toString())) answer["value"] = "";
    answer["value"] = answer["value"].toString();
    answer["notes"] = answer["notes"].toString();
    answer["assessment"] = assessment;
    answers[key] = answer;
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
        report["status"].toString() == "closed" || report["owner_id"] != m_user["id"]) {
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
    auto bytes = f.read(20 * 1024 * 1024 + 1);
    if (bytes.size() > 20 * 1024 * 1024) {
        status("Máximo 20 MB por evidência.");
        return false;
    }
    const auto videoMime = EvidenceMedia::videoMime(bytes);
    QString ext;
    if (!videoMime.isEmpty()) {
        if (!EvidenceMedia::durationValid(bytes)) {
            status("Vídeo inválido ou superior a 60 segundos. Use MP4 ou 3GP.");
            return false;
        }
        ext = videoMime == "video/3gpp" ? ".3gp" : ".mp4";
    } else {
    QBuffer b(&bytes);
    b.open(QIODevice::ReadOnly);
    QImageReader reader(&b);
    reader.setAutoTransform(true);
    auto size = reader.size();
    if (!size.isValid() || qint64(size.width()) * size.height() > 40000000) {
        status("Imagem inválida ou superior a 40 MP.");
        return false;
    }
    auto format = reader.format().toLower();
    if (format == "jpeg" || format == "jpg")
        ext = ".jpg";
    else if (format == "png")
        ext = ".png";
    else {
        auto image = reader.read();
        if (image.isNull()) {
            status("Formato de imagem não suportado.");
            return false;
        }
        bytes.clear();
        QBuffer output(&bytes);
        output.open(QIODevice::WriteOnly);
        image.save(&output, "JPEG", 92);
        ext = ".jpg";
    }
    }
    if (bytes.size() > 20 * 1024 * 1024) {
        status("Máximo 20 MB por evidência após conversão."); return false;
    }
    auto existing = report["photos"].toArray();
    qint64 total = bytes.size();
    for (auto value : existing) total += value.toObject()["size"].toDouble();
    if (existing.size() >= 200 || total > 250 * 1024 * 1024) {
        status("Limite por report: 200 evidências e 250 MB."); return false;
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

// Crops create a fresh evidence identity: an acknowledged server attachment is
// immutable. Persist the replacement and deletion tombstone before unlinking it.
void Lem::cropPhoto(QString photoId, qreal x, qreal y, qreal width, qreal height) {
    if (switchingBlocked(false) || m_current.isEmpty() || m_current["_conflict"].toBool() ||
        m_current["status"].toString() != "draft" || m_current["owner_id"] != m_user["id"])
        return;
    for (auto entry : m_current["payload"].toObject()["signatures"].toObject())
        if (entry.toObject()["photo_id"].toString() == photoId) {
            status("Para alterar a assinatura, assine novamente em FIN.02."); return;
        }
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) || !std::isfinite(height) ||
        x < 0 || y < 0 || width <= 0 || height <= 0 || x + width > 1.000001 || y + height > 1.000001)
        return;
    QJsonObject old;
    for (auto entry : m_current["photos"].toArray())
        if (entry.toObject()["id"].toString() == photoId) old = entry.toObject();
    if (copyLocksCheckpoint(m_current["id"].toString(), old["checkpoint"].toString())) return;
    const auto source = localPath(QUrl(old["path"].toString()));
    if (old.isEmpty() || old["mime"].toString().startsWith("video/") || !QFileInfo::exists(source)) return;
    if (!flushEdits()) return;
    const auto reportId = m_current["id"].toString(), newId = uuid();
    const auto mime = old["mime"].toString() == "image/png" || QFileInfo(source).suffix() == "png"
        ? QString("image/png") : QString("image/jpeg");
    const auto target = SafeStorage::photoPath(cacheDir(), newId, mime);
    m_busy = true;
    m_localOperationTitle = "A recortar fotografia";
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
            report["owner_id"] != m_user["id"] || report["_conflict"].toBool()) {
            QFile::remove(target); status("Não foi possível recortar. Fotografia original conservada."); return;
        }
        qint64 total = size;
        for (auto value : photos)
            if (value.toObject()["id"].toString() != photoId) total += value.toObject()["size"].toDouble();
        if (total > 250 * 1024 * 1024) {
            QFile::remove(target); status("Limite de 250 MB por report. Fotografia original conservada."); return;
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
        status("Fotografia recortada. Envio ao mudar de grupo ou sair do report.");
    });
    watcher->setFuture(QtConcurrent::run(&m_editPool, [source, target, x, y, width, height, mime]() -> qint64 {
        QImageReader reader(source); reader.setAutoTransform(true);
        const auto dimensions = reader.size();
        if (!dimensions.isValid() || qint64(dimensions.width()) * dimensions.height() > 40000000) return -1;
        const auto image = reader.read(); if (image.isNull()) return -1;
        const int left = qBound(0, int(std::floor(x * image.width())), image.width() - 1);
        const int top = qBound(0, int(std::floor(y * image.height())), image.height() - 1);
        const int right = qBound(left + 1, int(std::ceil((x + width) * image.width())), image.width());
        const int bottom = qBound(top + 1, int(std::ceil((y + height) * image.height())), image.height());
        QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly);
        if (!image.copy(left, top, right - left, bottom - top).save(&buffer, mime == "image/png" ? "PNG" : "JPEG", 92) || bytes.size() > 20 * 1024 * 1024) return -1;
        QSaveFile file(target);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) return -1;
        return bytes.size();
    }));
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
    if (id == m_qveCopyTarget || id == m_attachReport) return;
    if (id != m_current.value("id").toString() || switchingBlocked())
        return;
    const auto r = m_current;
    const bool admin = m_user["role"].toString() == "admin";
    if (r.isEmpty() ||
        (!admin && (r["status"].toString() != "draft" || r["owner_id"] != m_user["id"]))) {
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
            "/api/reports/" + id + "?base_revision=" + QString::number(r["revision"].toInt()), {},
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
        m_current["status"].toString() != "draft" || m_current["owner_id"] != m_user["id"])
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
    auto signatures = payload["signatures"].toObject();
    for (auto key : signatures.keys())
        if (signatures[key].toObject()["photo_id"].toString() == photoId) signatures.remove(key);
    payload["signatures"] = signatures;
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
    // Only remove our private cache copy, never the original in the user's gallery.
    const QFileInfo cached(localPath(QUrl(removed["path"].toString())));
    if (cached.canonicalPath() == QFileInfo(cacheDir() + "/photos").canonicalFilePath() &&
        (cached.fileName() == photoId + ".jpg" || cached.fileName() == photoId + ".png" || cached.fileName() == photoId + ".mp4" || cached.fileName() == photoId + ".3gp")) {
        if (QFile::remove(cached.absoluteFilePath()))
            m_photoBytes = qMax(qint64(0), m_photoBytes - cached.size());
    }
    emit changed();
    status("Fotografia removida do rascunho. A remoção será enviada automaticamente.");
    scheduleSync();
}

// Android delegates to the installed camera through a small Java Activity.
// Capture the report/checkpoint identity now, rather than using the active page on return.
void Lem::takePhoto(QString key) { takeCapture(key, false); }
void Lem::takeVideo(QString key) { takeCapture(key, true); }
void Lem::takeCapture(QString key, bool video) {
#ifdef Q_OS_ANDROID
    if (m_busy || m_cameraPending || m_current.isEmpty() || m_current["_conflict"].toBool() ||
        m_current["status"].toString() == "closed" || m_current["owner_id"] != m_user["id"])
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
void Lem::finish() {
    if (!m_qveCopyTarget.isEmpty() || attachingFiles()) { status("Aguarde o fim da cópia ou da seleção de evidências antes de concluir."); return; }
    if (m_busy || m_current.isEmpty() || m_current["_conflict"].toBool() ||
        m_current["owner_id"] != m_user["id"] || m_current["status"].toString() == "closed")
        return;
    auto answers = m_current["payload"].toObject()["answers"].toObject();
    auto photos = m_current["photos"].toArray();
    QStringList errors;
    const QMap<QString, QString> identityFields{{"EST.02", "manufacturer"}, {"EST.03", "model"}, {"EST.04", "serial"}};
    for (auto s : m_template["sections"].toArray())
        for (auto i : s.toObject()["items"].toArray()) {
            auto item = i.toObject();
            auto id = item["id"].toString();
            auto a = answers[id].toObject();
            auto value = a["value"].toString().trimmed();
            if (Measurements::digits(id) && !value.isEmpty()) {
                bool valid = false;
                const auto formatted = Measurements::format(id, value, &valid);
                if (!valid) errors << id + ": valor numérico inválido";
                else {
                    value = formatted;
                    a["value"] = formatted;
                    answers[id] = a;
                }
            }
            const auto signature = m_current["payload"].toObject()["signatures"].toObject()[id].toObject();
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
    m_current.remove("_defer_photo_sync");
    m_current["status"] = "closed";
    auto payload = m_current["payload"].toObject();
    payload["answers"] = answers;
    payload["finished"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    m_current["payload"] = payload;
    m_current["dirty"] = true;
    m_current["_edit_seq"] = m_current["_edit_seq"].toDouble() + 1;
    applyClosedIdentity(m_current);
    if (m_template["version"].toInt() >= 4) {
        auto snapshot = m_current["snapshot"].toObject();
        auto details = snapshot["charger"].toObject();
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
                               "/report-" + m_current.value("id").toString() +
                               (kind == "pdf" ? ".pdf" : ".zip"))
        .toString();
}

// Validate before opening a document picker: Android providers may create an empty
// document at selection time. Local PDF generation does not require a session.
bool Lem::prepareExport(QString kind) {
    if (m_busy || m_current.isEmpty()) {
        status("Abra um report antes de exportar e aguarde a operação em curso.");
        return false;
    }
    if (m_current["status"].toString() != "closed") {
        status("Conclua o report antes de exportar PDF ou ZIP.");
        return false;
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
        if (!online())
            return false;
        if (m_current["dirty"].toBool()) {
            status(
                "Sincronize o report antes de exportar o ZIP. O PDF pode ser gerado localmente.");
            return false;
        }
        return true;
    }
    return false;
}
void Lem::exportSaved(const QString &kind, const QUrl &target, bool local) {
    QString message =
        (local ? "PDF exportado localmente: " : "Exportado: ") + target.toDisplayString();
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
    if (kind == "pdf" && ReportPdf::validate(m_current).isEmpty()) {
        if (!flushEdits())
            return;
        auto report = m_current;
        if (report["technician"].toString().isEmpty() && report["owner_id"] == m_user["id"])
            report["technician"] = m_user["name"];
        if (report["technician"].toString().isEmpty())
            report["technician"] =
                report["payload"].toObject()["answers"].toObject()[report["template"].toObject()["version"].toInt() >= 3 ? "FIN.02" : "PG04.02"].toObject()["value"];
        const auto destination = localPath(target);
        m_busy = true;
        m_localOperationTitle = "A gerar e guardar PDF neste dispositivo";
        status(m_localOperationTitle);
        auto watcher = new QFutureWatcher<QString>(this);
        connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, target] {
            const auto error = watcher->result();
            watcher->deleteLater();
            m_busy = false;
            m_localOperationTitle.clear();
            if (error.isEmpty())
                exportSaved("pdf", target, true);
            else
                status(error);
        });
        watcher->setFuture(QtConcurrent::run(&m_editPool, [report, destination] {
            QString error;
            ReportPdf::write(report, destination, &error);
            return error;
        }));
        return;
    }
    // Server fallback for clean reports whose originals have not been cached.
    m_busy = true;
    emit changed();
    request("GET", "/api/reports/" + m_current.value("id").toString() + "/" + kind, {},
            [this, target, kind](QByteArray data) {
                m_busy = false;
                if ((kind == "pdf" && !data.startsWith("%PDF-")) ||
                    (kind == "bundle" && !data.startsWith("PK"))) {
                    status("O servidor não devolveu um documento válido; destino não alterado.");
                    return;
                }
                const auto destination = localPath(target);
                if (target.scheme() == "content") {
                    QFile file(destination);
                    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() ||
                        !file.flush()) {
                        status("Não foi possível gravar o documento selecionado.");
                        return;
                    }
                } else {
                    QSaveFile file(destination);
                    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() ||
                        !file.commit()) {
                        status("Não foi possível guardar o ficheiro.");
                        return;
                    }
                }
                exportSaved(kind, target);
            });
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
                if (m_contract == id) {
                    m_current = {};
                    m_contract.clear();
                    m_chargers = {};
                    m_preview = {};
                }
                m_catalogues.remove(id);
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
    if (m_busy || !online())
        return;
    QFile f(localPath(url));
    if (!f.open(QIODevice::ReadOnly)) {
        status("Não foi possível abrir o Excel.");
        return;
    }
    m_excel = f.read(20 * 1024 * 1024 + 1);
    if (m_excel.size() > 20 * 1024 * 1024) {
        status("Máximo 20 MB.");
        return;
    }
    m_excelName = "import.xlsx";
    m_preview = {};
    m_busy = true;
    emit changed();
    multipart("POST", "/api/imports/analyse", {}, m_excelName, m_excel, [this](QByteArray data) {
        m_importInfo = QJsonDocument::fromJson(data).object();
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

// Validate a proposed mapping without writing chargers; commitExcel reuses that mapping.
void Lem::previewExcel(QString sheet, int row, QVariantMap columns) {
    if (m_busy || !online() || m_contract.isEmpty() || m_excel.isEmpty())
        return;
    m_mapping = {
        {"sheet", sheet}, {"header_row", row}, {"columns", QJsonObject::fromVariantMap(columns)}};
    m_busy = true;
    emit changed();
    multipart("POST", "/api/contracts/" + m_contract + "/import",
              {{"mapping", QString::fromUtf8(json(m_mapping))}, {"commit", "false"}}, m_excelName,
              m_excel, [this](QByteArray data) {
                  m_preview = QJsonDocument::fromJson(data).object();
                  m_busy = false;
                  status("Pré-visualização pronta. Confira os IDs antes de importar.");
              });
}

// Commit the previously previewed import and invalidate the catalogue cache afterwards.
void Lem::commitExcel() {
    if (m_busy || !online() || m_preview.isEmpty() || !m_preview["errors"].toArray().isEmpty())
        return;
    m_busy = true;
    emit changed();
    multipart("POST", "/api/contracts/" + m_contract + "/import",
              {{"mapping", QString::fromUtf8(json(m_mapping))}, {"commit", "true"}}, m_excelName,
              m_excel, [this](QByteArray data) {
                  m_busy = false;
                  m_preview = {};
                  m_importInfo = {};
                  m_excel.clear();
                  status("Importação concluída: " + QString::fromUtf8(data));
                  refreshCatalogue(m_contract, true);
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
    m_token.clear();
    m_server = "demo://local";
    m_clientCompatibility = {};
    emit compatibilityChanged();
    m_user = {{"id", "demonstracao"},
              {"name", "Técnico de demonstração"},
              {"role", "admin"},
              {"email", "demo@local"}};
    loadUser();
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
                                                       {"location", "Local de demonstração"},
                                                       {"address", "Dados fictícios"},
                                                       {"cpe", "CPE-DEMO"},
                                                       {"manufacturer", "Exemplo"},
                                                       {"power", "60 kW"}}},
                               {"reports", QJsonArray{}}}};
    m_catalogues[cid] = m_chargers;
    openReport(chargerId, "");
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
