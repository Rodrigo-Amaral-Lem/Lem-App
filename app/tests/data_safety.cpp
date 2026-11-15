// SPDX-License-Identifier: GPL-3.0-only
// Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

#include "Lem.h"
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QFile>
#include <QDir>
#include <QImage>
#include <QBuffer>
#include <QDebug>
#include <QJsonDocument>
#include <QFileInfo>

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            qCritical() << "FAIL" << __LINE__ << #condition;                                       \
            return false;                                                                          \
        }                                                                                          \
    } while (false)

struct LemReviewTest {
    static QByteArray read(const QString &path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return {};
        return file.readAll();
    }
    static bool write(const QString &path, const QByteArray &bytes) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
    }
    static bool wait(Lem &client) {
        QElapsedTimer timer;
        timer.start();
        while (client.busy() && timer.elapsed() < 5000)
            QCoreApplication::processEvents();
        return !client.busy();
    }
    static bool storage(const QString &root) {
        for (const auto &kind :
             {"syntax", "shape", "record-shape", "journal", "journal-shape", "unreadable"}) {
            Lem client(root + '/' + kind);
            client.demo();
            client.answer("MAN.01", "Unsynced field observation", "Keep this work");
            QImage photo(40, 30, QImage::Format_RGB32);
            photo.fill(Qt::cyan);
            const auto source = root + "/source.png";
            CHECK(photo.save(source));
            client.attach("EST.01", QUrl::fromLocalFile(source));
            CHECK(client.persist());
            const auto report = client.m_current;
            const auto evidence = report["photos"].toArray()[0].toObject()["path"].toString();
            const auto cache = client.cacheDir() + "/cache.json";
            const auto goodCache = read(cache);
            const auto journal = client.editPath(report["id"].toString());
            const auto damaged = QByteArray("{ interrupted write");
            QByteArray damagedJournal = damaged;
            if (QString(kind) == "journal-shape") {
                auto partial = report;
                partial["payload"] = QJsonArray{};
                damagedJournal = QJsonDocument(partial).toJson();
                CHECK(write(journal, damagedJournal));
            } else if (QString(kind) == "journal")
                CHECK(write(journal, damaged));
            else if (QString(kind) == "record-shape") {
                auto checkpoint = QJsonDocument::fromJson(goodCache).object();
                auto reports = checkpoint["reports"].toObject();
                auto partial = report;
                partial["payload"] = QJsonArray{};
                reports[report["id"].toString()] = partial;
                checkpoint["reports"] = reports;
                CHECK(write(cache, QJsonDocument(checkpoint).toJson()));
            } else if (QString(kind) == "shape")
                CHECK(write(cache, "{\"reports\":\"invalid\"}"));
            else if (QString(kind) == "unreadable") {
                CHECK(QFile::rename(cache, cache + ".recovery"));
                CHECK(QDir().mkdir(cache));
            } else
                CHECK(write(cache, damaged));
            const auto original = QString(kind) == "unreadable" ? goodCache : read(cache);
            client.loadUser();
            CHECK(client.localSaveError());
            CHECK(!client.persist());
            CHECK(!client.saveEdits(report));
            client.queueEdits(report);
            CHECK(!client.flushEdits());
            client.m_token = "test-session-do-not-send";
            client.startSync(true);
            CHECK(!client.m_syncing && client.m_transfers.isEmpty());
            client.m_token.clear();
            client.clearLocalCopies();
            client.cleanPhotoCache();
            CHECK(QFileInfo::exists(QUrl(evidence).toLocalFile()));
            if (QString(kind).startsWith("journal"))
                CHECK(read(journal) == damagedJournal);
            if (QString(kind) == "unreadable") {
                CHECK(QFileInfo(cache).isDir());
                CHECK(read(cache + ".recovery") == goodCache);
            } else
                CHECK(read(cache) == original);
            // A repaired checkpoint/journal can be reloaded; recovery is not a permanent lock.
            if (QString(kind) == "unreadable")
                CHECK(QDir().rmdir(cache));
            CHECK(write(cache, goodCache));
            if (QString(kind).startsWith("journal"))
                CHECK(QFile::remove(journal));
            client.loadUser();
            CHECK(!client.localSaveError());
            CHECK(client.m_reports[report["id"].toString()].toObject()["payload"] ==
                  report["payload"]);
            CHECK(client.persist());
        }
        return true;
    }
    static bool signature(const QString &root) {
        Lem client(root);
        client.demo();
        client.answer("FIN.02", "Original technician", "");
        QImage image(80, 30, QImage::Format_RGB32);
        image.fill(Qt::black);
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        CHECK(image.save(&buffer, "PNG"));
        CHECK(client.saveSignature("FIN.02",
                                   "data:image/png;base64," + QString::fromLatin1(png.toBase64())));
        const auto originalId = client.m_current["id"].toString();
        const auto original = client.m_current;
        const auto signatureId = original["payload"]
                                     .toObject()["signatures"]
                                     .toObject()["FIN.02"]
                                     .toObject()["photo_id"];
        client.copyLocalReport();
        CHECK(wait(client));
        CHECK(client.m_current["id"] != originalId);
        const auto copied = client.m_current;
        const auto signature =
            copied["payload"].toObject()["signatures"].toObject()["FIN.02"].toObject();
        const auto photo = copied["photos"].toArray()[0].toObject();
        CHECK(signature["photo_id"] == photo["id"]);
        CHECK(signature["photo_id"] != signatureId);
        CHECK(signature["signed_by"] == "Original technician");
        CHECK(client.m_reports[originalId].toObject()["payload"] == original["payload"]);
        CHECK(read(QUrl(photo["path"].toString()).toLocalFile()) == png);
        Lem reloaded(root);
        reloaded.demo();
        reloaded.openReport("demo-charger", copied["id"].toString());
        CHECK(reloaded.m_current["payload"] == copied["payload"]);
        // Invalid source references must not produce a misleading 'complete' backup.
        client.m_current = original;
        auto payload = original["payload"].toObject();
        auto signatures = payload["signatures"].toObject();
        auto broken = signatures["FIN.02"].toObject();
        broken["photo_id"] = "missing-photo";
        signatures["FIN.02"] = broken;
        payload["signatures"] = signatures;
        client.m_current["payload"] = payload;
        client.m_reports[originalId] = client.m_current;
        const auto count = client.m_reports.size();
        client.copyLocalReport();
        CHECK(wait(client));
        CHECK(client.m_reports.size() == count);
        CHECK(client.m_current["id"] == originalId);
        return true;
    }
    static bool cleanup(const QString &root) {
        for (const auto &kind : {"conflict", "unconfirmed", "pending-deletion", "confirmed"}) {
            Lem client(root + '/' + kind);
            client.demo();
            QImage image(30, 30, QImage::Format_RGB32);
            image.fill(Qt::cyan);
            const auto path = root + "/cleanup.png";
            CHECK(image.save(path));
            client.attach("EST.01", QUrl::fromLocalFile(path));
            CHECK(client.flushEdits());
            auto report = client.m_current;
            report["status"] = "closed";
            report["dirty"] = false;
            report["revision"] = QString(kind) == "unconfirmed" ? 0 : 3;
            report["last_access"] = 1;
            auto photos = report["photos"].toArray();
            auto photo = photos[0].toObject();
            photo["uploaded"] = true;
            photos[0] = photo;
            report["photos"] = photos;
            if (QString(kind) == "conflict")
                report["_conflict"] = true;
            if (QString(kind) == "pending-deletion")
                report["deleted_photos"] = QJsonArray{"old-evidence"};
            client.m_reports[report["id"].toString()] = report;
            client.m_current = {};
            CHECK(client.persist());
            client.cleanPhotoCache();
            CHECK(QFileInfo::exists(QUrl(photo["path"].toString()).toLocalFile()) ==
                  (QString(kind) != "confirmed"));
        }
        return true;
    }
};

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    app.setApplicationVersion("0.2.0");
    QTemporaryDir root;
    const QString mode = argc > 1 ? argv[1] : "";
    const bool ok = mode == "storage"     ? LemReviewTest::storage(root.path())
                    : mode == "signature" ? LemReviewTest::signature(root.path())
                                          : LemReviewTest::cleanup(root.path());
    if (ok)
        qInfo() << "PASS: data safety" << mode;
    return ok ? 0 : 1;
}
