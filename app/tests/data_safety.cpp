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
#include <QSaveFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <QNetworkProxy>

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

    static bool checkpoint(const QString &root, bool correction, bool failCommit) {
        Lem client(root);
        client.demo();
        const QString charger = "demo-charger";
        const auto originalId = client.m_current["id"].toString();
        const auto originalStart = QStringLiteral("2026-11-16T09:30:00Z");
        const auto originalEnd = QStringLiteral("2026-11-16T11:00:00Z");
        const auto newStart = QStringLiteral("2026-11-17T09:30:00Z");
        const auto newEnd = QStringLiteral("2026-11-17T11:00:00Z");

        QImage photo(40, 30, QImage::Format_RGB32);
        photo.fill(Qt::cyan);
        const auto source = root + "/evidence.png";
        CHECK(photo.save(source));
        client.attach("SITE.02", QUrl::fromLocalFile(source));
        const auto photos = client.m_current["photos"].toArray();
        CHECK(photos.size() == 1);
        const auto evidence = QUrl(photos.first().toObject()["path"].toString()).toLocalFile();
        const auto evidenceBytes = read(evidence);
        CHECK(!evidenceBytes.isEmpty());

        // The correction fixture owns an editing copy and retains a separate closed original.
        auto original = client.m_current;
        if (correction) {
            auto payload = client.m_current["payload"].toObject();
            payload["started"] = originalStart;
            payload["finished"] = originalEnd;
            client.m_current["payload"] = payload;
            client.m_current["started"] = originalStart;
            client.m_current["finished"] = originalEnd;
            original = client.m_current;
            original["status"] = "closed";
            original["revision"] = 3;
            original["dirty"] = false;
            client.m_reports[originalId] = original;
            client.m_current["id"] = "correction-copy";
            client.m_current["correction"] = QJsonObject{{"state", "editing"},
                                                         {"editor_id", client.m_user["id"]},
                                                         {"source_id", originalId}};
        }
        const auto editedId = client.m_current["id"].toString();
        client.m_reports[editedId] = client.m_current;
        CHECK(client.persist());

        const auto cache = client.cacheDir() + "/cache.json";
        const auto cacheBytes = read(cache);
        CHECK(!cacheBytes.isEmpty());
        const auto journal = client.editPath(editedId);
        CHECK(!QFileInfo::exists(journal));
        client.answer("SITE.02", "NOK", "Keep this pending journal");
        CHECK(client.saving());
        const auto before = client.m_current;
        const auto reports = client.m_reports;
        const auto sections = client.m_template;
        const auto change = [&] {
            return correction ? client.setCorrectionTimes(newStart, newEnd) : client.openReport(charger);
        };

        QFile lockedCache(cache);
        if (failCommit) {
            // A Windows QFile shares reads/writes but denies deletion. QSaveFile can
            // write its temporary file, then fails at the atomic replacement.
            CHECK(lockedCache.open(QIODevice::ReadOnly));
            QSaveFile probe(cache);
            CHECK(probe.open(QIODevice::WriteOnly));
            CHECK(probe.write(cacheBytes) == cacheBytes.size());
            CHECK(!probe.commit());
        } else {
            CHECK(QFile::rename(cache, cache + ".backup"));
            CHECK(QDir().mkdir(cache));
            QSaveFile probe(cache);
            CHECK(!probe.open(QIODevice::WriteOnly));
        }

        CHECK(!change());
        CHECK(client.message().contains(correction ? "datas anteriores" : "rascunho não foi criado"));
        CHECK(client.m_current == before);
        CHECK(client.m_reports == reports);
        CHECK(client.m_template == sections);
        CHECK(!client.saving());
        CHECK(read(evidence) == evidenceBytes);
        CHECK(QJsonDocument::fromJson(read(journal)).object() == before);
        if (failCommit) {
            lockedCache.close();
        } else {
            CHECK(QDir().rmdir(cache));
            CHECK(QFile::rename(cache + ".backup", cache));
        }
        CHECK(read(cache) == cacheBytes);

        // Open the durable state with a fresh controller before retrying, so a
        // later successful checkpoint cannot mask a lost journal or stale date.
        Lem reloaded(root);
        reloaded.m_server = client.m_server;
        reloaded.m_user = client.m_user;
        reloaded.loadUser();
        CHECK(reloaded.m_reports == reports);
        CHECK(!reloaded.localSaveError());

        CHECK(change());
        CHECK(client.message().contains(correction ? "Datas da correção guardadas" : "Novo rascunho guardado"));
        CHECK(read(evidence) == evidenceBytes);
        reloaded.loadUser();
        const auto savedId = client.m_current["id"].toString();
        CHECK(reloaded.m_reports.contains(savedId));
        const auto saved = reloaded.m_reports.value(savedId).toObject();
        if (correction) {
            CHECK(!QFileInfo::exists(journal));
            CHECK(savedId == editedId);
            CHECK(saved["started"] == newStart);
            CHECK(saved["finished"] == newEnd);
            const auto payload = saved["payload"].toObject();
            CHECK(payload["started"] == newStart);
            CHECK(payload["finished"] == newEnd);
            CHECK(payload["answers"] == before["payload"].toObject()["answers"]);
            CHECK(saved["photos"] == photos);
            CHECK(saved["dirty"].toBool());
            CHECK(saved["_edit_seq"].toDouble() == before["_edit_seq"].toDouble() + 1);
            CHECK(reloaded.m_reports.value(originalId).toObject() == original);
        } else {
            CHECK(savedId != editedId);
            CHECK(saved["status"] == "draft");
            CHECK(saved["dirty"].toBool());
            CHECK(saved["charger_id"] == charger);
            CHECK(saved["payload"].toObject()["started"] == saved["started"]);
            auto previous = before;
            // A successful switch releases deferred photos in the old report.
            // This creates a newer journal after checkpointing the draft.
            CHECK(previous.take("_defer_photo_sync").toBool());
            previous["_edit_seq"] = before["_edit_seq"].toDouble() + 1;
            CHECK(reloaded.m_reports.value(editedId).toObject() == previous);
        }
        qInfo() << "PASS: checkpoint" << (correction ? "correction dates" : "new draft")
                << (failCommit ? "commit failure" : "open failure") << "rollback, retry and reload";
        return true;
    }

    static bool storage(const QString &root) {
        CHECK(checkpoint(root + "/draft-open", false, false));
        CHECK(checkpoint(root + "/correction-open", true, false));
#ifdef Q_OS_WIN
        CHECK(checkpoint(root + "/draft-commit", false, true));
        CHECK(checkpoint(root + "/correction-commit", true, true));
#endif
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
        const auto sentBeforeSigning = client.m_current;
        client.m_syncing = true;
        client.m_refreshing = true;
        CHECK(client.saveSignature("FIN.02",
                                   "data:image/png;base64," + QString::fromLatin1(png.toBase64())));
        CHECK(client.mergeSync(sentBeforeSigning, true));
        CHECK(client.m_current["dirty"].toBool());
        CHECK(client.m_current["payload"].toObject()["signatures"].toObject().contains("FIN.02"));
        client.m_syncing = false;
        client.m_refreshing = false;
        // A migrated correction can contain historical signatures whose keys
        // no longer match a current checkpoint. Copies still own new photo IDs.
        auto historicalPayload = client.m_current["payload"].toObject();
        historicalPayload["legacy_signatures"] = QJsonObject{{"PC99.01",
            historicalPayload["signatures"].toObject()["FIN.02"]}};
        client.m_current["payload"] = historicalPayload;
        client.m_reports[client.m_current["id"].toString()] = client.m_current;
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
        CHECK(copied["payload"].toObject()["legacy_signatures"].toObject()["PC99.01"].toObject()["photo_id"] == photo["id"]);
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
    static bool rotation(const QString &root) {
        Lem client(root); client.demo();
        QImage source(24, 12, QImage::Format_RGB32);
        source.fill(Qt::white);
        for (int y = 0; y < 6; ++y)
            for (int x = 0; x < 12; ++x) source.setPixelColor(x, y, Qt::red);
        const auto path = root + "/rotation-source.png";
        CHECK(source.save(path));
        client.attach("MAN.14", QUrl::fromLocalFile(path));
        auto report = client.m_current;
        auto photos = report["photos"].toArray();
        auto uploaded = photos[0].toObject(); uploaded["uploaded"] = true; uploaded["digest"] = "original-digest";
        photos[0] = uploaded; report["photos"] = photos;
        client.m_current = report; client.m_reports[report["id"].toString()] = report;
        for (int turn = 0; turn < 4; ++turn) {
            const auto previous = client.m_current["photos"].toArray()[0].toObject();
            client.rotatePhoto(previous["id"].toString()); CHECK(wait(client));
            const auto rotated = client.m_current["photos"].toArray()[0].toObject();
            CHECK(rotated["id"] != previous["id"] && !rotated["uploaded"].toBool());
            CHECK(!rotated.contains("digest"));
            CHECK(client.m_current["deleted_photos"].toArray().contains(previous["id"]));
            const QImage pixels(QUrl(rotated["path"].toString()).toLocalFile());
            CHECK(pixels.size() == (turn % 2 == 0 ? QSize(12, 24) : QSize(24, 12)));
            if (turn == 0) CHECK(pixels.pixelColor(10, 2) == QColor(Qt::red));
            if (turn == 3) CHECK(pixels == source);
        }
        CHECK(QImage(path) == source); // The user's imported file is never changed.
        // Saving the untouched preview, or completing four preview turns, is a
        // true no-op: retain the ID, upload state, journal and original bytes.
        const auto unchanged = client.m_current;
        const auto unchangedPhoto = unchanged["photos"].toArray()[0].toObject();
        CHECK(client.editPhoto(unchangedPhoto["id"].toString(), 0, 0, 0, 1, 1));
        CHECK(!client.busy() && client.m_current == unchanged);
        CHECK(!client.editPhoto(unchangedPhoto["id"].toString(), -1, 0, 0, 1, 1));
        CHECK(!client.editPhoto(unchangedPhoto["id"].toString(), 4, 0, 0, 1, 1));
        CHECK(client.m_current == unchanged);

        // Exercise every orientation with and without cropping. Distinct pixels
        // reveal a wrong rotation direction or crop coordinates in the old space.
        QImage grid(24, 12, QImage::Format_RGB32);
        for (int y = 0; y < grid.height(); ++y)
            for (int x = 0; x < grid.width(); ++x) grid.setPixelColor(x, y, QColor(x * 10, y * 20, 30));
        const auto gridPath = root + "/grid.png";
        CHECK(grid.save(gridPath));
        for (int turn = 0; turn < 4; ++turn) {
            for (bool crop : {false, true}) {
                const auto cache = root + QString("/edit-%1-%2").arg(turn).arg(crop);
                Lem editor(cache); editor.demo(); editor.attach("MAN.14", QUrl::fromLocalFile(gridPath));
                const auto before = editor.m_current;
                const auto photo = before["photos"].toArray()[0].toObject();
                const auto oldId = photo["id"].toString();
                CHECK(editor.editPhoto(oldId, turn, crop ? .25 : 0, crop ? .25 : 0,
                                       crop ? .5 : 1, crop ? .5 : 1));
                CHECK(wait(editor));
                const auto edited = editor.m_current["photos"].toArray()[0].toObject();
                const QImage pixels(QUrl(edited["path"].toString()).toLocalFile());
                const auto oriented = turn % 2 ? QSize(12, 24) : QSize(24, 12);
                CHECK(pixels.size() == (crop ? oriented / 2 : oriented));
                for (int y = 0; y < pixels.height(); ++y) {
                    for (int x = 0; x < pixels.width(); ++x) {
                        const int px = x + (crop ? oriented.width() / 4 : 0);
                        const int py = y + (crop ? oriented.height() / 4 : 0);
                        const QPoint original = turn == 0 ? QPoint(px, py)
                            : turn == 1 ? QPoint(py, 11 - px)
                            : turn == 2 ? QPoint(23 - px, 11 - py) : QPoint(23 - py, px);
                        CHECK(pixels.pixelColor(x, y) == grid.pixelColor(original));
                    }
                }
                if (turn || crop) {
                    CHECK(edited["id"] != oldId && editor.m_current["photos"].toArray().size() == 1);
                    CHECK(editor.m_current["deleted_photos"].toArray() == QJsonArray{oldId});
                    CHECK(editor.m_current["_edit_seq"].toDouble() == before["_edit_seq"].toDouble() + 1);
                    CHECK(!QFile::exists(QUrl(photo["path"].toString()).toLocalFile()));
                    Lem restored(cache); restored.demo(); restored.openReport("", before["id"].toString());
                    CHECK(restored.m_current["photos"] == editor.m_current["photos"]);
                } else {
                    CHECK(editor.m_current == before);
                }
            }
        }
        CHECK(QImage(gridPath) == grid);
        const auto finalPhotos = client.m_current["photos"];
        client.m_current["status"] = "closed";
        client.rotatePhoto(finalPhotos.toArray()[0].toObject()["id"].toString());
        CHECK(!client.busy() && client.m_current["photos"] == finalPhotos);
        client.m_current["status"] = "draft"; client.m_current["owner_id"] = "another-user";
        client.rotatePhoto(finalPhotos.toArray()[0].toObject()["id"].toString());
        CHECK(!client.busy() && client.m_current["photos"] == finalPhotos);
        return true;
    }
    static bool rejectedClosure(const QString &root) {
        Lem client(root); client.demo();
        client.answer("MAN.14", "600 MΩ", "Medição preservada após atualização");
        CHECK(client.flushEdits());
        auto report = client.m_current;
        report["status"] = "closed";
        // The temporary frozen-upload marker must not survive a rejected
        // closure; otherwise correctionAllowed() disables every editing action.
        auto schema = report["template"].toObject();
        schema["frozen_upload"] = true;
        report["template"] = schema;
        const auto payload = report["payload"];
        const auto photos = report["photos"];
        client.m_current = report; client.m_reports[report["id"].toString()] = report;
        client.m_syncReport = report["id"].toString(); client.m_syncing = true;
        client.syncFailed(422, "MAN.14: falta fotografia");
        CHECK(client.m_current["status"] == "draft" && client.m_current["dirty"].toBool());
        CHECK(!client.m_current["template"].toObject().contains("frozen_upload"));
        CHECK(client.correctionAllowed(client.m_current));
        CHECK(client.m_current["payload"] == payload && client.m_current["photos"] == photos);
        Lem restored(root); restored.demo(); restored.openReport("", report["id"].toString());
        CHECK(restored.m_current["status"] == "draft" && restored.m_current["payload"] == payload);
        return true;
    }
    static bool migrationSync(const QString &root) {
        // Equal answers do not imply equal forms: a rejected historical closure
        // may need a draft PUT solely to acknowledge the new template/marker.
        QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
        for (int scenario = 0; scenario < 4; ++scenario) {
            Lem client(root + QString::number(scenario)); client.demo();
            CHECK(client.flushEdits());
            QTcpServer server;
            CHECK(server.listen(QHostAddress::LocalHost));
            client.m_server = "http://127.0.0.1:" + QString::number(server.serverPort());
            client.m_token = "isolated-test-session";
            client.m_autoSync.stop(); client.m_manualSync = false;
            CHECK(QDir().mkpath(client.cacheDir()));
            auto local = client.m_current;
            local["revision"] = 1; local["dirty"] = true;
            local.remove("_defer_photo_sync");
            const auto id = local["id"].toString();
            client.m_current = local; client.m_reports = QJsonObject{{id, local}};
            auto remote = local;
            auto schema = remote["template"].toObject();
            schema["version"] = scenario < 2 ? 6 : 7;
            if (scenario == 1 || scenario == 3) schema["frozen_upload"] = true;
            remote["template"] = schema;
            int puts = 0;
            QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
                auto socket = server.nextPendingConnection();
                auto buffer = QSharedPointer<QByteArray>::create();
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket, buffer] {
                    *buffer += socket->readAll();
                    const auto end = buffer->indexOf("\r\n\r\n");
                    if (end < 0 || socket->property("sent").toBool()) return;
                    int length = 0;
                    for (const auto &header : buffer->left(end).split('\n'))
                        if (header.toLower().startsWith("content-length:"))
                            length = header.mid(15).trimmed().toInt();
                    if (buffer->size() < end + 4 + length) return;
                    socket->setProperty("sent", true);
                    if (buffer->startsWith("PUT ")) {
                        ++puts;
                        const auto body = QJsonDocument::fromJson(buffer->mid(end + 4, length)).object();
                        remote["template"] = local["template"];
                        remote["payload"] = body["payload"];
                        remote["revision"] = remote["revision"].toInt() + 1;
                    }
                    const auto body = QJsonDocument(remote).toJson(QJsonDocument::Compact);
                    socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                                  QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
            });
            client.m_syncing = true;
            client.m_syncIds = {id};
            client.syncNext(0);
            QElapsedTimer timer; timer.start();
            while (client.m_syncing && timer.elapsed() < 5000) QCoreApplication::processEvents();
            client.m_autoSync.stop();
            CHECK(!client.m_syncing);
            CHECK(!client.m_current["dirty"].toBool());
            CHECK(puts == (scenario == 2 ? 0 : 1));
            CHECK(remote["template"].toObject()["version"] == local["template"].toObject()["version"]);
            CHECK(!remote["template"].toObject()["frozen_upload"].toBool());
            CHECK(remote["payload"] == local["payload"]);
        }
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
    app.setApplicationVersion("0.3.0");
    QTemporaryDir root;
    const QString mode = argc > 1 ? argv[1] : "";
    const bool ok = mode == "storage"     ? LemReviewTest::storage(root.path())
                    : mode == "signature" ? LemReviewTest::signature(root.path())
                    : mode == "rotation" ? LemReviewTest::rotation(root.path())
                    : mode == "rejected-closure" ? LemReviewTest::rejectedClosure(root.path())
                    : mode == "migration-sync" ? LemReviewTest::migrationSync(root.path())
                                          : LemReviewTest::cleanup(root.path());
    if (ok)
        qInfo() << "PASS: data safety" << mode;
    return ok ? 0 : 1;
}
