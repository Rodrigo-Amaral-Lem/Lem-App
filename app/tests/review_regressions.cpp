#include "Lem.h"
#include "Checklist.h"
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QFile>
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
    static bool contractTrashPreservesOfflineWork(Lem &client) {
        QTcpServer server;
        if (!server.listen(QHostAddress::LocalHost, 0)) return false;
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
        if (!paths.isEmpty() || client.m_busy) return false;
        client.m_user["role"] = "tech";
        client.deleteContract("contract", "APAGAR");
        if (!paths.isEmpty() || client.m_busy) return false;
        client.m_user["role"] = "admin";
        client.deleteContract("contract", "APAGAR");
        if (!wait() || !client.m_contract.isEmpty() || !client.m_chargers.isEmpty() ||
            !client.m_contracts.isEmpty() || client.m_reports["offline"].toObject() != local) return false;
        client.loadDeletedContracts();
        if (!wait() || client.m_deletedContracts.size() != 1) return false;
        client.restoreContract("contract");
        if (!wait() || !client.m_deletedContracts.isEmpty() || client.m_contracts.size() != 1) return false;
        client.m_syncReport = "offline";
        client.syncFailed(423, "Contrato apagado");
        client.m_autoSync.stop();
        return client.m_reports["offline"].toObject() == local &&
               !client.m_reports["offline"].toObject()["_conflict"].toBool() &&
               paths.contains("/api/contracts/contract/trash") &&
               paths.contains("/api/contracts/contract/restore");
    }
    static bool replyAffectsOnlyItsSession(Lem &client, int code, bool replaceSession) {
        QTcpServer server;
        if (!server.listen(QHostAddress::LocalHost, 0)) return false;
        QObject::connect(&server, &QTcpServer::newConnection, &server, [&]() {
            auto *socket = server.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, code]() {
                socket->readAll();
                const QByteArray body = "{\"detail\":{}}";
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
        return replied && (replaceSession ? client.m_token == "new-session" : client.m_token.isEmpty());
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
    Lem client(root.filePath("cache"));
    client.demo();
    if (!LemReviewTest::recoveredPhotoUsesTargetTemplate(client, QUrl::fromLocalFile(path)))
        return 2;
    qInfo() << "PASS: recovered photo validates against target report; current report untouched";
    for (const auto code : {401, 426}) {
        for (const bool replaceSession : {false, true}) {
            Lem networkClient(root.filePath("network-" + QString::number(code) +
                                           (replaceSession ? "-obsolete" : "-current")));
            if (!LemReviewTest::replyAffectsOnlyItsSession(networkClient, code, replaceSession)) {
                qCritical() << "FAILED: auth reply" << code << "replaced session=" << replaceSession;
                return 3;
            }
        }
    }
    qInfo() << "PASS: obsolete 401/426 preserve a newer session; current 401/426 still revoke authentication";
    Lem contractClient(root.filePath("contract-client"));
    if (!LemReviewTest::contractTrashPreservesOfflineWork(contractClient)) return 4;
    qInfo() << "PASS: contract trash/restore requests, confirmation/role guards and HTTP 423 preserve offline work";
}
