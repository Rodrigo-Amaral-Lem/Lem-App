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

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
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
    if (sameReply->error() != QNetworkReply::NoError || sameReply->readAll() != "OK" || foreignRequests != 0)
        return 5;
    auto large = manager.get(QNetworkRequest(QUrl("http://127.0.0.1:" + QString::number(origin.serverPort()) + "/large")));
    auto body = NetworkSecurity::boundReply(large, 64);
    QObject::connect(large, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    body->consume(large);
    if (!body->exceeded || body->data.size() > 64)
        return 6;
    qInfo() << "PASS: foreign-origin POST blocked, same-origin redirect works, chunked body bounded, unsafe cache IDs rejected.";
    return 0;
}
