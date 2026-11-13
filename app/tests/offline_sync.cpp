// SPDX-License-Identifier: GPL-3.0-only
// Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

// Run only against the disposable test API provided by test_offline_sync.py.
#include "Lem.h"
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QDateTime>
#include <QTimer>
#include <QImage>
#include <QFile>
#include <QJsonDocument>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QUuid>
#include <functional>
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            qCritical() << "Failed at line" << __LINE__ << ":" << #c;                              \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)
static bool until(std::function<bool()> condition, int timeout = 20000) {
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < timeout) {
        QEventLoop loop;
        QTimer::singleShot(10, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return condition();
}
static bool settled(Lem &c) {
    return until([&] { return !c.busy() && !c.syncing() && !c.refreshing(); });
}
static QJsonObject read(QString path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}
static bool write(QString path, QJsonObject o) {
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(QJsonDocument(o).toJson()) > 0;
}
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    app.setApplicationVersion("0.1.57");
    app.setOrganizationName("Lem-Offline-Test");
    app.setApplicationName("Lem Offline Test");
    QTemporaryDir temp;
    QImage image(64, 48, QImage::Format_RGB32);
    image.fill(Qt::darkCyan);
    const auto source = temp.path() + "/original.png";
    CHECK(image.save(source));
    {
        Lem c(temp.path() + "/journal");
        c.demo();
        c.attach("EST.01", QUrl::fromLocalFile(source));
        const auto rid = c.current()["id"].toString();
        const auto photo = c.current()["photos"].toList().first().toMap()["path"].toString();
        const auto dir =
            QDir::cleanPath(QFileInfo(QUrl(photo).toLocalFile()).absolutePath() + "/..");
        const auto cache = dir + "/cache.json";
        QFile old(cache);
        CHECK(old.open(QIODevice::ReadOnly));
        const auto before = old.readAll();
        old.close();
        for (int i = 0; i < 50; ++i)
            c.answer("SITE.02", "NOK", "Nota " + QString::number(i));
        CHECK(until([&] { return !c.saving(); }));
        CHECK(QDir(dir + "/edits").entryList({"*.json"}, QDir::Files).size() == 1);
        QFile same(cache);
        CHECK(same.open(QIODevice::ReadOnly));
        CHECK(same.readAll() == before);
        same.close();
        Lem restored(temp.path() + "/journal");
        restored.demo();
        restored.openReport("", rid);
        CHECK(
            restored.current()["payload"].toMap()["answers"].toMap()["SITE.02"].toMap()["notes"] ==
            "Nota 49");
        // An acknowledged closed report is eligible for storage eviction; a draft is not.
        auto manifest = read(cache);
        auto reports = manifest["reports"].toObject();
        auto closed = QJsonObject::fromVariantMap(restored.current());
        CHECK(closed["_defer_photo_sync"].toBool());
        for (const auto &entry : QDir(dir + "/edits").entryList({"*.json"}, QDir::Files))
            CHECK(QFile::remove(dir + "/edits/" + entry));
        closed["status"] = "closed";
        closed["dirty"] = false;
        closed["last_access"] = 0;
        auto photos = closed["photos"].toArray();
        auto p = photos[0].toObject();
        p["uploaded"] = true;
        photos[0] = p;
        closed["photos"] = photos;
        reports[rid] = closed;
        manifest["reports"] = reports;
        CHECK(write(cache, manifest));
        const auto pinnedId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto pinnedPhoto = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto pinnedPath = dir + "/photos/" + pinnedPhoto + ".png";
        QFile pinnedFile(pinnedPath);
        CHECK(pinnedFile.open(QIODevice::WriteOnly));
        CHECK(pinnedFile.resize(257LL * 1024 * 1024));
        pinnedFile.close();
        auto pinned = closed;
        pinned["id"] = pinnedId;
        pinned["offline_photos"] = true;
        auto pp = p;
        pp["id"] = pinnedPhoto;
        pp["path"] = QUrl::fromLocalFile(pinnedPath).toString();
        pinned["photos"] = QJsonArray{pp};
        reports[pinnedId] = pinned;
        manifest["reports"] = reports;
        CHECK(write(cache, manifest));
        Lem cleaner(temp.path() + "/journal");
        cleaner.demo();
        CHECK(!QFileInfo::exists(QUrl(photo).toLocalFile()));
        CHECK(QFileInfo::exists(source));
        CHECK(QFileInfo::exists(pinnedPath));
        cleaner.openReport("", pinnedId);
        CHECK(cleaner.current()["offline_photos"].toBool());
        cleaner.releaseReportPhotos();
        CHECK(!cleaner.current()["offline_photos"].toBool());
        cleaner.openReport("demo-charger");
        cleaner.cleanPhotoCache();
        CHECK(!QFileInfo::exists(pinnedPath));
        cleaner.openReport("", rid);
        CHECK(cleaner.current()["photos"].toList().size() == 1);
        CHECK(cleaner.current()["photos"].toList().first().toMap()["path"].toString().isEmpty());
    }
    {
        Lem c(temp.path() + "/protected");
        c.demo();
        c.attach("EST.01", QUrl::fromLocalFile(source));
        const auto rid = c.current()["id"].toString();
        const auto path =
            QUrl(c.current()["photos"].toList().first().toMap()["path"].toString()).toLocalFile();
        // Sparse fixture: exercise the 256 MiB budget without allocating an image in RAM.
        QFile large(path);
        CHECK(large.open(QIODevice::WriteOnly));
        CHECK(large.resize(257LL * 1024 * 1024));
        large.close();
        c.cleanPhotoCache();
        CHECK(QFileInfo::exists(path));
        CHECK(c.photoCacheMB() > 256);
        Lem restored(temp.path() + "/protected");
        CHECK(!restored.authenticated());
        restored.demo();
        CHECK(!restored.authenticated());
        restored.openReport("", rid);
        CHECK(QFileInfo::exists(path));
        CHECK(restored.current()["photos"].toList().size() == 1);
    }
    {
        Lem c(temp.path() + "/slow-storage");
        c.demo();
        int templateSignals = 0;
        QObject::connect(&c, &Lem::sectionsChanged, &app, [&] { ++templateSignals; });
        qputenv("LEM_TEST_WRITE_DELAY_MS", "500");
        QElapsedTimer elapsed;
        elapsed.start();
        c.answer("SITE.02", "OK", "primeira");
        const auto firstMs = elapsed.elapsed();
        CHECK(firstMs < 100);
        CHECK(c.saving());
        // Start the deliberately slow atomic write, then edit again during that write.
        QElapsedTimer running;
        running.start();
        CHECK(until([&] { return running.elapsed() > 50; }, 1000));
        elapsed.restart();
        c.answer("SITE.02", "NOK", "última");
        const auto secondMs = elapsed.elapsed();
        CHECK(secondMs < 100);
        int heartbeats = 0;
        QTimer heartbeat;
        heartbeat.setInterval(10);
        QObject::connect(&heartbeat, &QTimer::timeout, &app, [&] { ++heartbeats; });
        heartbeat.start();
        CHECK(until([&] { return !c.saving(); }, 4000));
        heartbeat.stop();
        CHECK(heartbeats > 30);
        CHECK(!c.localSaveError());
        CHECK(templateSignals == 0);
        qunsetenv("LEM_TEST_WRITE_DELAY_MS");
        const auto rid = c.current()["id"].toString();
        Lem restored(temp.path() + "/slow-storage");
        restored.demo();
        restored.openReport("", rid);
        CHECK(
            restored.current()["payload"].toMap()["answers"].toMap()["SITE.02"].toMap()["notes"] ==
            "última");
        qInfo() << "PASS: 500 ms storage write; answer calls (ms)" << firstMs << secondMs
                << "; event-loop ticks" << heartbeats
                << "; latest edit durable; template model unchanged";
    }
    {
        const auto path = temp.path() + "/shutdown";
        QString rid;
        {
            Lem c(path);
            c.demo();
            rid = c.current()["id"].toString();
            c.answer("SITE.02", "OK", "última ao sair");
            CHECK(c.saving());
        }
        Lem restored(path);
        restored.demo();
        restored.openReport("", rid);
        CHECK(
            restored.current()["payload"].toMap()["answers"].toMap()["SITE.02"].toMap()["notes"] ==
            "última ao sair");
    }
    {
        Lem c(temp.path() + "/write-failure");
        c.demo();
        c.attach("EST.01", QUrl::fromLocalFile(source));
        const auto photo = c.current()["photos"].toList()[0].toMap()["path"].toString();
        const auto dir =
            QDir::cleanPath(QFileInfo(QUrl(photo).toLocalFile()).absolutePath() + "/../edits");
        const auto names = QDir(dir).entryList({"*.json"}, QDir::Files);
        CHECK(names.size() == 1);
        const auto journal = dir + "/" + names.first();
        CHECK(QFile::remove(journal));
        CHECK(QDir().mkdir(journal));
        c.answer("SITE.02", "NOK", "preservar após falha");
        CHECK(until([&] { return c.localSaveError(); }, 2000));
        CHECK(c.saving());
        CHECK(c.current()["payload"].toMap()["answers"].toMap()["SITE.02"].toMap()["notes"] ==
              "preservar após falha");
        CHECK(QDir().rmdir(journal));
        CHECK(until([&] { return !c.saving(); }, 4000));
        CHECK(!c.localSaveError());
        const auto rid = c.current()["id"].toString();
        Lem restored(temp.path() + "/write-failure");
        restored.demo();
        restored.openReport("", rid);
        CHECK(
            restored.current()["payload"].toMap()["answers"].toMap()["SITE.02"].toMap()["notes"] ==
            "preservar após falha");
    }
    {
        const auto path = temp.path() + "/delete-local";
        Lem c(path);
        c.demo();
        c.attach("EST.01", QUrl::fromLocalFile(source));
        c.answer("SITE.02", "OK", "apagar");
        const auto rid = c.current()["id"].toString();
        const auto stale = QJsonObject::fromVariantMap(c.current());
        const auto file =
            QUrl(c.current()["photos"].toList()[0].toMap()["path"].toString()).toLocalFile();
        const auto dir = QDir::cleanPath(QFileInfo(file).absolutePath() + "/..");
        c.deleteReport(rid);
        CHECK(c.current().isEmpty());
        CHECK(!QFileInfo::exists(file));
        CHECK(QFileInfo::exists(source));
        // Even a stale journal left by a crash must not restore a deleted draft.
        CHECK(write(dir + "/edits/stale.json", stale));
        Lem restored(path);
        restored.demo();
        restored.openReport("", rid);
        CHECK(restored.current()["id"].toString() != rid);
    }
    {
        // Explicit cleanup ignores age, budget, pin and current report, but preserves unsent work.
        const auto path = temp.path() + "/manual-clean";
        Lem c(path);
        c.demo();
        c.attach("EST.01", QUrl::fromLocalFile(source));
        const auto closedId = c.current()["id"].toString();
        const auto photo =
            QUrl(c.current()["photos"].toList()[0].toMap()["path"].toString()).toLocalFile();
        const auto dir = QDir::cleanPath(QFileInfo(photo).absolutePath() + "/..");
        auto manifest = read(dir + "/cache.json");
        auto reports = manifest["reports"].toObject();
        auto closed = QJsonObject::fromVariantMap(c.current());
        closed["status"] = "closed";
        closed["dirty"] = false;
        closed["revision"] = 2;
        closed["offline_photos"] = true;
        closed["last_access"] = double(QDateTime::currentSecsSinceEpoch());
        auto photos = closed["photos"].toArray();
        auto p = photos[0].toObject();
        p["uploaded"] = true;
        photos[0] = p;
        closed["photos"] = photos;
        reports[closedId] = closed;
        auto extraClosed = closed;
        extraClosed["id"] = "ffffffff-ffff-ffff-ffff-ffffffffffff";
        extraClosed["photos"] = QJsonArray{};
        reports[extraClosed["id"].toString()] = extraClosed;
        const auto draftId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        auto draft = closed;
        draft["id"] = draftId;
        draft["status"] = "draft";
        draft["dirty"] = true;
        const auto draftPhoto = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto draftPath = dir + "/photos/" + draftPhoto + ".png";
        CHECK(QFile::copy(photo, draftPath));
        auto dp = p;
        dp["id"] = draftPhoto;
        dp["uploaded"] = false;
        dp["path"] = QUrl::fromLocalFile(draftPath).toString();
        draft["photos"] = QJsonArray{dp};
        reports[draftId] = draft;
        const auto dirtyId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        auto dirty = draft;
        dirty["id"] = dirtyId;
        dirty["status"] = "closed";
        reports[dirtyId] = dirty;
        const auto syncedDraftId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        auto syncedDraft = closed;
        syncedDraft["id"] = syncedDraftId;
        syncedDraft["status"] = "draft";
        const auto syncedPhotoId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto syncedPhotoPath = dir + "/photos/" + syncedPhotoId + ".png";
        CHECK(QFile::copy(photo, syncedPhotoPath));
        auto sp = p;
        sp["id"] = syncedPhotoId;
        sp["path"] = QUrl::fromLocalFile(syncedPhotoPath).toString();
        syncedDraft["photos"] = QJsonArray{sp};
        reports[syncedDraftId] = syncedDraft;
        const auto conflictId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        auto conflict = syncedDraft;
        conflict["id"] = conflictId;
        conflict["_conflict"] = true;
        conflict["photos"] = QJsonArray{};
        reports[conflictId] = conflict;
        const auto deletionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        auto deletion = conflict;
        deletion["id"] = deletionId;
        deletion.remove("_conflict");
        deletion["deleted_photos"] = QJsonArray{syncedPhotoId};
        reports[deletionId] = deletion;
        manifest["reports"] = reports;
        CHECK(write(dir + "/cache.json", manifest));
        for (const auto &name : QDir(dir + "/edits").entryList({"*.json"}, QDir::Files))
            CHECK(QFile::remove(dir + "/edits/" + name));
        Lem cleaner(path);
        cleaner.demo();
        cleaner.openReport("", closedId);
        CHECK(cleaner.current()["offline_photos"].toBool());
        cleaner.clearLocalCopies();
        CHECK(cleaner.current()["id"].toString() == closedId);
        CHECK(!QFileInfo::exists(photo));
        CHECK(!QFileInfo::exists(syncedPhotoPath));
        const auto cleanedReports = read(dir + "/cache.json")["reports"].toObject();
        CHECK(cleanedReports.contains(syncedDraftId));
        CHECK(cleanedReports.contains(conflictId) && cleanedReports.contains(deletionId));
        CHECK(QFileInfo::exists(draftPath));
        CHECK(QFileInfo::exists(source));
        cleaner.openReport("", draftId);
        CHECK(cleaner.current()["photos"].toList().size() == 1);
        cleaner.openReport("", dirtyId);
        CHECK(cleaner.current()["id"].toString() == dirtyId);
        Lem restored(path);
        restored.demo();
        restored.openReport("", closedId);
        CHECK(restored.current()["id"].toString() == closedId);
        restored.openReport("", draftId);
        CHECK(restored.current()["id"].toString() == draftId);
    }
    {
        // Closed offline work stays protected through restart; copies get independent
        // IDs, dates and files, and cannot accidentally remove the source's images.
        const auto root = temp.path() + "/conflict-copy";
        QString originalId, originalPhoto, originalPath, copyId, copyPath;
        {
            Lem c(root);
            c.demo();
            c.attach("EST.01", QUrl::fromLocalFile(source));
            c.answer("SITE.02", "NOK", "Preservar esta observação");
            CHECK(until([&] { return !c.saving(); }));
            originalId = c.current()["id"].toString();
            auto r = QJsonObject::fromVariantMap(c.current());
            auto p = r["photos"].toArray()[0].toObject();
            originalPhoto = p["id"].toString();
            originalPath = QUrl(p["path"].toString()).toLocalFile();
            const auto cache = QFileInfo(originalPath).absolutePath() + "/../cache.json";
            auto manifest = read(cache);
            auto reports = manifest["reports"].toObject();
            r["_conflict"] = true;
            r["_conflict_reason"] = "Propriedade transferida";
            r["status"] = "closed";
            r["started"] = "2020-01-01T00:00:00Z";
            r["finished"] = "2020-01-02T00:00:00Z";
            auto payload = r["payload"].toObject();
            payload["finished"] = "2020-01-02T00:00:00Z";
            r["payload"] = payload;
            reports[originalId] = r;
            manifest["reports"] = reports;
            CHECK(write(cache, manifest));
        }
        {
            Lem restored(root);
            restored.demo();
            restored.openReport("", originalId);
            CHECK(restored.current()["_conflict"].toBool());
            const auto before = restored.current()["payload"];
            restored.answer("SITE.02", "OK", "não alterar");
            CHECK(restored.current()["payload"] == before);
            restored.copyLocalReport();
            CHECK(settled(restored));
            copyId = restored.current()["id"].toString();
            CHECK(copyId != originalId);
            CHECK(restored.current()["status"] == "draft" && restored.current()["dirty"].toBool() &&
                  restored.current()["revision"].toInt() == 0);
            CHECK(!restored.current()["_conflict"].toBool());
            CHECK(!restored.current().contains("finished"));
            CHECK(restored.current()["started"] != "2020-01-01T00:00:00Z");
            const auto payload = restored.current()["payload"].toMap();
            CHECK(payload["source_report_id"] == originalId);
            CHECK(!payload.contains("finished"));
            CHECK(payload["answers"].toMap()["SITE.02"].toMap()["notes"] ==
                  "Preservar esta observação");
            auto p = restored.current()["photos"].toList()[0].toMap();
            CHECK(p["id"].toString() != originalPhoto && !p["uploaded"].toBool());
            copyPath = QUrl(p["path"].toString()).toLocalFile();
            QFile a(originalPath), b(copyPath);
            CHECK(a.open(QIODevice::ReadOnly) && b.open(QIODevice::ReadOnly));
            CHECK(a.readAll() == b.readAll());
            restored.openReport("", originalId);
            CHECK(restored.current()["_conflict_copy_id"] == copyId);
            restored.clearLocalCopies();
            CHECK(QFileInfo::exists(originalPath) && QFileInfo::exists(copyPath));
            restored.openReport("", copyId);
            restored.removePhoto(p["id"].toString());
            CHECK(QFileInfo::exists(originalPath));
        }
        {
            Lem restored(root);
            restored.demo();
            restored.openReport("", originalId);
            CHECK(restored.current()["_conflict"].toBool());
            CHECK(QFileInfo::exists(originalPath));
        }
    }
    if (!app.arguments().contains("--network")) {
        qInfo() << "PASS: edit journals/restart and protected draft/closed-photo cache policy";
        return 0;
    }
    Lem c(temp.path() + "/network");
    bool sawTransfer = false, sawPhotoContext = false, sawWaiting = false, validProgress = true;
    QObject::connect(&c, &Lem::operationChanged, &app, [&] {
        const auto overall = c.operationStatus();
        const auto progress = overall["progress"].toDouble();
        validProgress &= progress >= 0 && progress <= 1;
        for (const auto &value : c.operationTransfers()) {
            const auto t = value.toMap();
            sawTransfer = true;
            sawWaiting |= t["waiting"].toBool();
            sawPhotoContext |= t["context"].toString().contains("SITE.02");
            validProgress &= t["progress"].toDouble() >= 0 && t["progress"].toDouble() <= 1;
        }
    });
    c.setServer("http://127.0.0.1:18765");
    c.login("admin@lem.pt", "IntegrationPassword123!");
    CHECK(settled(c));
    CHECK(!c.user().isEmpty());
    CHECK(c.authenticated());
    c.createContract("Offline performance", 2026, 1, "TRE");
    CHECK(settled(c));
    const auto contract = c.contracts().first().toMap()["id"].toString();
    c.selectContract(contract);
    CHECK(settled(c));
    c.analyseExcel(
        QUrl::fromLocalFile(app.arguments().value(app.arguments().indexOf("--xlsx") + 1)));
    CHECK(settled(c));
    c.previewExcel("Parque", 1,
                   {{"external_id", "Identificador"}, {"model", "Modelo"}, {"serial", "Série"}, {"connectors", "Tomadas"}});
    CHECK(settled(c));
    c.commitExcel();
    CHECK(settled(c));
    CHECK(!c.chargers().isEmpty());
    const auto charger = c.chargers().first().toMap()["id"].toString();
    c.openReport(charger);
    const auto frozenTests = c.current()["template"].toMap()["load_test_connectors"].toList();
    CHECK(frozenTests == QVariantList({"CCS2", "CCS2"}));
    bool foundFirst = false, foundSecond = false;
    for (const auto &section : c.sections())
        for (const auto &item : section.toMap()["items"].toList()) {
            if (item.toMap()["id"] == "CARG.01.01") foundFirst = item.toMap()["label"] == "Teste de carga do conector CCS2 #1";
            if (item.toMap()["id"] == "CARG.01.02") foundSecond = item.toMap()["label"] == "Teste de carga do conector CCS2 #2";
        }
    CHECK(foundFirst && foundSecond);
    const auto rid = c.current()["id"].toString();
    c.answer("SITE.02", "OK", "antes do envio");
    c.attach("SITE.02", QUrl::fromLocalFile(source));
    const auto first = c.current()["photos"].toList().first().toMap()["id"].toString();
    bool edited = false, removed = false, nonblocking = true;
    QString nextJob;
    c.sync();
    CHECK(c.syncing());
    CHECK(!c.busy());
    QTimer::singleShot(150, &app, [&] {
        nonblocking &= c.syncing() && !c.busy();
        c.answer("SITE.02", "NOK", "escrito durante envio");
        c.attach("SITE.02", QUrl::fromLocalFile(source));
        edited = true;
    });
    QTimer::singleShot(500, &app, [&] {
        nonblocking &= c.syncing() && !c.busy();
        c.removePhoto(first);
        removed = true;
        c.openReport(charger);
        nextJob = c.current()["id"].toString();
        c.answer("FIN.03", "NEXT_JOB", "");
    });
    CHECK(settled(c));
    CHECK(edited && removed && nonblocking);
    CHECK(c.current()["id"].toString() == nextJob && nextJob != rid);
    c.openReport("", rid);
    CHECK(!c.busy());
    CHECK(c.current()["dirty"].toBool());
    CHECK(c.current()["photos"].toList().size() == 1);
    CHECK(c.current()["deleted_photos"].toList().size() == 1);
    CHECK(c.current()["payload"].toMap()["answers"].toMap()["SITE.02"].toMap()["notes"] ==
          "escrito durante envio");
    c.sync();
    CHECK(settled(c));
    CHECK(!c.current()["dirty"].toBool());
    CHECK(c.current()["deleted_photos"].toList().isEmpty());
    Lem other(temp.path() + "/other");
    other.setServer("http://127.0.0.1:18765");
    other.login("admin@lem.pt", "IntegrationPassword123!");
    CHECK(settled(other));
    other.selectContract(contract);
    CHECK(settled(other));
    other.openReport("", rid);
    CHECK(settled(other));
    CHECK(other.current()["photos"].toList().size() == 1);
    auto remote = other.current()["photos"].toList().first().toMap();
    CHECK(remote["id"].toString() != first);
    CHECK(remote["path"].toString().isEmpty());
    CHECK(other.current()["payload"].toMap()["answers"].toMap()["SITE.02"].toMap()["notes"] ==
          "escrito durante envio");
    // Repeat cached opening without requests, then explicitly fetch the one photo.
    other.openReport("", rid);
    CHECK(!other.busy());
    const auto pid = remote["id"].toString();
    other.fetchPhoto(pid);
    CHECK(until([&] { return !other.photoDownloading(pid); }));
    CHECK(!other.current()["photos"].toList().first().toMap()["path"].toString().isEmpty());
    // Bulk download reuses the one cached image and pins the report for offline use.
    c.openReport("", rid);
    for (int i = 0; i < 3; ++i)
        c.attach("SITE.02", QUrl::fromLocalFile(source));
    c.sync();
    CHECK(settled(c));
    other.refreshReport();
    CHECK(settled(other));
    CHECK(other.photoDownloadSummary()["total"].toInt() == 4);
    CHECK(other.photoDownloadSummary()["missing"].toInt() == 3);
    CHECK(other.photoDownloadSummary()["bytes"].toLongLong() > 0);
    other.downloadReportPhotos();
    CHECK(other.downloadingPhotos() && !other.busy());
    other.openReport(charger);
    const auto visibleDuringDownload = other.current()["id"].toString();
    CHECK(visibleDuringDownload != rid);
    CHECK(until([&] { return !other.downloadingPhotos(); }));
    CHECK(other.current()["id"].toString() == visibleDuringDownload);
    other.openReport("", rid);
    CHECK(other.current()["offline_photos"].toBool());
    CHECK(other.photoDownloadSummary()["missing"].toInt() == 0);
    other.downloadReportPhotos();
    CHECK(!other.downloadingPhotos());
    for (auto value : other.current()["photos"].toList())
        CHECK(QFile::remove(QUrl(value.toMap()["path"].toString()).toLocalFile()));
    other.downloadReportPhotos();
    other.cancelReportPhotoDownloads();
    CHECK(until([&] { return !other.downloadingPhotos(); }));
    CHECK(other.photoDownloadSummary()["missing"].toInt() == 2);
    other.downloadReportPhotos();
    CHECK(until([&] { return !other.downloadingPhotos(); }));
    CHECK(other.photoDownloadSummary()["missing"].toInt() == 0);
    // The runner fails the ninth GET once; stop remaining work and allow a selective retry.
    for (auto value : other.current()["photos"].toList())
        CHECK(QFile::remove(QUrl(value.toMap()["path"].toString()).toLocalFile()));
    other.downloadReportPhotos();
    CHECK(until([&] { return !other.downloadingPhotos(); }));
    CHECK(other.photoDownloadSummary()["missing"].toInt() > 0);
    CHECK(other.photoDownloadState().contains("interrompido"));
    other.downloadReportPhotos();
    CHECK(until([&] { return !other.downloadingPhotos(); }));
    CHECK(other.photoDownloadSummary()["missing"].toInt() == 0);
    other.releaseReportPhotos();
    CHECK(!other.current()["offline_photos"].toBool());
    // A lost PUT acknowledgement must not turn newer local typing into a conflict.
    c.openReport(charger);
    const auto retryId = c.current()["id"].toString();
    c.answer("FIN.03", "LOSE_ACK", "");
    c.sync();
    QTimer::singleShot(100, &app, [&] { c.answer("FIN.03", "NEWER", ""); });
    CHECK(settled(c));
    CHECK(c.current()["dirty"].toBool());
    c.sync();
    CHECK(settled(c));
    CHECK(!c.current()["dirty"].toBool());
    CHECK(c.current()["payload"].toMap()["answers"].toMap()["FIN.03"].toMap()["value"] == "NEWER");
    // No manual sync: the idle queue drains while the app remains open.
    c.answer("FIN.03", "AUTO", "");
    CHECK(until([&] { return !c.current()["dirty"].toBool() && !c.syncing(); }, 15000));
    c.sync();
    CHECK(!c.syncing());
    CHECK(c.current()["id"].toString() == retryId);
    // Complete while an older draft snapshot is in flight, then start another job.
    for (auto sv : c.sections())
        for (auto iv : sv.toMap()["items"].toList())
            c.answer(iv.toMap()["id"].toString(), "NA", "Sem conector/veículo disponível");
    bool finishedDuringSync = false;
    QString following;
    c.sync();
    QTimer::singleShot(100, &app, [&] {
        c.finish();
        finishedDuringSync = c.current()["status"].toString() == "closed" && !c.busy();
        c.openReport(charger);
        following = c.current()["id"].toString();
    });
    CHECK(settled(c));
    CHECK(finishedDuringSync && c.current()["id"].toString() == following);
    c.openReport("", retryId);
    CHECK(c.current()["status"].toString() == "closed" && c.current()["dirty"].toBool());
    c.sync();
    CHECK(settled(c));
    CHECK(!c.current()["dirty"].toBool());
    // Attachments stay local across idle/resume schedules until a group boundary.
    c.openReport(charger);
    const auto batchId = c.current()["id"].toString();
    c.attach("SITE.02", QUrl::fromLocalFile(source));
    CHECK(c.current()["_defer_photo_sync"].toBool());
    QElapsedTimer idle;
    idle.start();
    CHECK(until([&] { return idle.elapsed() > 9000; }, 10000));
    CHECK(c.current()["revision"].toInt() == 0 && !c.syncing());
    CHECK(!c.current()["photos"].toList()[0].toMap()["uploaded"].toBool());
    c.reportSyncBoundary();
    CHECK(until([&] { return c.syncing(); }, 4000));
    c.attach("SITE.02",
             QUrl::fromLocalFile(
                 source)); // New photograph during the older batch waits for the next boundary.
    CHECK(settled(c));
    CHECK(c.current()["id"].toString() == batchId && c.current()["dirty"].toBool());
    CHECK(c.current()["photos"].toList()[0].toMap()["uploaded"].toBool());
    CHECK(!c.current()["photos"].toList()[1].toMap()["uploaded"].toBool());
    idle.restart();
    CHECK(until([&] { return idle.elapsed() > 9000; }, 10000));
    CHECK(!c.current()["photos"].toList()[1].toMap()["uploaded"].toBool());
    c.reportSyncBoundary();
    CHECK(until([&] { return !c.current()["dirty"].toBool() && !c.syncing(); }, 6000));
    // Manual local cleanup preserves the server report and reopens without old image paths.
    c.openReport("", rid);
    for (auto sv : c.sections())
        for (auto iv : sv.toMap()["items"].toList())
            c.answer(iv.toMap()["id"].toString(), "NA", "Sem conector/veículo disponível");
    c.finish();
    CHECK(settled(c));
    CHECK(c.current()["status"].toString() == "closed");
    c.sync();
    CHECK(settled(c));
    CHECK(!c.current()["dirty"].toBool());
    c.clearLocalCopies();
    CHECK(c.current()["id"].toString() == rid);
    c.openReport("", rid);
    CHECK(settled(c));
    CHECK(c.current()["id"].toString() == rid && c.current()["status"].toString() == "closed");
    CHECK(c.current()["photos"].toList().size() == 4);
    for (auto value : c.current()["photos"].toList())
        CHECK(value.toMap()["path"].toString().isEmpty());
    c.openReport("", batchId);
    CHECK(settled(c)); // Await this client's refresh before testing deletion.
    // Remote removal is propagated to an already cached dirty copy via HTTP 410.
    other.openReport("", batchId);
    CHECK(settled(other));
    other.answer("SITE.02", "OK", "cópia antiga");
    c.deleteReport(batchId);
    CHECK(settled(c));
    CHECK(c.current().isEmpty());
    other.sync();
    CHECK(settled(other));
    CHECK(other.current().isEmpty());
    other.openReport("", batchId);
    CHECK(other.current().isEmpty());
    other.openReport("", retryId);
    CHECK(settled(other));
    c.openReport("", retryId);
    CHECK(settled(c));
    CHECK(c.current()["status"].toString() == "closed");
    c.deleteReport(retryId);
    CHECK(settled(c));
    CHECK(c.current().isEmpty());
    other.refresh();
    CHECK(settled(other));
    CHECK(other.current().isEmpty());
    other.openReport("", retryId);
    CHECK(other.current().isEmpty());
    // Ownership change while another device edits offline: preserve local work,
    // consult server separately, then copy all photographs with fresh identifiers.
    c.openReport(charger);
    c.answer("SITE.02", "OK", "servidor");
    c.attach("EST.01", QUrl::fromLocalFile(source));
    c.sync();
    CHECK(settled(c));
    CHECK(!c.current()["dirty"].toBool());
    const auto ownedId = c.current()["id"].toString();
    other.openReport("", ownedId);
    CHECK(settled(other));
    other.answer("SITE.02", "NOK", "alteração offline importante");
    other.attach("SITE.02", QUrl::fromLocalFile(source));
    c.createUser("ownership@example.test", "Novo proprietário", "OwnershipPassword123!");
    CHECK(settled(c));
    QString target;
    for (auto u : c.users())
        if (u.toMap()["email"] == "ownership@example.test")
            target = u.toMap()["id"].toString();
    CHECK(!target.isEmpty());
    c.assignUser(target);
    CHECK(settled(c));
    c.loadReportOwners();
    CHECK(settled(c));
    c.transferReport(target);
    CHECK(settled(c));
    CHECK(c.current()["owner_id"] == target);
    other.sync();
    CHECK(settled(other));
    CHECK(other.current()["_conflict"].toBool());
    const auto localPayload = other.current()["payload"];
    CHECK(other.current()["photos"].toList().size() == 2);
    other.viewServerReport();
    CHECK(settled(other));
    CHECK(other.current()["_conflict_remote"].toMap()["owner_id"] == target);
    CHECK(other.current()["payload"] == localPayload);
    other.copyLocalReport();
    CHECK(other.current()["id"] == ownedId);
    CHECK(other.message().contains("Descarregue"));
    other.downloadReportPhotos();
    CHECK(until([&] { return !other.downloadingPhotos(); }));
    CHECK(other.photoDownloadSummary()["missing"].toInt() == 0);
    other.copyLocalReport();
    CHECK(settled(other));
    const auto independent = other.current()["id"].toString();
    CHECK(independent != ownedId);
    CHECK(other.current()["payload"].toMap()["answers"].toMap()["SITE.02"].toMap()["notes"] ==
          "alteração offline importante");
    CHECK(other.current()["photos"].toList().size() == 2);
    other.answer("SITE.02", "OK", "cópia entretanto alterada");
    other.openReport("", ownedId);
    CHECK(other.current()["_conflict"].toBool());
    other.useServerReport();
    CHECK(!other.busy() && other.current()["_conflict"].toBool());
    CHECK(other.message().contains("outra cópia"));
    other.copyLocalReport();
    CHECK(settled(other));
    other.openReport("", ownedId);
    other.useServerReport();
    CHECK(settled(other));
    CHECK(!other.current()["dirty"].toBool() && other.current()["owner_id"] == target);
    CHECK(!other.current()["_conflict"].toBool());
    other.openReport("", independent);
    CHECK(other.current()["owner_id"] == other.user()["id"]);
    // A revision conflict between two devices belonging to the same owner also
    // offers a copy; ownership alone is insufficient to allow a stale overwrite.
    c.openReport(charger);
    c.answer("SITE.02", "OK", "base");
    c.sync();
    CHECK(settled(c));
    const auto revisionId = c.current()["id"].toString();
    other.openReport("", revisionId);
    CHECK(settled(other));
    other.answer("SITE.02", "NOK", "outra versão offline");
    c.answer("SITE.02", "OK", "versão mais recente");
    c.sync();
    CHECK(settled(c));
    other.sync();
    CHECK(settled(other));
    CHECK(other.current()["_conflict"].toBool());
    other.copyLocalReport();
    CHECK(settled(other));
    CHECK(other.current()["id"].toString() != revisionId);
    CHECK(other.current()["payload"].toMap()["source_report_id"] == revisionId);
    CHECK(sawTransfer && sawPhotoContext && validProgress);
    CHECK(c.operationTransfers().isEmpty());
    // A fully uploaded draft keeps answers/metadata locally while its cached
    // evidence bytes are removed; server evidence remains available.
    c.attach("EST.01", QUrl::fromLocalFile(source));
    c.reportSyncBoundary();
    c.sync();
    CHECK(settled(c));
    CHECK(!c.current()["dirty"].toBool());
    CHECK(c.current()["status"] == "draft");
    const auto draftPhotoPath = QUrl(c.current()["photos"].toList().first().toMap()["path"].toString()).toLocalFile();
    CHECK(QFileInfo::exists(draftPhotoPath));
    c.clearLocalCopies();
    CHECK(c.current()["id"].toString() == revisionId && !QFileInfo::exists(draftPhotoPath));
    c.openReport("", revisionId);
    CHECK(settled(c));
    CHECK(c.current()["id"].toString() == revisionId && c.current()["status"] == "draft");
    CHECK(c.current()["payload"].toMap()["answers"].toMap()["SITE.02"].toMap()["value"] == "OK");
    CHECK(c.current()["photos"].toList().size() == 1);
    CHECK(c.current()["photos"].toList().first().toMap()["path"].toString().isEmpty());
    qInfo() << "PASS: synced draft metadata retained locally; cached photograph removed; server evidence preserved";
    c.answer("FIN.03", "LOSE_DELETE_ACK", "");
    c.sync();
    CHECK(settled(c) && !c.current()["dirty"].toBool());
    const auto removedPhoto = c.current()["photos"].toList().first().toMap()["id"].toString();
    c.removePhoto(removedPhoto);
    c.sync();
    CHECK(settled(c));
    CHECK(c.current()["dirty"].toBool() && !c.current()["_conflict"].toBool());
    const auto deleteCache = QDir::cleanPath(QFileInfo(draftPhotoPath).absolutePath() + "/../cache.json");
    const auto intent = read(deleteCache)["reports"].toObject()[revisionId].toObject();
    CHECK(intent["_inflight_delete_id"].toString() == removedPhoto);
    c.sync();
    CHECK(settled(c));
    CHECK(!c.current()["dirty"].toBool() && !c.current()["_conflict"].toBool());
    CHECK(c.current()["photos"].toList().isEmpty());
    CHECK(c.current()["deleted_photos"].toList().isEmpty());
    CHECK(!c.current().contains("_inflight_delete_id"));
    qInfo() << "PASS: lost DELETE acknowledgement recovered with durable matching intent";

    c.openReport(charger);
    const auto a = c.current()["id"].toString();
    c.openReport(charger);
    const auto b = c.current()["id"].toString();
    const auto rejected = a < b ? a : b;
    const auto accepted = a < b ? b : a;
    c.openReport("", rejected);
    c.answer("FIN.03", "REJECT_413", "");
    c.openReport("", accepted);
    c.answer("FIN.03", "NEXT_AFTER_413", "");
    c.sync();
    CHECK(until([&] { return !c.current()["dirty"].toBool() && !c.syncing(); }, 12000));
    c.openReport("", rejected);
    CHECK(c.current()["dirty"].toBool());
    CHECK(c.current()["payload"].toMap()["answers"].toMap()["FIN.03"].toMap()["value"] == "REJECT_413");
    qInfo() << "PASS: rejected report preserved; next queued report synchronized";
    qInfo()
        << "PASS: ownership transfer/offline revocation; revision conflict; protected server preview; independent photo/report copy; backup guards; operation stages/context/progress; local/remote/closed deletion and stale-copy cleanup; deferred group photo batches; bulk/cancel/retry/pin/photos; nonblocking edit/attach/remove; latest local changes survive ACK; lost ACK recovery; automatic idle sync; lazy photos and cached reopen";
    return 0;
}
