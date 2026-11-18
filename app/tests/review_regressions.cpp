// SPDX-License-Identifier: GPL-3.0-only
// Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

#include "Lem.h"
#include "Checklist.h"
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QImage>
#include <QUuid>
#include <QDebug>
#include <QTcpServer>
#include <QTcpSocket>
#include <QElapsedTimer>
#include <QNetworkProxy>
#include <QThread>
#include <memory>

struct LemReviewTest {
    static bool unopenedLegacyDraftMigratesBeforeSync(Lem &client) {
        client.demo();
        auto report = client.m_current;
        const auto id = report["id"].toString();
        auto schema = report["template"].toObject();
        schema["version"] = 6;
        auto sections = schema["sections"].toArray();
        for (int n = 0; n < sections.size(); ++n) {
            auto section = sections[n].toObject();
            if (section["id"] != "MAN") continue;
            QJsonArray items;
            for (auto entry : section["items"].toArray()) {
                auto item = entry.toObject();
                if (item["id"] == "MAN.21") continue;
                if (item["id"] == "MAN.22") item["id"] = "MAN.21";
                items.append(item);
            }
            section["items"] = items; sections[n] = section;
        }
        schema["sections"] = sections; report["template"] = schema;
        report["payload"] = QJsonObject{{"answers", QJsonObject{
            {"MAN.21", QJsonObject{{"value", ""}, {"notes", "Preservar antes de abrir"}}},
            {"MAN.11", QJsonObject{{"value", "0.03 A"}, {"notes", ""}}}}}};
        report["_inflight_payload"] = report.value("payload");
        report["dirty"] = true;
        client.m_reports[id] = report;
        client.m_current = {}; // The user has not opened this cached draft.
        QTcpServer server;
        if (!server.listen(QHostAddress::LocalHost)) return false;
        client.net.setProxy(QNetworkProxy::NoProxy);
        client.m_server = "http://127.0.0.1:" + QString::number(server.serverPort());
        // cacheDir is server-scoped; initialize the isolated HTTP scope too.
        if (!QDir().mkpath(client.cacheDir())) return false;
        client.m_token = "isolated-test";
        client.m_syncing = true;
        client.m_syncIds = {id};
        client.syncNext(0);
        // This checkpoint must be upgraded synchronously before the first network reply.
        const auto updated = client.m_reports[id].toObject();
        const auto answers = updated["payload"].toObject()["answers"].toObject();
        QFile cache(client.cacheDir() + "/cache.json");
        if (!cache.open(QIODevice::ReadOnly)) return false;
        const auto saved = QJsonDocument::fromJson(cache.readAll()).object()["reports"].toObject()[id].toObject();
        const bool passed = updated["template"].toObject()["version"] == 7 && client.m_current.value("id").toString().isEmpty()
            && answers["MAN.22"].toObject()["notes"] == "Preservar antes de abrir"
            && answers["MAN.11"].toObject()["value"] == "30 mA" && !answers.contains("MAN.21")
            && updated["_inflight_payload"] == updated["payload"]
            && saved["template"] == updated["template"] && saved["payload"] == updated["payload"];
        if (!passed) qCritical() << "Unopened migration" << updated["template"].toObject()["version"]
            << answers << "inflight match" << (updated["_inflight_payload"] == updated["payload"])
            << "saved match" << (saved["payload"] == updated["payload"]);
        return passed;
    }
    static bool contractTrashPreservesOfflineWork(Lem &client) {
        QTcpServer server;
        if (!server.listen(QHostAddress::LocalHost, 0)) { qCritical() << "Contract fixture failed at" << __LINE__; return false; }
        QStringList paths;
        QObject::connect(&server, &QTcpServer::newConnection, &server, [&]() {
            auto *socket = server.nextPendingConnection();
            auto received = std::make_shared<QByteArray>();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket, received]() {
                *received += socket->readAll();
                const auto split = received->indexOf("\r\n\r\n");
                if (split < 0) return;
                int length = 0;
                for (auto line : received->left(split).split('\n'))
                    if (line.toLower().startsWith("content-length:"))
                        length = line.mid(15).trimmed().toInt();
                if (received->size() < split + 4 + length) return;
                const auto path = QString::fromUtf8(received->split(' ')[1]);
                paths.append(path);
                QByteArray body = "{}";
                if (path == "/api/admin/deleted-contracts")
                    body = "[{\"id\":\"contract\",\"name\":\"Test\",\"purge_after\":\"2026-12-13T00:00:00+00:00\"}]";
                if (path == "/api/contracts") body = "[{\"id\":\"contract\",\"name\":\"Test\"}]";
                if (path.endsWith("/trash") && !received->contains("APAGAR")) body = "bad confirmation";
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                    QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        });
        auto wait = [&]() {
            QElapsedTimer elapsed; elapsed.start();
            while ((client.m_busy || client.m_refreshing) && elapsed.elapsed() < 3000) {
                QCoreApplication::processEvents(); QThread::msleep(1);
            }
            return !client.m_busy && !client.m_refreshing;
        };
        client.net.setProxy(QNetworkProxy::NoProxy);
        client.m_server = "http://127.0.0.1:" + QString::number(server.serverPort());
        client.m_token = "session";
        client.m_user = {{"id", "admin"}, {"role", "admin"}};
        client.m_contract = "contract";
        client.m_contracts = {QJsonObject{{"id", "contract"}}};
        const QJsonObject local{{"id", "offline"}, {"dirty", true},
            {"photos", QJsonArray{QJsonObject{{"path", "retained.png"}, {"uploaded", false}}}}};
        client.m_reports = {{"offline", local}};
        client.m_current = local;
        client.deleteContract("contract", "apagar");
        if (!paths.isEmpty() || client.m_busy) { qCritical() << "Contract fixture failed at" << __LINE__; return false; }
        client.m_user["role"] = "tech";
        client.deleteContract("contract", "APAGAR");
        if (!paths.isEmpty() || client.m_busy) { qCritical() << "Contract fixture failed at" << __LINE__; return false; }
        client.m_user["role"] = "admin";
        client.deleteContract("contract", "APAGAR");
        if (!wait() || !client.m_contract.isEmpty() || !client.m_chargers.isEmpty() ||
            !client.m_contracts.isEmpty() || client.m_reports["offline"].toObject() != local) { qCritical() << "Contract fixture failed at" << __LINE__; return false; }
        client.loadDeletedContracts();
        if (!wait() || client.m_deletedContracts.size() != 1) { qCritical() << "Contract fixture failed at" << __LINE__; return false; }
        client.restoreContract("contract");
        if (!wait() || !client.m_deletedContracts.isEmpty() || client.m_contracts.size() != 1) { qCritical() << "Contract fixture failed at" << __LINE__; return false; }
        client.m_syncReport = "offline";
        client.syncFailed(423, "Contrato apagado");
        client.m_autoSync.stop();
        // Catalogue refresh may add presentation/identifier metadata. Every
        // original field, including pending evidence, must remain unchanged.
        const auto retained = client.m_reports["offline"].toObject();
        for (auto it = local.constBegin(); it != local.constEnd(); ++it)
            if (retained.value(it.key()) != it.value()) return false;
        return !retained["_conflict"].toBool() &&
               paths.contains("/api/contracts/contract/trash") &&
               paths.contains("/api/contracts/contract/restore");
    }
    static bool replyAffectsOnlyItsSession(Lem &client, int code, bool replaceSession, bool evidence = false, bool oversized = false) {
        QTcpServer server;
        if (!server.listen(QHostAddress::LocalHost, 0)) return false;
        QObject::connect(&server, &QTcpServer::newConnection, &server, [&]() {
            auto *socket = server.nextPendingConnection();
            auto received = std::make_shared<QByteArray>();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, code, oversized, received]() {
                // TCP may split headers/body; reply only after the complete request.
                *received += socket->readAll();
                if (socket->property("responded").toBool()) return;
                const auto split = received->indexOf("\r\n\r\n");
                if (split < 0) return;
                int length = 0;
                for (auto line : received->left(split).split('\n'))
                    if (line.toLower().startsWith("content-length:")) length = line.mid(15).trimmed().toInt();
                if (received->size() < split + 4 + length) return;
                socket->setProperty("responded", true);
                const QByteArray body = oversized ? QByteArray(4096, 'X') : QByteArray("{\"detail\":{}}");
                socket->write("HTTP/1.1 " + QByteArray::number(code) +
                    " Rejected\r\nContent-Type: application/json\r\nContent-Length: " +
                    QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        });
        client.net.setProxy(QNetworkProxy::NoProxy);
        client.m_server = "http://127.0.0.1:" + QString::number(server.serverPort());
        client.m_token = "old-session";
        bool replied = false;
        if (evidence)
            client.requestPhoto("/api/reports/report/photos/photo", client.cacheDir() + "/unused.png",
                                1024, [](auto) {}, [&](int, QString) { replied = true; });
        else
            client.request("POST", "/api/logout", "{}", [](QByteArray) {}, "application/json",
                           [&](int, QString) { replied = true; });
        // Logout can finish locally before its server reply. A later login on
        // another server must not be cleared by the old request's rejection.
        if (replaceSession) {
            client.setServer(client.m_server + "/new-server");
            client.m_token = "new-session";
        }
        QElapsedTimer elapsed;
        elapsed.start();
        while (!replied && elapsed.elapsed() < 3000) {
            QCoreApplication::processEvents();
            QThread::msleep(1);
        }
        if (!replied || (replaceSession ? client.m_token != "new-session" : !client.m_token.isEmpty()))
            qCritical() << "Auth check: replied=" << replied << "elapsed=" << elapsed.elapsed()
                        << "token retained=" << !client.m_token.isEmpty();
        return replied && (replaceSession ? client.m_token == "new-session" : client.m_token.isEmpty());
    }

    static bool removalDuringUpload(Lem &client, const QUrl &source, const QString &scenario) {
        client.demo();
        QTcpServer server;
        if (!server.listen(QHostAddress::LocalHost)) return false;
        client.net.setProxy(QNetworkProxy::NoProxy);
        client.m_server = "http://127.0.0.1:" + QString::number(server.serverPort());
        const auto reportId = client.m_current["id"].toString();
        const bool queued = scenario == "queued";
        const bool remove = scenario != "retained";
        const bool fail = scenario == "failed";
        for (int i = 0; i < (queued ? 3 : 1); ++i)
            if (!client.attachToReport(reportId, "EST.01", source)) return false;
        const auto snapshot = client.m_current;
        const auto photos = snapshot["photos"].toArray();
        const auto removed = photos[queued ? 1 : 0].toObject();
        const auto removedId = removed["id"].toString();
        const auto cached = QUrl(removed["path"].toString()).toLocalFile();
        QStringList uploaded;
        QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
            auto socket = server.nextPendingConnection();
            auto received = std::make_shared<QByteArray>();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket, received] {
                *received += socket->readAll();
                if (socket->property("responded").toBool()) return;
                const auto split = received->indexOf("\r\n\r\n");
                if (split < 0) return;
                int length = 0;
                for (auto line : received->left(split).split('\n'))
                    if (line.toLower().startsWith("content-length:")) length = line.mid(15).trimmed().toInt();
                if (received->size() < split + 4 + length) return;
                socket->setProperty("responded", true);
                const auto id = QString::fromUtf8(received->split(' ')[1]).section('/', -1);
                uploaded.append(id);
                // The HTTP body has arrived, but the QFile remains open until the reply.
                if (remove && uploaded.size() == 1) client.removePhoto(removedId);
                const auto body = fail ? QByteArray("temporarily unavailable") :
                    QJsonDocument(QJsonObject{{"id", id}, {"digest", "acknowledged"}}).toJson(QJsonDocument::Compact);
                socket->write((fail ? QByteArray("HTTP/1.1 503 Unavailable") : QByteArray("HTTP/1.1 200 OK")) +
                    "\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        });
        client.m_token = "session";
        client.m_syncing = true;
        client.m_syncReport = reportId;
        client.m_syncIds = {reportId};
        client.syncPhotos(0, snapshot, 0);
        QElapsedTimer elapsed; elapsed.start();
        while (client.m_syncing && elapsed.elapsed() < 5000) {
            QCoreApplication::processEvents(); QThread::msleep(1);
        }
        client.m_autoSync.stop();
        const auto latest = client.m_reports[reportId].toObject();
        const bool ok = !client.m_syncing && client.m_syncConflicts.isEmpty() &&
            uploaded.size() == (queued ? 2 : 1) && (!queued || !uploaded.contains(removedId)) &&
            QFile::exists(source.toLocalFile()) && QFile::exists(cached) == !remove &&
            latest["photos"].toArray().size() == (queued ? 2 : remove ? 0 : 1) &&
            latest["deleted_photos"].toArray().contains(removedId) == remove;
        if (!ok) qCritical() << "Upload removal regression:" << scenario << "sent" << uploaded.size()
            << "cached" << QFile::exists(cached) << "state" << client.m_syncState;
        return ok;
    }

    static bool recoveredPhotoUsesTargetTemplate(Lem &client, const QUrl &image) {
        auto first = client.m_current;
        QString error;
        QFile schema(":/shared/checklist-v1.json");
        if (!schema.open(QIODevice::ReadOnly)) return false;
        const auto base = QJsonDocument::fromJson(schema.readAll()).object();
        first["template"] = Checklist::forCharger(base, {{"connectors", "2x CCS2"}}, &error);
        if (!error.isEmpty()) return false;
        const auto targetId = first["id"].toString();
        client.m_reports[targetId] = first;

        auto second = first;
        second["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
        second["template"] = Checklist::forCharger(base, {{"connectors", "3x T2"}}, &error);
        client.m_current = second;
        client.m_template = second["template"].toObject();
        client.m_reports[second["id"].toString()] = second;
        if (!client.attachToReport(targetId, "CARG.01.02", image)) return false;
        // The current report's third connector must not authorize one on the target.
        if (client.attachToReport(targetId, "CARG.01.03", image)) return false;
        return client.m_reports[targetId].toObject()["photos"].toArray().size() == 1
            && client.m_current["id"] == second["id"]
            && client.m_current["photos"].toArray().isEmpty();
    }
};

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    app.setOrganizationName("Lem-Review-Test");
    app.setApplicationName("Lem-Review-Test");
    QTemporaryDir root;
    QImage image(16, 16, QImage::Format_RGB32);
    image.fill(Qt::cyan);
    const auto path = root.filePath("capture.png");
    if (!image.save(path)) return 1;
    {
        Lem unopened(root.filePath("unopened"));
        if (!LemReviewTest::unopenedLegacyDraftMigratesBeforeSync(unopened)) return 6;
        qInfo() << "PASS: unopened legacy draft migrates durably before background sync";
    }
    Lem client(root.filePath("cache"));
    client.demo();
    if (!LemReviewTest::recoveredPhotoUsesTargetTemplate(client, QUrl::fromLocalFile(path)))
        return 2;
    qInfo() << "PASS: recovered photo validates against target report; current report untouched";
    for (const int responseMode : {0, 1, 2}) for (const auto code : {401, 426}) {
        for (const bool replaceSession : {false, true}) {
            Lem networkClient(root.filePath("network-" + QString::number(code) +
                                           (replaceSession ? "-obsolete" : "-current")));
            if (!LemReviewTest::replyAffectsOnlyItsSession(networkClient, code, replaceSession, responseMode != 0, responseMode == 2)) {
                qCritical() << "FAILED: auth reply" << code << "replaced session=" << replaceSession << "response mode=" << responseMode;
                return 3;
            }
        }
    }
    qInfo() << "PASS: obsolete 401/426 preserve a newer session; current 401/426 still revoke authentication";
    Lem contractClient(root.filePath("contract-client"));
    if (!LemReviewTest::contractTrashPreservesOfflineWork(contractClient)) return 4;
    qInfo() << "PASS: contract trash/restore requests, confirmation/role guards and HTTP 423 preserve offline work";
    bool uploadsOk = true;
    for (const auto &scenario : {QString("retained"), QString("active"), QString("failed"), QString("queued")}) {
        Lem uploadClient(root.filePath("upload-" + scenario));
        uploadsOk &= LemReviewTest::removalDuringUpload(uploadClient, QUrl::fromLocalFile(path), scenario);
    }
    if (!uploadsOk) return 5;
    qInfo() << "PASS: in-flight/queued removals preserve gallery originals and pending deletions, clean private files and continue remaining uploads";
}
