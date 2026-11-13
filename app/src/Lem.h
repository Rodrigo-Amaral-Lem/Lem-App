// SPDX-License-Identifier: GPL-3.0-only
// Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

// Public QML contract and private state of the Lem application controller.
// Lem is the QML-facing controller type used throughout the client.
// See docs/architecture.md for the JSON fields and notification contract.

#pragma once
#include <QObject>
#include "Checklist.h"
#include "ServerSettings.h"
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QUrl>
#include <functional>
#include <QTimer>
#include <QSet>
#include <QFutureWatcher>
#include <QThreadPool>
#include <QMap>
class QNetworkReply;
/**
 * Owns the active user, cached report collection, network requests and save queues.
 *
 * QML reads QVariant projections and invokes public actions. m_current is a copy
 * of the selected entry in m_reports: mutations must deliberately update both.
 * Network callbacks run on this object's thread. Worker tasks only handle copied
 * bytes/files; they must not modify this QObject or its JSON containers.
 */
class Lem : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString server READ server WRITE setServer NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QVariantMap operationStatus READ operationStatus NOTIFY operationChanged)
    Q_PROPERTY(QVariantList operationTransfers READ operationTransfers NOTIFY operationChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString copyingGroup READ copyingGroup NOTIFY changed)
    Q_PROPERTY(bool attachingFiles READ attachingFiles NOTIFY changed)
    Q_PROPERTY(bool saving READ saving NOTIFY localSaveStateChanged)
    Q_PROPERTY(bool localSaveError READ localSaveError NOTIFY localSaveStateChanged)
    Q_PROPERTY(QVariantMap clientCompatibility READ clientCompatibility NOTIFY compatibilityChanged)
    Q_PROPERTY(bool authenticated READ authenticated NOTIFY changed)
    Q_PROPERTY(bool syncing READ syncing NOTIFY changed)
    Q_PROPERTY(bool refreshing READ refreshing NOTIFY changed)
    Q_PROPERTY(QString syncState READ syncState NOTIFY changed)
    Q_PROPERTY(double photoCacheMB READ photoCacheMB NOTIFY changed)
    Q_PROPERTY(bool downloadingPhotos READ downloadingPhotos NOTIFY changed)
    Q_PROPERTY(QString photoDownloadState READ photoDownloadState NOTIFY changed)
    Q_PROPERTY(QVariantMap user READ user NOTIFY changed)
    Q_PROPERTY(QVariantList users READ users NOTIFY changed)
    Q_PROPERTY(QVariantList contracts READ contracts NOTIFY contractsChanged)
    Q_PROPERTY(QVariantList deletedContracts READ deletedContracts NOTIFY contractTrashChanged)
    Q_PROPERTY(QVariantList chargers READ chargers NOTIFY chargersChanged)
    Q_PROPERTY(QVariantMap current READ current NOTIFY currentChanged)
    Q_PROPERTY(QVariantMap photosByCheckpoint READ photosByCheckpoint NOTIFY photosChanged)
    Q_PROPERTY(QVariantList sections READ sections NOTIFY sectionsChanged)
    Q_PROPERTY(QVariantMap importInfo READ importInfo NOTIFY changed)
    Q_PROPERTY(QVariantMap importPreview READ importPreview NOTIFY changed)
    Q_PROPERTY(QString storage READ storage CONSTANT)
  public:
    explicit Lem(QString root = {}, QObject *parent = nullptr);
    ~Lem() override;
    bool saving() const {
        return !m_pendingEdits.isEmpty();
    }
    bool localSaveError() const {
        return m_localSaveError;
    }
    QString server() const {
        return m_server;
    }
    void setServer(QString);
    QString message() const {
        return m_message;
    }
    QVariantMap operationStatus() const;
    QVariantList operationTransfers() const;
    QString copyingGroup() const { return m_copyScope; }
    bool attachingFiles() const { return !m_attachReport.isEmpty(); }
    bool busy() const {
        return m_busy;
    }
    QVariantMap clientCompatibility() const {
        return m_clientCompatibility.toVariantMap();
    }
    bool authenticated() const {
        return !m_token.isEmpty() && !m_server.startsWith("demo:");
    }
    bool syncing() const {
        return m_syncing;
    }
    bool refreshing() const {
        return m_refreshing;
    }
    bool downloadingPhotos() const {
        return !m_photoBatchReport.isEmpty();
    }
    QString photoDownloadState() const {
        return m_photoBatchState;
    }
    QString syncState() const {
        return m_syncState;
    }
    double photoCacheMB() const {
        return m_photoBytes / (1024.0 * 1024.0);
    }
    QVariantMap user() const {
        return m_user.toVariantMap();
    }
    QVariantList users() const {
        return m_users.toVariantList();
    }
    QVariantList contracts() const {
        return m_contracts.toVariantList();
    }
    QVariantList deletedContracts() const {
        return m_deletedContracts.toVariantList();
    }
    QVariantList chargers() const;
    QVariantMap current() const;
    QVariantMap photosByCheckpoint() const;
    QVariantList sections() const {
        return Checklist::displayTemplate(m_template)["sections"].toArray().toVariantList();
    }
    QVariantMap importInfo() const {
        return m_importInfo.toVariantMap();
    }
    QVariantMap importPreview() const {
        return m_preview.toVariantMap();
    }
    QString storage() const {
        return m_root;
    }
    // Session/catalogue actions exposed to QML.
    Q_INVOKABLE void demo();
    Q_INVOKABLE void useDefaultServer();
    Q_INVOKABLE void login(QString email, QString password);
    Q_INVOKABLE void logout();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void selectContract(QString id);
    // Local-first report editing and synchronization.
    Q_INVOKABLE void openReport(QString chargerId, QString reportId = {});
    Q_INVOKABLE void answer(QString checkpoint, QString value, QString notes);
    Q_INVOKABLE void setAssessment(QString checkpoint, QString assessment);
    Q_INVOKABLE void attach(QString checkpoint, QUrl file);
    Q_INVOKABLE void attachMany(QString checkpoint, const QList<QUrl> &files);
    Q_INVOKABLE void takePhoto(QString checkpoint);
    Q_INVOKABLE void takeVideo(QString checkpoint);
    Q_INVOKABLE void openVideo(QString photoId);
    Q_INVOKABLE void cropPhoto(QString photoId, qreal x, qreal y, qreal width, qreal height);
    Q_INVOKABLE void removePhoto(QString photoId);
    Q_INVOKABLE void deleteReport(QString reportId);
    Q_INVOKABLE void sync();
    Q_INVOKABLE void reportSyncBoundary();
    // Originals are downloaded explicitly; report metadata alone stays lightweight.
    Q_INVOKABLE void fetchPhoto(QString photoId);
    Q_INVOKABLE QVariantMap photoDownloadSummary() const;
    Q_INVOKABLE void downloadReportPhotos();
    Q_INVOKABLE void cancelReportPhotoDownloads();
    Q_INVOKABLE void releaseReportPhotos();
    Q_INVOKABLE bool photoDownloading(QString photoId) const {
        return m_downloads.contains(photoId);
    }
    Q_INVOKABLE void refreshReport();
    // Administrator transfers and protected offline conflict recovery.
    Q_INVOKABLE void loadReportOwners();
    Q_INVOKABLE void transferReport(QString ownerId);
    Q_INVOKABLE void viewServerReport();
    Q_INVOKABLE void useServerReport();
    Q_INVOKABLE void copyLocalReport();
    Q_INVOKABLE void cleanPhotoCache();
    Q_INVOKABLE void clearLocalCopies();
    Q_INVOKABLE QVariantList qveCopySources() const;
    Q_INVOKABLE void copyQveFrom(QString sourceReportId);
    Q_INVOKABLE QVariantList measurementCopySources() const;
    Q_INVOKABLE void copyMeasurementsFrom(QString sourceReportId);
    Q_INVOKABLE bool importSignature(QString checkpoint, QUrl image);
    Q_INVOKABLE bool saveSignature(QString checkpoint, QString pngDataUrl);
    Q_INVOKABLE void finish();
    Q_INVOKABLE int pendingCount() const;
    Q_INVOKABLE bool prepareExport(QString kind);
    Q_INVOKABLE void exportReport(QString kind, QUrl destination);
    // Administrative Excel import and account/contract management.
    Q_INVOKABLE void createContract(QString company, int year, int semester, QString kind);
    Q_INVOKABLE void deleteContract(QString id, QString confirmation);
    Q_INVOKABLE void loadDeletedContracts();
    Q_INVOKABLE void restoreContract(QString id);
    Q_INVOKABLE void analyseExcel(QUrl file);
    Q_INVOKABLE void previewExcel(QString sheet, int row, QVariantMap columns);
    Q_INVOKABLE void commitExcel();
    Q_INVOKABLE void invalidatePreview() {
        m_preview = {};
        emit changed();
    }
    Q_INVOKABLE void loadUsers();
    Q_INVOKABLE void createUser(QString email, QString name, QString password, QString role = "tech");
    Q_INVOKABLE void assignUser(QString userId);
    Q_INVOKABLE QString contractId() const {
        return m_contract;
    }
    Q_INVOKABLE QString defaultExport(QString kind) const;
  signals:
    void changed();
    // Frequent progress updates: do not use changed() for every transferred byte.
    void operationChanged();
    void compatibilityChanged();
    void localSaveStateChanged();
    void sectionsChanged();
    void contractsChanged();
    void contractTrashChanged();
    void chargersChanged();
    void currentChanged();
    void photosChanged();

  private:
    void applyClosedIdentity(const QJsonObject &report);
    QVariantList groupCopySources(QString scope) const;
    void copyReportGroup(QString sourceId, QString scope);
    bool copyLocksCheckpoint(const QString &reportId, const QString &checkpoint) const;
    void attachNextFile();
    QList<QUrl> m_attachFiles;
    QString m_attachReport, m_attachCheckpoint;
    int m_attachDone = 0, m_attachFailed = 0;
    QString m_copyScope;
    QString m_pendingCopySource, m_pendingCopyScope, m_pendingCopyTarget;
    void startPendingGroupCopy();
    void beginQveCopy(QJsonObject source);
    void copyNextQvePhoto(int index);
    void stageQvePhoto(int index, const QByteArray &bytes);
    void commitQveCopy();
    void failQveCopy(QString message);
    QJsonObject m_qveCopySource;
    QJsonArray m_qveCopyPhotos;
    QStringList m_qveCopyFiles;
    QString m_qveCopyTarget;
    QVariantList buildChargerRows() const;
    QVariantList m_chargerRows;
    // Inputs of the last buildChargerRows() call; rows are rebuilt only when these change.
    QJsonArray m_rowsChargers, m_rowsReports;
    bool m_rowsBuilt = false;
    bool applyClientPolicy(const QJsonObject &policy);
    void rejectIncompatibleReply(const QByteArray &body);
    QJsonObject m_clientCompatibility;
    void trackReply(QNetworkReply *reply, QString method, QString path, bool upload);
    // Operation presentation only; this is not the durable synchronization queue.
    QMap<quint64, QVariantMap> m_transfers;
    QVariantMap m_lastTransfer;
    quint64 m_transferSerial = 0;
    QTimer m_operationNotify;
    int m_syncCompleted = 0, m_syncTotal = 0;
    bool forgetReport(QString reportId);
    // Local tombstones prevent stale journal replay after permanent deletion.
    QSet<QString> m_deletedReports;
    bool attachToReport(QString reportId, QString checkpoint, QUrl file);
    void checkCameraResult();
    void takeCapture(QString key, bool video);
    bool m_cameraPending = false;
    using Failure = std::function<void(int, QString)>;
    using Callback = std::function<void(QByteArray)>;
    void request(QString method, QString path, QByteArray body, Callback cb,
                 QString content = "application/json", Failure error = {});
    void multipart(QString method, QString path, QList<QPair<QString, QString>> fields,
                   QString filename, QByteArray data, Callback cb, Failure error = {});
    void exportSaved(const QString &kind, const QUrl &target, bool local = false);
    void status(QString s);
    bool online();
    bool persist();
    void loadUser();
    QString cacheDir() const;
    void remember();
    void scheduleSync(int delay = 8000);
    void startSync(bool manual);
    bool releasePhotoSync(QString reportId);
    void syncFailed(int code, QString message);
    bool mergeSync(QJsonObject sent, bool complete = false);
    void syncRequest(QString method, QString path, QByteArray body, Callback cb);
    bool saveEdits(QJsonObject report);
    QString editPath(QString reportId) const;
    void queueEdits(QJsonObject report);
    void writeNextEdits();
    void completeEditWrite();
    bool flushEdits();
    // One writer, immutable active snapshot, newest pending snapshot per report.
    QThreadPool m_editPool;
    QFutureWatcher<bool> m_editWatcher;
    QTimer m_editTimer;
    QJsonObject m_pendingEdits;
    QString m_activeEditId;
    QJsonObject m_activeEdit;
    bool m_flushingEdits = false, m_localSaveError = false;
    // Emit model-specific notifications only when their contents actually change.
    QJsonObject m_notifiedTemplate, m_notifiedCurrent;
    QJsonArray m_notifiedPhotos;
    // Lazy QVariant projections are implicitly shared, not rebuilt per QML read.
    mutable QJsonObject m_projectedCurrent;
    mutable QVariantMap m_currentProjection, m_photoProjection;
    mutable QJsonArray m_projectedPhotos;
    QJsonArray m_notifiedContracts;
    void refreshCatalogue(QString id, bool force = false);
    void fetchPhotoForReport(QString reportId, QString photoId);
    void pumpPhotoBatch();
    void photoDownloadFinished(QString reportId, QString photoId, bool success);
    friend struct LemReviewTest; // Isolated native regression harness; no public QML API.
    bool switchingBlocked(bool includeGroupCopy = true) const {
        return m_busy || m_syncing || m_refreshing || !m_downloads.isEmpty() || downloadingPhotos() ||
               attachingFiles() || (includeGroupCopy && !m_qveCopyTarget.isEmpty());
    }
    void syncNext(QStringList ids, int index);
    void syncPhotos(QStringList ids, int index, QJsonObject report, int photoIndex);
    void getPhotos(QJsonObject report, int index);
    QString localPath(QUrl url) const;
    QNetworkAccessManager net;
    QString m_root, m_server = ServerSettings::defaultUrl(), m_message, m_token, m_contract;
    QString m_localOperationTitle;
    bool m_busy = false, m_syncing = false, m_refreshing = false;
    bool m_catalogueLoading = false, m_manualSync = false;
    // Transient retry backoff, separate from durable conflicts in each report JSON.
    int m_retrySeconds = 30;
    qint64 m_retryNotBefore = 0;
    QString m_syncState = "Guardado no dispositivo";
    QString m_syncReport;
    QTimer m_autoSync;
    QSet<QString> m_syncConflicts, m_downloads;
    QJsonObject m_catalogueTimes;
    qint64 m_photoBytes = 0;
    // One explicit report-download batch, sharing two slots with individual downloads.
    QString m_photoBatchReport, m_photoBatchState;
    QStringList m_photoBatchQueue;
    QSet<QString> m_photoBatchPending;
    int m_photoBatchTotal = 0, m_photoBatchDone = 0, m_photoBatchFailures = 0;
    bool m_photoBatchStopped = false;
    QJsonObject m_catalogues;
    // m_reports is keyed by report UUID; m_current mirrors the selected report.
    QJsonObject m_user, m_template, m_current, m_reports, m_importInfo, m_preview, m_mapping;
    QJsonArray m_contracts, m_chargers, m_users, m_deletedContracts;
    QString m_excelName;
    QByteArray m_excel;
};
