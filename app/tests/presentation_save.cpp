// SPDX-License-Identifier: GPL-3.0-only
// Verify settings writes and acknowledgements without any production service.
#include "Lem.h"
#include <QGuiApplication>
#include <QTcpServer>
#include <QTcpSocket>
#include <QNetworkProxy>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QDir>
#include <QDebug>
#define CHECK(x) do { if (!(x)) { qCritical() << "FAILED" << #x << __LINE__; return 1; } } while (false)

struct LemReviewTest {
    static void prepare(Lem &c, const QString &server) {
        c.m_server = server; c.m_token = "test-only";
        c.m_user = QJsonObject{{"id", "presentation-admin"}, {"name", "Test"}, {"role", "admin"}};
        c.m_contract = "contract-test";
        c.m_contracts = QJsonArray{QJsonObject{{"id", c.m_contract}, {"name", "Synthetic contract"},
            {"presentation", QJsonObject{{"issuer", "Lem"}, {"revision", 0}, {"client_logos", QJsonArray{}}}}}};
        QDir().mkpath(c.cacheDir()); c.m_autoSync.stop();
    }
    static void individualDownload(Lem &c, bool held) {
        if (held) c.m_downloads.insert("another-report-photo"); else c.m_downloads.clear();
    }
    static void reload(Lem &c) { c.loadUser(); }
    static void role(Lem &c, QString role) { c.m_user["role"] = role; }
};
static bool settled(Lem &client) {
    QElapsedTimer timer; timer.start();
    while ((client.busy() || client.refreshing()) && timer.elapsed() < 10000) {
        QEventLoop loop; QTimer::singleShot(5, &loop, &QEventLoop::quit); loop.exec();
    }
    return !client.busy() && !client.refreshing();
}
static QJsonObject style(const Lem &client) {
    return QJsonObject::fromVariantMap(client.contracts()[0].toMap()["presentation"].toMap());
}
static QString state(const Lem &client) {
    return client.property("reportPresentationSave").toMap()["state"].toString();
}

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv); app.setApplicationVersion("0.4.1");
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
    QTemporaryDir root; QTcpServer server; CHECK(server.listen(QHostAddress::LocalHost));
    const auto endpoint = "http://127.0.0.1:" + QString::number(server.serverPort());
    Lem client(root.path()); LemReviewTest::prepare(client, endpoint);
    QJsonObject stored = style(client);
    QString mode = "good"; int writes = 0, reads = 0;
    QObject::connect(&server, &QTcpServer::newConnection, &app, [&] {
        auto socket = server.nextPendingConnection(); auto buffer = QSharedPointer<QByteArray>::create();
        QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket, buffer] {
            *buffer += socket->readAll();
            const auto end = buffer->indexOf("\r\n\r\n");
            if (end < 0 || socket->property("sent").toBool()) return;
            int length = 0;
            for (auto line : buffer->left(end).split('\n'))
                if (line.toLower().startsWith("content-length:")) length = line.mid(15).trimmed().toInt();
            if (buffer->size() < end + 4 + length) return;
            socket->setProperty("sent", true);
            const auto parts = buffer->split(' '); const auto method = parts[0], path = parts[1];
            QByteArray payload = "{}", status = "200 OK";
            if (path == "/api/contracts/contract-test/presentation" && method == "PUT") {
                ++writes;
                const auto body = QJsonDocument::fromJson(buffer->mid(end + 4, length)).object();
                if (mode == "denied") status = "403 Forbidden";
                else if (mode == "failed-write") status = "503 Unavailable";
                else if (mode == "conflict") status = "409 Conflict";
                else {
                    stored = QJsonObject{{"issuer", body["issuer"]}, {"revision", stored["revision"].toInt() + 1},
                                         {"client_logos", QJsonArray{}}};
                    payload = mode == "lost-ack" ? "{}" : QJsonDocument(stored).toJson(QJsonDocument::Compact);
                }
            } else if (path == "/api/contracts/contract-test/presentation" && method == "GET") {
                ++reads;
                if (mode == "failed-read") status = "503 Unavailable";
                else if (mode == "malformed-read") payload = "{}";
                else if (mode == "wrong-read") payload = "{\"issuer\":\"Lem\",\"revision\":0,\"client_logos\":[]}";
                else payload = QJsonDocument(stored).toJson(QJsonDocument::Compact);
            } else {
                // Catalogue refresh must not erase the durable save result.
                status = "503 Unavailable";
            }
            socket->write("HTTP/1.1 " + status + "\r\nContent-Type: application/json\r\nContent-Length: " +
                          QByteArray::number(payload.size()) + "\r\n\r\n" + payload);
            socket->disconnectFromHost();
        });
    });
    // The previous global switching guard silently discarded this enabled button's click.
    LemReviewTest::individualDownload(client, true);
    client.saveReportPresentation("CSMTEC", "", "", 0); CHECK(settled(client));
    CHECK(writes == 1 && reads == 1);
    CHECK(state(client) == "saved" && style(client)["issuer"] == "CSMTEC");
    LemReviewTest::individualDownload(client, false);
    {
        Lem reopened(root.path()); LemReviewTest::prepare(reopened, endpoint); LemReviewTest::reload(reopened);
        CHECK(style(reopened)["issuer"] == "CSMTEC");
    }
    mode = "lost-ack";
    client.saveReportPresentation("Lem", "", "", stored["revision"].toInt()); CHECK(settled(client));
    CHECK(state(client) == "saved" && style(client)["issuer"] == "Lem");
    for (const auto &failure : {"malformed-read", "wrong-read", "failed-read", "failed-write", "denied", "conflict"}) {
        mode = failure;
        const auto before = style(client);
        client.saveReportPresentation("CSMTEC", "", "", before["revision"].toInt()); CHECK(settled(client));
        // A failed PUT can recover only if a fresh read proves the intended choice.
        const bool recovered = (mode == "failed-write" || mode == "conflict") && stored["issuer"] == "CSMTEC";
        CHECK(state(client) == (recovered ? "saved" : "error"));
        if (!recovered) CHECK(style(client) == before);
    }
    mode = "good";
    client.saveReportPresentation("Lem", "", "", stored["revision"].toInt()); CHECK(settled(client));
    CHECK(state(client) == "saved");
    // If another writer won, a conflict must not masquerade as our saved selection.
    mode = "conflict";
    client.saveReportPresentation("CSMTEC", "", "", stored["revision"].toInt()); CHECK(settled(client));
    CHECK(state(client) == "error" && style(client)["issuer"] == "Lem");
    const int authorizedWrites = writes;
    LemReviewTest::role(client, "tech");
    client.saveReportPresentation("CSMTEC", "", "", stored["revision"].toInt());
    CHECK(writes == authorizedWrites && state(client) == "error");
    qInfo() << "PASS: saves during individual downloads; read-back verification; restart; lost ACK; malformed/stale reads; HTTP failures; conflicts; administrator gate";
}
