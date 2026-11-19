// SPDX-License-Identifier: GPL-3.0-only
// Actual controller transfers against a loopback server that interrupts responses.
#include "Lem.h"
#include "NetworkSecurity.h"
#include "ReportBundle.h"
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QNetworkProxy>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QFile>
#include <QDir>
#include <QDebug>
#ifdef Q_OS_WIN
#include "WindowsBackground.h"
#include <QProcess>
#endif
#define CHECK(x) do { if (!(x)) { qCritical() << "FAILED" << #x << __LINE__; return 1; } } while(false)
struct LemReviewTest {
    static void session(Lem &c, QString server) { c.m_server = server; c.m_token = "test-only"; }
    static void download(Lem &c, QString path, QString destination, QString digest,
                         std::function<void(QSharedPointer<NetworkSecurity::FileBody>)> done, std::function<void(int,QString)> failed) {
        c.requestPhoto(path, destination, 20*1024*1024, done, failed, digest);
    }
    static void report(Lem &c, QString id, const QJsonArray &photos) {
        c.m_user = QJsonObject{{"id","download-test"},{"name","Test"},{"role","tech"}};
        c.m_current = QJsonObject{{"id",id},{"owner_id","download-test"},{"status","closed"},
            {"contract_id","test-contract"},
            {"dirty",false},{"revision",1},{"photos",photos},
            {"snapshot",QJsonObject{{"charger",QJsonObject{{"external_id","TEST"}}}}},
            {"template",QJsonObject{{"sections",QJsonArray{QJsonObject{{"items",QJsonArray{QJsonObject{{"id","EST.01"}}}}}}}}}};
        c.m_reports = QJsonObject{{id,c.m_current}};
        QDir().mkpath(c.cacheDir());
        c.m_autoSync.stop();
    }
};
static bool until(std::function<bool()> predicate) {
    QElapsedTimer timer; timer.start();
    while (!predicate() && timer.elapsed()<20000) {
        QEventLoop loop; QTimer::singleShot(10,&loop,&QEventLoop::quit); loop.exec();
    }
    return predicate();
}
int main(int argc, char **argv) {
#ifdef Q_OS_WIN
    QApplication app(argc, argv);
    if (app.arguments().contains("--restore-test-window")) {
        WindowsInstance second(app.arguments().last().toStdWString().c_str());
        return second.existing ? 0 : 1;
    }
#else
    QGuiApplication app(argc, argv);
#endif
    app.setApplicationVersion("0.3.0");
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
    QTemporaryDir temp; Lem client(temp.filePath("client"));
    const QByteArray original(6*1024*1024, 'E');
    const auto digest = QString::fromLatin1(QCryptographicHash::hash(original,QCryptographicHash::Sha256).toHex());
    QByteArray bundle; QBuffer bundleBuffer(&bundle); CHECK(bundleBuffer.open(QIODevice::WriteOnly));
    ReportBundle::Writer archive(bundleBuffer); CHECK(archive.add("original.bin",original)); CHECK(archive.finish()); bundleBuffer.close();
    QTcpServer server; CHECK(server.listen(QHostAddress::LocalHost));
    LemReviewTest::session(client, "http://127.0.0.1:" + QString::number(server.serverPort()));
    QMap<QString,int> requests; QMap<QString,qint64> bytes; bool resumed = false, validator = false, batchRecovered = false;
    int presentationFailure = 0;
    QObject::connect(&server, &QTcpServer::newConnection, &app, [&] {
        auto socket=server.nextPendingConnection(); auto buffer=QSharedPointer<QByteArray>::create();
        QObject::connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
        QObject::connect(socket,&QTcpSocket::readyRead,socket,[&,socket,buffer] {
            *buffer += socket->readAll(); if (!buffer->contains("\r\n\r\n") || socket->property("sent").toBool()) return;
            socket->setProperty("sent",true);
            const auto name = QString::fromLatin1(buffer->split(' ')[1]); const auto n=++requests[name];
            QByteArray response, payload; const auto offset=262144;
            if (name == "/api/contracts" || name == "/api/stations") {
                payload = name == "/api/contracts"
                    ? R"([{"id":"test-contract","name":"Updated contract"}])"
                    : R"([{"id":"station","external_id":"PCVE-01","updated":"now","details":{}}])";
                response = "HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(payload.size())
                         + "\r\n\r\n" + payload;
            } else if (name=="/api/contracts/test-contract/presentation") {
                payload = presentationFailure ? "{}" : "{\"issuer\":\"CSMTEC\",\"revision\":1,\"client_logos\":[]}";
                const QByteArray status = presentationFailure == 1 ? "404 Not Found" :
                                          presentationFailure == 2 ? "403 Forbidden" : "200 OK";
                response="HTTP/1.1 "+status+"\r\nContent-Length: "+QByteArray::number(payload.size())+"\r\n\r\n"+payload;
            } else if (name.startsWith("/api/reports/batch/photos/")) {
                if (name.endsWith("/photo-0") && !batchRecovered) {
                    payload="{\"detail\":\"missing original for test\"}";
                    response="HTTP/1.1 404 Not Found\r\nContent-Length: "+QByteArray::number(payload.size())+"\r\n\r\n"+payload;
                }
                else { payload=original; response="HTTP/1.1 200 OK\r\nContent-Length: "+QByteArray::number(payload.size())+"\r\n\r\n"+payload; }
            } else if (name=="/api/reports/export-test/bundle") {
                payload=n==1?bundle.left(offset):bundle;
                response="HTTP/1.1 200 OK\r\nContent-Length: "+QByteArray::number(bundle.size())+"\r\n\r\n"+payload;
            } else if (name=="/denied") response="HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n";
            else if (name=="/persistent" || (name=="/busy" && n==1)) response="HTTP/1.1 503 Unavailable\r\nContent-Length: 0\r\n\r\n";
            else if (n==1 && name!="/busy") {
                payload=original.left(offset);
                response="HTTP/1.1 200 OK\r\nContent-Length: "+QByteArray::number(original.size())+"\r\n\r\n"+payload;
            } else if (name=="/legacy" || name=="/busy") {
                payload=original;
                response="HTTP/1.1 200 OK\r\nContent-Length: "+QByteArray::number(payload.size())+"\r\n\r\n"+payload;
            } else {
                resumed=buffer->toLower().contains("range: bytes=262144-");
                validator=buffer->contains("If-Range: \""+digest.toUtf8()+"\"");
                payload=original.mid(offset);
                response="HTTP/1.1 206 Partial Content\r\nContent-Range: bytes "+QByteArray::number(name=="/bad-range"?offset+1:offset)+"-"+
                    QByteArray::number(original.size()-1)+"/"+QByteArray::number(original.size())+"\r\nContent-Length: "+QByteArray::number(payload.size())+"\r\n\r\n"+payload;
            }
            bytes[name]+=payload.size(); socket->write(response); socket->disconnectFromHost();
        });
    });
#ifdef Q_OS_WIN
    if (app.arguments().contains("--windows-background")) {
        QWindow window;
        const auto name = QStringLiteral("Local\\Lem-background-test-%1").arg(app.applicationPid());
        WindowsInstance instance(name.toStdWString().c_str());
        CHECK(instance.event && !instance.existing && QSystemTrayIcon::isSystemTrayAvailable());
        WindowsBackground background(&window, client, instance.event);
        window.show();
        batchRecovered = true;
        LemReviewTest::report(client, "batch", {QJsonObject{{"id","photo-0"},{"mime","image/png"},
            {"size",double(original.size())},{"uploaded",true},{"digest",digest}}});
        client.downloadReportPhotos();
        CHECK(client.downloadingPhotos());
        CHECK(!window.close() && !window.isVisible());
        CHECK(until([&] { return !client.downloadingPhotos() && client.stationCatalogueLoaded(); }));
        CHECK(!window.isVisible() && client.photoDownloadSummary()["missing"].toInt() == 0);
        CHECK(client.contracts().value(0).toMap()["name"] == "Updated contract");
        CHECK(QProcess::execute(app.applicationFilePath(), {"--restore-test-window", name}) == 0);
        CHECK(until([&] { return window.isVisible(); }));
        CHECK(!window.close());
        for (auto *widget : QApplication::allWidgets()) {
            if (auto *menu = qobject_cast<QMenu *>(widget))
                for (auto *action : menu->actions())
                    if (action->text() == "Sair")
                        QTimer::singleShot(0, action, &QAction::trigger);
        }
        QTimer::singleShot(3000, &app, [&] { app.exit(7); });
        CHECK(app.exec() == 0);
        qInfo() << "PASS: closed window, authenticated catalogue and evidence received, second launch restored window, tray exit";
        return 0;
    }
#endif
    for (const auto &name : {QString("/resume"),QString("/legacy"),QString("/bad-range"),QString("/denied"),QString("/busy"),QString("/persistent")}) {
        const auto destination=temp.filePath("result.bin");
        { QFile previous(destination); CHECK(previous.open(QIODevice::WriteOnly)); previous.write("KEEP"); }
        bool done=false, success=false; int code=0; QEventLoop loop;
        LemReviewTest::download(client,name,destination,digest,[&](auto body) {
            success=body->matches(digest) && body->commit(); done=true; loop.quit();
        },[&](int status,QString) { code=status; done=true; loop.quit(); });
        QTimer::singleShot(20000,&loop,&QEventLoop::quit); loop.exec(); CHECK(done);
        QFile result(destination); CHECK(result.open(QIODevice::ReadOnly));
        const bool expected = name=="/resume" || name=="/legacy" || name=="/busy";
        CHECK(success==expected); CHECK(result.readAll()==(expected?original:QByteArray("KEEP")));
        if (name=="/resume") { CHECK(resumed && validator); CHECK(requests[name]==2); CHECK(bytes[name]==original.size()); }
        if (name=="/legacy") CHECK(requests[name]==2);
        if (name=="/denied") CHECK(requests[name]==1 && code==403);
        if (name=="/persistent") CHECK(requests[name]==4 && code==503);
    }
    QJsonArray photos;
    for (int i=0;i<9;++i) photos.append(QJsonObject{{"id","photo-"+QString::number(i)}, {"checkpoint","EST.01"},
        {"mime","image/png"},{"digest",digest},{"size",double(original.size())},{"uploaded",true}});
    LemReviewTest::report(client,"batch",photos);
    client.downloadReportPhotos();
    CHECK(client.backgroundTransferActive() && !client.refreshBackgroundCatalogues());
    CHECK(until([&] { return !client.downloadingPhotos(); }));
    CHECK(client.photoDownloadSummary()["missing"].toInt()==1);
    CHECK(client.photoDownloadState().contains("missing original for test"));
    for (int i=0;i<9;++i) CHECK(requests["/api/reports/batch/photos/photo-"+QString::number(i)]==1);
    batchRecovered=true; client.downloadReportPhotos(); CHECK(until([&] { return !client.downloadingPhotos(); }));
    CHECK(client.photoDownloadSummary()["missing"].toInt()==0);
    for (int i=1;i<9;++i) CHECK(requests["/api/reports/batch/photos/photo-"+QString::number(i)]==1);
    CHECK(client.refreshBackgroundCatalogues());
    CHECK(until([&] { return !client.backgroundTransferActive(); }));
    CHECK(requests["/api/contracts"] == 1 && requests["/api/stations"] == 1);
    CHECK(client.contracts().value(0).toMap()["name"] == "Updated contract");
    CHECK(client.stations().value(0).toMap()["external_id"] == "PCVE-01");
    CHECK(client.photoDownloadSummary()["missing"].toInt() == 0);
    LemReviewTest::report(client,"export-test",photos);
    const auto zipPath=temp.filePath("downloaded.zip");
    client.exportReport("bundle",QUrl::fromLocalFile(zipPath)); CHECK(until([&] { return !client.busy(); }));
    if (!QFileInfo::exists(zipPath)) qCritical()<<client.message()<<requests["/api/reports/export-test/bundle"];
    QFile zip(zipPath); CHECK(zip.open(QIODevice::ReadOnly)); CHECK(zip.readAll()==bundle);
    CHECK(requests["/api/reports/export-test/bundle"]==2);
    zip.close();
    for (int failure : {1, 2, 3}) {
        presentationFailure = failure;
        client.exportReport("bundle",QUrl::fromLocalFile(zipPath)); CHECK(until([&] { return !client.busy(); }));
        CHECK(requests["/api/reports/export-test/bundle"]==2);
        CHECK(zip.open(QIODevice::ReadOnly)); CHECK(zip.readAll()==bundle); zip.close();
        CHECK(client.message().contains("Não foi possível confirmar"));
    }
    qInfo()<<"PASS: interrupted 6 MiB evidence resumes without repeating bytes; old-server restart; bad range rejected; bounded retries; access denial; failed item does not stop nine-file batch; only missing item repeats; interrupted server ZIP restarts and saves exact bytes";
    return 0;
}
