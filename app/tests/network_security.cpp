// SPDX-License-Identifier: GPL-3.0-only
// Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

// Local loopback only: prove POST bodies cannot be redirected to another origin.
#include "NetworkSecurity.h"
#include "SafeStorage.h"
#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTcpServer>
#include <QTcpSocket>
#include <QEventLoop>
#include <QTimer>
#include <QSharedPointer>
#include <QDebug>
#include <QNetworkProxy>
#include <QTemporaryDir>
#include <QFile>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    // These integration fixtures only contact the disposable loopback server.
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
    for (const auto &id : {QString("../outside"), QString("..\\outside"), QString("/absolute"), QString("%2fescape"), QString("valid\n"), QString()}) {
        if (SafeStorage::component(id) || !SafeStorage::photoPath("/cache", id, "image/jpeg").isEmpty()
            || !SafeStorage::component(SafeStorage::userDirectory(id)))
            return 1;
    }
    if (SafeStorage::photoPath("/cache", "valid-id", "image/png") != "/cache/photos/valid-id.png")
        return 2;
    QTcpServer origin, other;
    if (!origin.listen(QHostAddress::LocalHost) || !other.listen(QHostAddress::LocalHost))
        return 3;
    int foreignRequests = 0;
    auto serve = [&](QTcpServer &server, bool redirect) {
        QObject::connect(&server, &QTcpServer::newConnection, &app, [&server, redirect, &other, &foreignRequests]() {
            auto socket = server.nextPendingConnection();
            auto buffer = QSharedPointer<QByteArray>::create();
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, buffer, redirect, &server, &other, &foreignRequests]() {
                *buffer += socket->readAll();
                if (socket->property("responded").toBool()) return;
                const int head = buffer->indexOf("\r\n\r\n");
                if (head < 0) return;
                int length = 0;
                for (auto line : buffer->left(head).split('\n'))
                    if (line.toLower().startsWith("content-length:")) length = line.mid(15).trimmed().toInt();
                if (buffer->size() < head + 4 + length) return;
                socket->setProperty("responded", true);
                QByteArray response;
                const auto firstLine = buffer->left(buffer->indexOf("\r\n"));
                if (firstLine.contains(" /same-end "))
                    response = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nOK";
                else if (firstLine.contains(" /stream")) {
                    const bool failed = firstLine.contains("/stream-error");
                    const bool largeError = firstLine.contains("/stream-error-large");
                    const bool shortBody = firstLine.contains("/stream-short");
                    const QByteArray content = failed && !largeError ? QByteArray("session expired") : QByteArray(3 * 1024 * 1024, 'E');
                    response = (failed ? QByteArray("HTTP/1.1 401 Unauthorized") : QByteArray("HTTP/1.1 200 OK")) +
                        "\r\nContent-Length: " + QByteArray::number(content.size() + (shortBody ? 1024 : 0)) +
                        "\r\nConnection: close\r\n\r\n" + content;
                    if (largeError)
                        response = "HTTP/1.1 401 Unauthorized\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n" +
                            QByteArray::number(content.size(), 16) + "\r\n" + content + "\r\n0\r\n\r\n";
                }
                else if (firstLine.contains(" /large ")) {
                    const QByteArray content(256, 'X');
                    response = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n100\r\n" + content + "\r\n0\r\n\r\n";
                } else if (redirect) {
                    const bool same = firstLine.contains(" /same-login ");
                    response = "HTTP/1.1 307 Temporary Redirect\r\nLocation: http://127.0.0.1:" + QByteArray::number(same ? server.serverPort() : other.serverPort()) + (same ? "/same-end" : "/capture") + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
                } else {
                    ++foreignRequests;
                    response = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nOK";
                }
                socket->write(response);
                socket->disconnectFromHost();
            });
        });
    };
    serve(origin, true); serve(other, false);
    QNetworkAccessManager manager;
    QNetworkRequest request(QUrl("http://127.0.0.1:" + QString::number(origin.serverPort()) + "/login"));
    if (!app.arguments().contains("--unsafe-default")) NetworkSecurity::restrictRedirects(request);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    auto reply = manager.post(request, "{\"password\":\"TEST-ONLY\"}");
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    loop.exec();
    if (!reply->isFinished() || foreignRequests != 0 || reply->error() == QNetworkReply::NoError) {
        qCritical() << "FAIL: requests to foreign origin=" << foreignRequests << "error=" << reply->error();
        return 4;
    }
    QNetworkRequest same(QUrl("http://127.0.0.1:" + QString::number(origin.serverPort()) + "/same-login"));
    NetworkSecurity::restrictRedirects(same);
    same.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    auto sameReply = manager.post(same, "TEST-ONLY");
    QObject::connect(sameReply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    const auto sameBody = sameReply->readAll();
    if (sameReply->error() != QNetworkReply::NoError || sameBody != "OK" || foreignRequests != 0) {
        qCritical() << "Same-origin redirect:" << sameReply->errorString() << sameReply->url() << sameBody << foreignRequests;
        return 5;
    }
    auto large = manager.get(QNetworkRequest(QUrl("http://127.0.0.1:" + QString::number(origin.serverPort()) + "/large")));
    auto body = NetworkSecurity::boundReply(large, 64);
    QObject::connect(large, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    body->consume(large);
    if (!body->exceeded || body->data.size() > 64)
        return 6;
    QTemporaryDir temp;
    const auto destination = temp.filePath("evidence.bin");
    const auto expected = QString::fromLatin1(QCryptographicHash::hash(QByteArray(3 * 1024 * 1024, 'E'), QCryptographicHash::Sha256).toHex());
    auto readFile = [&] { QFile f(destination); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray{}; };
    for (const auto &scenario : {QString("good"), QString("digest"), QString("over"), QString("error"), QString("short"), QString("disk"), QString("error-large")}) {
        QFile original(destination);
        if (!original.open(QIODevice::WriteOnly) || original.write("original") != 8) return 7;
        original.close();
        auto streamed = manager.get(QNetworkRequest(QUrl("http://127.0.0.1:" + QString::number(origin.serverPort()) + "/stream-" + scenario)));
        auto staged = NetworkSecurity::streamReply(streamed,
            scenario == "disk" ? temp.filePath("missing/blocked.bin") : destination,
            scenario == "over" || scenario == "error-large" ? 65536 : 4 * 1024 * 1024);
        QObject::connect(streamed, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        staged->consume(streamed);
        if (readFile() != "original") return 8; // No partial evidence is ever published.
        if (scenario == "good") {
            if (streamed->error() != QNetworkReply::NoError || staged->received != 3 * 1024 * 1024 ||
                !staged->errorData.isEmpty() || !staged->matches(expected) || !staged->commit()) return 9;
            if (readFile() != QByteArray(3 * 1024 * 1024, 'E')) return 10;
        } else if (scenario == "digest") {
            if (staged->matches("wrong-digest")) return 11;
        } else if (scenario == "over") {
            if (!staged->exceeded || staged->commit()) return 12;
        } else if (scenario == "error") {
            if (streamed->error() == QNetworkReply::NoError || staged->received != 0 || staged->errorData != "session expired") return 13;
        } else if (scenario == "short") {
            if (streamed->error() == QNetworkReply::NoError) return 14;
        } else if (scenario == "error-large") {
            if (!staged->exceeded || staged->received != 0 || staged->errorData.size() > 65536 || staged->commit()) {
                qCritical() << "FAIL: streamed HTTP errors must also enforce the response limit";
                return 17;
            }
        } else if (!staged->writeFailed || staged->commit()) return 15;
        // Both network and report callbacks release staging without commit on failure.
        delete streamed;
        staged.clear();
        if (scenario != "good" && readFile() != "original") return 16;
    }
    qInfo() << "PASS: redirect restrictions and bounded replies; streaming evidence, atomic publish, digest rejection, limits, HTTP/disk errors and interrupted download.";
    return 0;
}
