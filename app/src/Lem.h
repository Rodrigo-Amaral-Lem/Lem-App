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
class QFile;
namespace NetworkSecurity { struct FileBody; }
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
    Q_PROPERTY(QVariantMap reportPresentationSave READ reportPresentationSave NOTIFY changed)
    Q_PROPERTY(bool canSaveReportPresentation READ canSaveReportPresentation NOTIFY changed)
    Q_PROPERTY(bool authenticated READ authenticated NOTIFY changed)
    Q_PROPERTY(bool sessionPending READ sessionPending NOTIFY changed)
    Q_PROPERTY(bool syncing READ syncing NOTIFY changed)
    Q_PROPERTY(int pendingReports READ pendingCount NOTIFY changed)
    Q_PROPERTY(bool refreshing READ refreshing NOTIFY changed)
    Q_PROPERTY(QString syncState READ syncState NOTIFY changed)
    Q_PROPERTY(double photoCacheMB READ photoCacheMB NOTIFY changed)
    Q_PROPERTY(bool downloadingPhotos READ downloadingPhotos NOTIFY changed)
    Q_PROPERTY(bool currentReportDownloadingPhotos READ currentReportDownloadingPhotos NOTIFY changed)
    Q_PROPERTY(QString photoDownloadState READ photoDownloadState NOTIFY changed)
    Q_PROPERTY(QVariantMap user READ user NOTIFY changed)
    Q_PROPERTY(QVariantList users READ users NOTIFY changed)
    Q_PROPERTY(QVariantMap localOrderDrafts READ localOrderDrafts NOTIFY localOrderDraftsChanged)
    Q_PROPERTY(QVariantList localRecoveryReports READ localRecoveryReports NOTIFY changed)
    Q_PROPERTY(bool currentOrderReadOnly READ currentOrderReadOnly NOTIFY changed)
    Q_PROPERTY(QVariantList workOrders READ workOrders NOTIFY workOrderListChanged)
    Q_PROPERTY(QVariantList workCandidates READ workCandidates NOTIFY workCandidatesChanged)
    Q_PROPERTY(QString planningDay READ planningDay NOTIFY workOrderListChanged)
    Q_PROPERTY(QString orderMessage READ orderMessage NOTIFY workOrdersChanged)
    Q_PROPERTY(bool savingOrder READ savingOrder NOTIFY workOrdersChanged)
    Q_PROPERTY(QVariantList contracts READ contracts NOTIFY contractsChanged)
    Q_PROPERTY(QVariantList interventions READ interventions NOTIFY contractsChanged)
    Q_PROPERTY(QVariantList deletedContracts READ deletedContracts NOTIFY contractTrashChanged)
    Q_PROPERTY(QVariantList chargers READ chargers NOTIFY chargersChanged)
    Q_PROPERTY(QVariantList stations READ stations NOTIFY stationsChanged)
    Q_PROPERTY(bool stationCatalogueLoaded READ stationCatalogueLoaded NOTIFY changed)
    Q_PROPERTY(bool stationCatalogueLoading READ stationCatalogueLoading NOTIFY changed)
    Q_PROPERTY(QString stationCatalogueMessage READ stationCatalogueMessage NOTIFY changed)
    Q_PROPERTY(bool desktopStationManagement READ desktopStationManagement CONSTANT)
    Q_PROPERTY(QVariantMap stationReports READ stationReports NOTIFY changed)
    Q_PROPERTY(QVariantMap stationEditResult READ stationEditResult NOTIFY changed)
    Q_PROPERTY(QVariantMap current READ current NOTIFY currentChanged)
    Q_PROPERTY(QVariantMap photosByCheckpoint READ photosByCheckpoint NOTIFY photosChanged)
    Q_PROPERTY(QVariantList sections READ sections NOTIFY sectionsChanged)
    Q_PROPERTY(QVariantMap importInfo READ importInfo NOTIFY changed)
    Q_PROPERTY(QVariantMap importPreview READ importPreview NOTIFY changed)
    Q_PROPERTY(QString storage READ storage CONSTANT)
    Q_PROPERTY(bool desktopCorrections READ desktopCorrections CONSTANT)
    Q_PROPERTY(bool desktopExports READ desktopCorrections CONSTANT)
    Q_PROPERTY(QVariantMap contractExport READ contractExport NOTIFY changed)
    Q_PROPERTY(QVariantMap passwordReset READ passwordReset NOTIFY changed)
  public:
    explicit Lem(QString root = {}, QObject *parent = nullptr);
    ~Lem() override;
    bool desktopCorrections() const;
    QVariantMap contractExport() const { return m_contractExportState.toVariantMap(); }
    QVariantMap passwordReset() const { return m_passwordReset.toVariantMap(); }
    Q_INVOKABLE void beginReportCorrection();
    Q_INVOKABLE bool setCorrectionTimes(QString started, QString finished);
    bool saving() const {
        return !m_pendingEdits.isEmpty();
    }
    bool localSaveError() const {
        return m_localSaveError || m_localReadError;
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
    bool sessionPending() const { return !m_pendingSessionToken.isEmpty(); }
    bool evidenceDownloadActive() const { return downloadingPhotos() || !m_downloads.isEmpty(); }
    bool backgroundTransferActive() const {
        return m_syncing || m_refreshing || m_stationCatalogueLoading ||
               evidenceDownloadActive();
    }
    bool refreshBackgroundCatalogues();
    QVariantMap reportPresentationSave() const { return m_reportPresentationSave.toVariantMap(); }
    bool canSaveReportPresentation() const {
        return authenticated() && m_user["role"] == "admin" && !m_contract.isEmpty() &&
               !m_busy && !m_syncing && !m_refreshing;
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
    bool currentReportDownloadingPhotos() const;
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
    QVariantList contracts() const;
    QVariantList interventions() const;
    Q_INVOKABLE void loadCorrectiveContracts(QString stationId);
    Q_INVOKABLE void createIntervention(QString id, QVariantMap fields);
    Q_INVOKABLE void shareIntervention(QString id, QVariantMap fields);
    Q_INVOKABLE void importCorrectiveContract(QString id, QVariantMap fields, QUrl workbook);
    Q_INVOKABLE QString prepareIntervention(QString id);
    Q_INVOKABLE void leaveInterventionScope();
    QVariantList deletedContracts() const {
        return m_deletedContracts.toVariantList();
    }
    QVariantList chargers() const;
    QVariantList stations() const { return m_stations.toVariantList(); }
    bool stationCatalogueLoaded() const { return m_stationCatalogueLoaded; }
    bool stationCatalogueLoading() const { return m_stationCatalogueLoading; }
    QString stationCatalogueMessage() const { return m_stationCatalogueMessage; }
    bool desktopStationManagement() const;
    QVariantMap stationEditResult() const { return m_stationEditResult.toVariantMap(); }
    QVariantMap stationReports() const { return m_stationReports.toVariantMap(); }
    Q_INVOKABLE void loadStationReports(QString stationId);
    Q_INVOKABLE void detachCharger(QString contractId, QString chargerId);
    Q_INVOKABLE void loadContractNotes(QString contractId, QString chargerId);
    Q_INVOKABLE QVariantMap historicalContractNotes(QString contractId, QString chargerId) const;
    Q_INVOKABLE void saveContractNotes(QString contractId, QString chargerId, int baseRevision, QString notes);
    Q_INVOKABLE void refreshStations();
    Q_INVOKABLE void saveStation(QString id, QString baseUpdated, QVariantMap fields);
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
    QVariantMap localOrderDrafts() const { return m_localOrderDrafts; }
    QVariantList localRecoveryReports() const;
    bool currentOrderReadOnly() const { return isOrderRecovery(m_current); }
    QVariantList workOrders() const { return m_orders.toVariantList(); }
    QVariantList workCandidates() const { return m_orderCandidates.toVariantList(); }
    QString planningDay() const { return m_planningDay; }
    QString orderMessage() const { return m_orderReadError.isEmpty() ? m_orderMessage : m_orderReadError; }
    bool savingOrder() const { return m_savingOrder; }
    Q_INVOKABLE void refreshOrders(bool planner = false);
    Q_INVOKABLE QString newOrderId() const;
    Q_INVOKABLE void saveOrder(QVariantMap plan);
    Q_INVOKABLE void cancelOrder(QString id, int revision);
    Q_INVOKABLE void followOrder(QString id, bool follow);
    Q_INVOKABLE void invalidateOrderItem(QString id, QString item, int revision, QString note);
    Q_INVOKABLE QString prepareOrderReport(QString orderId, QString itemId, QString requestedReport = {});
    Q_INVOKABLE bool prepareLocalRecovery(QString reportId);
    // Session/catalogue actions exposed to QML.
    Q_INVOKABLE QString connectorText(QString text) const { return Checklist::connectorText(text); }
    Q_INVOKABLE void demo();
    Q_INVOKABLE void useDefaultServer();
    Q_INVOKABLE void login(QString email, QString password);
    Q_INVOKABLE void logout();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void selectContract(QString id);
    // Local-first report editing and synchronization.
    Q_INVOKABLE bool openReport(QString chargerId, QString reportId = {});
    Q_INVOKABLE void answer(QString checkpoint, QString value, QString notes, bool readingOnly = false);
    Q_INVOKABLE void beginNotesEdit(QString checkpoint);
    Q_INVOKABLE void setAssessment(QString checkpoint, QString assessment);
    Q_INVOKABLE void attach(QString checkpoint, QUrl file);
    Q_INVOKABLE void attachMany(QString checkpoint, const QList<QUrl> &files);
    Q_INVOKABLE void takePhoto(QString checkpoint);
    Q_INVOKABLE void takeVideo(QString checkpoint);
    Q_INVOKABLE void openVideo(QString photoId);
    Q_INVOKABLE void cropPhoto(QString photoId, qreal x, qreal y, qreal width, qreal height);
    Q_INVOKABLE void rotatePhoto(QString photoId);
    Q_INVOKABLE bool editPhoto(QString photoId, int quarterTurns, qreal x, qreal y, qreal width, qreal height);
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
    Q_INVOKABLE void exportContract(QString contractId, QString kind, QUrl directory);
    Q_INVOKABLE void cancelContractExport();
    // Administrative Excel import and account/contract management.
    Q_INVOKABLE void createContract(QString company, int year, int semester, QString kind);
    Q_INVOKABLE void deleteContract(QString id, QString confirmation);
    Q_INVOKABLE void loadDeletedContracts(QString kind = {});
    Q_INVOKABLE void loadMemberships(QString contractId, bool corrective);
    Q_INVOKABLE void changeMembership(QString contractId, QString memberId, bool corrective, bool restore);
    Q_INVOKABLE void restoreContract(QString id);
    Q_INVOKABLE void analyseExcel(QUrl file);
    Q_INVOKABLE void analyseContractIds(QUrl file, QString contractId);
    Q_INVOKABLE void previewContractIds(QString contractId, QString sheet, int row, QVariantMap columns);
    Q_INVOKABLE void analyseStationExcel(QUrl file);
    Q_INVOKABLE void previewStationExcel(QString sheet, int row, QVariantMap columns);
    Q_INVOKABLE QVariantList reportBrands() const;
    Q_INVOKABLE void previewExcel(QString sheet, int row, QVariantMap columns, QString primary = "", QString secondary = "", QString issuer = "", int brandingRevision = -1);
    Q_INVOKABLE void saveReportPresentation(QString issuer, QString primary, QString secondary, int revision);
    Q_INVOKABLE void commitExcel();
    Q_INVOKABLE void invalidatePreview() {
        m_preview = {};
        emit changed();
    }
    Q_INVOKABLE void loadUsers();
    Q_INVOKABLE void createUser(QString email, QString name, QString password, QString role = "tech");
    Q_INVOKABLE bool resetTechnicianPassword(QString userId, QString password);
    Q_INVOKABLE bool changeOwnPassword(QString current, QString replacement, QString confirmation);
    Q_INVOKABLE void assignUser(QString userId);
    Q_INVOKABLE QString contractId() const {
        return m_contract;
    }
    Q_INVOKABLE QString defaultExport(QString kind) const;
  signals:
    void changed();
    void draftCreated(QString reportId);
    // Frequent progress updates: do not use changed() for every transferred byte.
    void operationChanged();
    void compatibilityChanged();
    void localSaveStateChanged();
    void sectionsChanged();
    void localOrderDraftsChanged();
    void workOrderListChanged();
    void workCandidatesChanged();
    void workOrdersChanged();
    void workOrderSaved(QString id);
    void correctiveContractsReceived(QString stationId, QVariantList contracts, QString error);
    void membershipsReceived(QString contractId, QVariantList rows, QString error);
    void interventionSaved(QString id, bool saved, QString message);
    void contractsChanged();
    void contractTrashChanged();
    void chargersChanged();
    void stationsChanged();
    void contractNotesReceived(QVariantMap record, bool saved);
    void contractNotesFailed(QString contractId, QString chargerId, int code, QString message, bool saving);
    void excelImported(QString summary);
    void ownPasswordChangeFinished(bool saved, QString message);
    void currentChanged();
    void photosChanged();
    void contractExportCancelled();

  private:
    void writeIntervention(QString id, QString suffix, QVariantMap fields);
    struct ContractExportJob {
        QString contractId, contractName, kind, directory, token, server;
        QJsonObject presentation;
        QJsonArray queue, results, localClosures;
        int index = 0, saved = 0, failed = 0;
        bool cancelled = false;
    };
    QSharedPointer<ContractExportJob> m_contractExportJob;
    QJsonObject m_contractExportState, m_passwordReset;
    QThreadPool m_exportPool;
    void nextContractExport();
    void renderContractExport(QJsonObject report, bool localOnly);
    void downloadContractExport(QJsonObject report);
    void completeContractExportItem(QString id, QString identifier, QString staged, QString error, QString source);
    void finishContractExport(QString detail = {});
    void updateContractExportState(QString detail);
    bool correctionAllowed(const QJsonObject &report) const;
    bool receiveReplacement(const QString &oldId, const QString &newId);
    QJsonObject withContractPresentation(QJsonObject report) const;
    void applyContractPresentations();
    void receiveContractPresentation(const QString &cid, const QJsonObject &style);
    void setPresentationSaveState(const QString &cid, const QString &state, const QString &message);
    QJsonObject m_reportPresentationSave;
    void exportPreparedReport(QString kind, QUrl target);
    static QJsonObject stationIdentity(const QJsonObject &details);
    void applyClosedIdentity(const QJsonObject &report);
    void updateWorkOrderStations(const std::function<void(QJsonObject &)> &update);
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
    void checkRestoredClientPolicy();
    bool saveLastUser(const QString &sessionDigest = {}) const;
    bool saveSession();
    void clearSession();
    QJsonObject m_clientCompatibility;
    void trackReply(QNetworkReply *reply, QString method, QString path, bool upload);
    // Operation presentation only; this is not the durable synchronization queue.
    QMap<quint64, QVariantMap> m_transfers;
    QVariantMap m_lastTransfer;
    quint64 m_transferSerial = 0;
    QTimer m_operationNotify;
    int m_syncCompleted = 0, m_syncTotal = 0;
    bool forgetReport(QString reportId, bool preservePending = false);
    // Local tombstones prevent stale journal replay after permanent deletion.
    QSet<QString> m_deletedReports;
    bool attachToReport(QString reportId, QString checkpoint, QUrl file);
    void removeCachedPhoto(const QJsonObject &photo);
    void checkCameraResult();
    void takeCapture(QString key, bool video);
    bool m_cameraPending = false;
    using Failure = std::function<void(int, QString)>;
    using Callback = std::function<void(QByteArray)>;
    QNetworkReply *request(QString method, QString path, QByteArray body, Callback cb,
                 QString content = "application/json", Failure error = {}, bool restoring = false);
    void multipart(QString method, QString path, QList<QPair<QString, QString>> fields,
                   QString filename, QByteArray data, Callback cb, Failure error = {}, QFile *file = nullptr);
    void requestPhoto(QString path, QString destination, qint64 limit,
                      std::function<void(QSharedPointer<NetworkSecurity::FileBody>)> cb, Failure error,
                      QString digest = {}, QSharedPointer<NetworkSecurity::FileBody> body = {},
                      int attempt = 0, std::function<bool()> retryAllowed = {}, bool contractExport = false);
    void exportSaved(const QString &kind, const QUrl &target, bool local = false);
    void status(QString s);
    bool online();
    bool storageWritable();
    bool persist();
    QString writeCheckpoint(const QJsonObject &reports);
    bool checkpointReport(const QJsonObject &report, const QString &failureMessage);
    void loadUser();
    QString cacheDir() const;
    void remember();
    void scheduleSync(int delay = 8000);
    static void recoverOrderSyncConflict(QJsonObject &report);
    static bool independentIntervention(const QJsonObject &report);
    bool canBindInterventionDraft(const QJsonObject &report) const;
    void startSync(bool manual);
    bool releasePhotoSync(QString reportId);
    void syncFailed(int code, QString message);
    bool mergeSync(QJsonObject sent, bool complete = false, QJsonValue serverPhotos = {});
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
    bool m_localReadError = false; // Never replace unreadable recovery evidence with empty state.
    // Emit model-specific notifications only when their contents actually change.
    QJsonObject m_notifiedTemplate, m_notifiedCurrent;
    QJsonArray m_notifiedPhotos;
    // Lazy QVariant projections are implicitly shared, not rebuilt per QML read.
    mutable QJsonObject m_projectedCurrent;
    mutable QVariantMap m_currentProjection, m_photoProjection;
    mutable QJsonArray m_projectedPhotos;
    QJsonArray m_notifiedContracts;
    QJsonArray m_orders, m_orderCandidates;
    QTimer m_orderExpiry;
    QString m_planningDay, m_orderMessage, m_orderReadError;
    quint64 m_orderRead = 0, m_candidateRead = 0;
    bool m_savingOrder = false;
    struct FollowedReport { int revision; int photoCount; };
    QHash<QString, FollowedReport> m_followedReports;
    QVariantMap m_localOrderDrafts;
    bool isOrderRecovery(const QJsonObject &report) const;
    bool hasOtherClosedInterventionReport(const QJsonObject &report) const;
    bool isLocalRecovery(const QJsonObject &report) const;
    QSet<QString> m_attemptedDownloads;
    bool m_orderDownload = false;
    void resetOrders();
    QJsonArray retainedOrders(QJsonArray orders);
    void expireCachedOrders();
    void writeOrder(QString id, QString path, QJsonObject body);
    bool orderAllowsDraft(QString target) const;
    void receiveFollowedReports();
    QJsonObject receivedPhotoPaths(QJsonObject report) const;
    QJsonArray reconcilePhotos(QJsonArray photos, const QJsonArray &serverPhotos, const QJsonArray &removed) const;
    void refreshContracts(bool fromManualSync, bool includeStations = false);
    void refreshCatalogue(QString id, bool force = false);
    void resetStationCatalogue();
    static QJsonArray currentPcveRows(QJsonArray rows);
    bool hasPendingStationClosure(const QString &stationId) const;
    void exchangeContractNotes(const QString &contractId, const QString &chargerId, const QJsonObject &edit);
    void applyStationRecords(const QJsonArray &records, bool complete = false, QJsonArray *memberships = nullptr);
    bool canManageStations() const;
    void beginExcelImport(QUrl file, bool stationsOnly, QString idsContract = {});
    void previewImport(const QString &path, QJsonObject mapping);
    void fetchPhotoForReport(QString reportId, QString photoId);
    void pumpPhotoBatch();
    void photoDownloadFinished(QString reportId, QString photoId, bool success);
    friend struct LemReviewTest; // Isolated native regression harness; no public QML API.
    bool switchingBlocked(bool includeGroupCopy = true) const {
        return m_busy || m_contractExportJob || m_syncing || m_refreshing || !m_downloads.isEmpty() || downloadingPhotos() ||
               attachingFiles() || (includeGroupCopy && !m_qveCopyTarget.isEmpty());
    }
    bool reportActionBlocked(bool includeGroupCopy = true) const {
        return m_busy || m_syncing || m_refreshing || currentReportDownloadingPhotos() ||
               attachingFiles() || (includeGroupCopy && !m_qveCopyTarget.isEmpty());
    }
    void suspendContractSync(QString contractId, QString reason);
    void reconcileContractSync(bool fromManualSync);
    void syncNext(int index, QString notice = {});
    void syncPhotos(int index, QJsonObject report, int photoIndex);
    static bool validRemoteReport(const QJsonObject &report, const QString &expectedId);
    bool getPhotos(QJsonObject report, const QString &expectedId);
    QString localPath(QUrl url) const;
    QNetworkAccessManager net;
    QString m_root, m_server = ServerSettings::defaultUrl(), m_message, m_token, m_contract;
    // Startup can show local work while the server decides whether the saved token is valid.
    QString m_pendingSessionToken;
    bool m_sessionPersisted = false;
    QTimer m_sessionRetry;
    QString m_localOperationTitle;
    bool m_busy = false, m_syncing = false, m_refreshing = false;
    bool m_catalogueLoading = false, m_manualSync = false, m_manualSyncRequested = false;
    // Transient retry backoff, separate from durable conflicts in each report JSON.
    int m_retrySeconds = 30;
    qint64 m_retryNotBefore = 0;
    QString m_syncState = "Guardado no dispositivo";
    QString m_syncReport;
    QStringList m_syncIds;
    QTimer m_autoSync;
    QSet<QString> m_syncConflicts, m_downloads;
    QSet<QString> m_manualRestoredCorrections;
    QJsonObject m_catalogueTimes;
    qint64 m_photoBytes = 0;
    // One explicit report-download batch, sharing four slots with individual downloads.
    QString m_photoBatchReport, m_photoBatchState, m_photoBatchLastError;
    QStringList m_photoBatchQueue;
    QSet<QString> m_photoBatchPending;
    int m_photoBatchTotal = 0, m_photoBatchDone = 0, m_photoBatchFailures = 0;
    bool m_photoBatchStopped = false;
    QJsonObject m_catalogues;
    QJsonObject m_historicalNotes; // By contract and membership, within this server/user's cache.
    static void retainContractNote(QJsonObject &history, const QJsonObject &row);
    // m_reports is keyed by report UUID; m_current mirrors the selected report.
    QJsonObject m_user, m_template, m_current, m_reports, m_importInfo, m_preview, m_mapping;
    QJsonArray m_contracts, m_chargers, m_users, m_deletedContracts;
    QJsonArray m_stations, m_notifiedStations;
    QJsonObject m_stationEditResult, m_stationReports;
    quint64 m_stationReportRequest = 0;
    QString m_stationCatalogueMessage, m_excelImportPath, m_excelName;
    quint64 m_stationRequest = 0;
    bool m_stationCatalogueLoaded = false, m_stationCatalogueLoading = false;
    QByteArray m_excel;
};
