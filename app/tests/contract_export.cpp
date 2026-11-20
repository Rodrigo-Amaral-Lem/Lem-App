// SPDX-License-Identifier: GPL-3.0-only
// Loopback-only contract export/account actions; no production reports or accounts.
#include "Lem.h"
#include "ReportBundle.h"
#include <QGuiApplication>
#include <QDesktopServices>
#include <QTcpServer>
#include <QTcpSocket>
#include <QNetworkReply>
#include <QNetworkProxy>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QImage>
#include <QDirIterator>
#include <QPointer>
#include <QDebug>

#define CHECK(x) do { if (!(x)) { qCritical() << "FAILED" << #x << __LINE__; return 1; } } while (false)

static bool waitFor(const std::function<bool()> &done) {
    QElapsedTimer timer; timer.start();
    while (!done() && timer.elapsed() < 20000) {
        QEventLoop loop; QTimer::singleShot(5, &loop, &QEventLoop::quit); loop.exec();
    }
    return done();
}
static QByteArray bytes(const QString &path) {
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}
static QJsonObject report(QString id, QString imagePath) {
    const QJsonObject charger{{"external_id", id == "collision" ? "local" : id}, {"model", "Model"}, {"serial", "123"}};
    const QJsonObject item{{"id", "EST.01"}, {"label", "Condition"}, {"kind", "status"}};
    const QJsonObject section{{"id", "EST"}, {"title", "Equipment"}, {"items", QJsonArray{item}}};
    const QJsonObject photo{{"id", "photo"}, {"checkpoint", "EST.01"}, {"mime", "image/png"},
        {"path", QUrl::fromLocalFile(imagePath).toString()}, {"size", double(QFileInfo(imagePath).size())}};
    const QJsonObject answers{{"EST.01", QJsonObject{{"value", "OK"}}}};
    return {{"id", id}, {"charger_id", "charger"}, {"contract_id", "contract"}, {"owner_id", "technician"},
        {"status", "closed"}, {"revision", 2}, {"technician", "Synthetic technician"},
        {"finished", "2026-11-17T10:20:30Z"},
        {"snapshot", QJsonObject{{"charger", charger}, {"contract", QJsonObject{{"name", "Contract"}}}}},
        {"presentation", QJsonObject{{"issuer", "CSMTEC"}, {"revision", 2}, {"client_logos", QJsonArray{}}}},
        {"template", QJsonObject{{"sections", QJsonArray{section}}}},
        {"payload", QJsonObject{{"answers", answers}}}, {"photos", QJsonArray{photo}}};
}
class ExportViewer : public QObject {
    Q_OBJECT
public slots:
    void view(const QUrl &) {} // Keep synthetic PDFs inside the test process.
};
struct LemReviewTest {
    static void prepare(Lem &c, QString endpoint, QString photo) {
        c.m_server = endpoint; c.m_token = "test-only";
        c.m_user = {{"id", "technician"}, {"role", "tech"}, {"name", "Test"}};
        c.m_contract = "contract";
        c.m_contracts = QJsonArray{QJsonObject{{"id", "contract"}, {"name", "Synthetic contract"}}};
        c.m_current = {{"id", "open-draft"}, {"status", "draft"}, {"payload", QJsonObject{{"answers", QJsonObject{}}}}};
        auto stale = report("local", photo); stale["status"] = "draft"; stale["owner_id"] = "other";
        c.m_reports["local"] = stale;
        auto pending = report("pending", photo); pending["dirty"] = true; pending["revision"] = 0;
        c.m_reports["pending"] = pending;
        auto foreign = pending; foreign["id"] = "foreign"; foreign["owner_id"] = "other";
        c.m_reports["foreign"] = foreign;
        auto correction = pending; correction["id"] = "correction"; correction["correction"] = QJsonObject{{"state", "editing"}};
        c.m_reports["correction"] = correction;
        c.m_autoSync.stop(); QDir().mkpath(c.cacheDir());
    }
    static void openCached(Lem &c, QJsonObject report) {
        c.m_current = report;
        c.m_template = report["template"].toObject();
        c.m_reports[report["id"].toString()] = report;
    }
    static QJsonObject current(const Lem &c) { return c.m_current; }
    static QJsonObject cached(const Lem &c) { return c.m_reports; }
    static void acknowledgePending(Lem &c) {
        auto report = c.m_reports["pending"].toObject(); report["dirty"] = false; report["revision"] = 3;
        c.m_reports["pending"] = report;
    }
    static void unrelated(Lem &c, bool &done) {
        c.request("GET", "/unrelated", {}, [&done](QByteArray) { done = true; });
    }
    static void admin(Lem &c) {
        c.m_user["role"] = "admin"; c.m_token = "test-only";
        c.m_users = QJsonArray{QJsonObject{{"id", "target"}, {"role", "tech"}}, QJsonObject{{"id", "admin"}, {"role", "admin"}}};
    }
};

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv); app.setApplicationVersion("0.3.0");
    ExportViewer viewer;
    QDesktopServices::setUrlHandler("file", &viewer, "view");
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
    QTemporaryDir root; QTcpServer server; CHECK(server.listen(QHostAddress::LocalHost));
    const auto endpoint = "http://127.0.0.1:" + QString::number(server.serverPort());
    const auto imagePath = root.filePath("image.png");
    QImage image(24, 16, QImage::Format_RGB32); image.fill(Qt::cyan); CHECK(image.save(imagePath));
    QString pdfError; const auto pdf = ReportPdf::render(report("remote", imagePath), &pdfError); CHECK(pdfError.isEmpty());
    QByteArray zip; QBuffer zipBuffer(&zip); zipBuffer.open(QIODevice::WriteOnly);
    CHECK(ReportBundle::archive(report("remote", imagePath), zipBuffer, &pdfError));
    Lem client(root.filePath("cache")); LemReviewTest::prepare(client, endpoint, imagePath);
    const auto originalCurrent = LemReviewTest::current(client), originalCache = LemReviewTest::cached(client);
    QString mode = "good"; QMap<QString, int> requests; int resets = 0;
    QObject::connect(&server, &QTcpServer::newConnection, &app, [&] {
        auto socket = server.nextPendingConnection(); auto buffer = QSharedPointer<QByteArray>::create();
        QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket, buffer] {
            *buffer += socket->readAll(); const auto end = buffer->indexOf("\r\n\r\n");
            if (end < 0 || socket->property("sent").toBool()) return;
            int length = 0;
            for (auto line : buffer->left(end).split('\n'))
                if (line.toLower().startsWith("content-length:")) length = line.mid(15).trimmed().toInt();
            if (buffer->size() < end + 4 + length) return;
            socket->setProperty("sent", true);
            const auto parts = buffer->split(' '); const QString path = QString::fromLatin1(parts[1]);
            ++requests[path]; QByteArray status = "200 OK", payload = "{}"; bool truncate = false;
            if (path == "/api/contracts/contract/presentation") {
                if (mode == "trash") status = "423 Locked";
                else payload = QJsonDocument(report("local", imagePath)["presentation"].toObject()).toJson();
            } else if (path == "/api/contracts/contract/chargers?include_detached=true") {
                if (mode == "ack-during-list") LemReviewTest::acknowledgePending(client);
                QJsonArray rows;
                for (const auto id : {"local", "remote", "collision", "bad-file", "gone", "draft"})
                    rows.append(QJsonObject{{"id", id}, {"status", QString(id) == "draft" ? "draft" : "closed"}});
                const auto detached = rows.takeAt(2);
                payload = QJsonDocument(QJsonArray{
                    QJsonObject{{"id", "charger"}, {"reports", rows}},
                    QJsonObject{{"id", "detached"}, {"attached", false}, {"reports", QJsonArray{detached}}}
                }).toJson();
            } else if (path == "/unrelated") {
                QTimer::singleShot(500, socket, [socket] {
                    socket->write("HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\n{}"); socket->disconnectFromHost();
                });
                return;
            } else if (path == "/api/users/target/password") {
                ++resets;
                const auto input = QJsonDocument::fromJson(buffer->mid(end + 4, length)).object();
                if (mode == "denied" || input["password"] != "SyntheticPassword123!") status = "403 Forbidden";
                else payload = "{\"ok\":true,\"user_id\":\"target\"}";
            } else if (path.startsWith("/api/reports/")) {
                const auto segments = path.split('/'); const auto id = segments[3];
                if (id == "gone" || id == "superseded") {
                    status = "410 Gone";
                    if (id == "superseded") payload = R"({"detail":{"replacement_id":"corrected","message":"Substituído"}})";
                } else if (id == "restricted") status = "403 Forbidden";
                else if (segments.size() == 4) {
                    auto metadata = report(id, imagePath);
                    auto photos = metadata["photos"].toArray(); auto photo = photos[0].toObject();
                    photo.remove("path"); photos[0] = photo; metadata["photos"] = photos;
                    payload = QJsonDocument(metadata).toJson();
                } else {
                    if (mode == "cancel" && id == "remote") {
                        QTimer::singleShot(50, &client, &Lem::cancelContractExport);
                        return; // An in-progress response must abort without touching /unrelated.
                    }
                    if (mode == "expired") status = "401 Unauthorized";
                    else if (id == "bad-file") payload = "not-a-pdf-or-zip";
                    else payload = path.endsWith("/pdf") ? pdf : zip;
                    if (mode == "good" && id == "remote" && requests[path] == 1) truncate = true;
                }
            } else status = "404 Not Found";
            socket->write("HTTP/1.1 " + status + "\r\nContent-Length: " + QByteArray::number(payload.size()) + "\r\n\r\n" +
                          (truncate ? payload.left(payload.size() / 2) : payload));
            socket->disconnectFromHost();
        });
    });
    // A selected, fully cached report must still pass the online UUID check.
    for (const QString kind : {"pdf", "bundle"}) {
        const auto selected = root.filePath(kind == "pdf" ? "existing.pdf" : "existing.zip");
        for (const QString id : {"superseded", "gone", "restricted"}) {
            Lem observer(root.filePath("observer-" + kind + "-" + id));
            LemReviewTest::prepare(observer, endpoint, imagePath);
            LemReviewTest::openCached(observer, report(id, imagePath));
            QFile file(selected);
            CHECK(file.open(QIODevice::WriteOnly) && file.write("keep") == 4);
            file.close();
            observer.exportReport(kind, QUrl::fromLocalFile(selected));
            CHECK(waitFor([&] { return !observer.busy(); }));
            CHECK(requests["/api/reports/" + id] > 0 && bytes(selected) == "keep");
            if (id != "restricted") CHECK(!LemReviewTest::cached(observer).contains(id));
            if (id == "superseded") CHECK(LemReviewTest::current(observer)["id"] == "corrected");
        }
    }
    QString firstDirectory;
    for (const auto kind : {"pdf", "bundle"}) {
        client.exportContract("contract", kind, QUrl::fromLocalFile(root.path()));
        CHECK(!client.busy()); // Independent of report navigation/editing state.
        CHECK(waitFor([&] { return !client.contractExport()["active"].toBool(); }));
        const auto result = client.contractExport();
        if (result["saved"].toInt() != 4 || result["failed"].toInt() != 2 || result["total"].toInt() != 6)
            qCritical() << result << bytes(QDir(result["directory"].toString()).filePath("resumo-exportacao.json"));
        CHECK(result["saved"].toInt() == 4 && result["failed"].toInt() == 2 && result["total"].toInt() == 6);
        const auto dir = result["directory"].toString(); CHECK(dir != firstDirectory); firstDirectory = dir;
        const auto manifest = QJsonDocument::fromJson(bytes(QDir(dir).filePath("resumo-exportacao.json"))).object();
        CHECK(manifest["results"].toArray().size() == 6 && manifest["remaining"].toArray().isEmpty());
        int files = 0; QDirIterator it(dir, {QString(kind) == "pdf" ? "*.pdf" : "*.zip"}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const auto content = bytes(it.next());
            CHECK(content.startsWith(QString(kind) == "pdf" ? "%PDF-" : "PK")); ++files;
        }
        CHECK(files == 4);
        CHECK(QDir(dir).exists("report-collision"));
        CHECK(requests[QString("/api/reports/remote/") + kind] == 2); // Retry an interrupted download.
        CHECK(requests["/api/reports/draft"] == 0 && requests["/api/reports/foreign"] == 0);
        CHECK(LemReviewTest::current(client) == originalCurrent && LemReviewTest::cached(client) == originalCache);
    }
    // Independent devices exporting into the same parent directory must not share
    // a queue, overwrite deliveries or use a renderer from the other thread.
    mode = "parallel";
    {
        Lem first(root.filePath("first-device")), second(root.filePath("second-device"));
        LemReviewTest::prepare(first, endpoint, imagePath); LemReviewTest::prepare(second, endpoint, imagePath);
        first.exportContract("contract", "bundle", QUrl::fromLocalFile(root.path()));
        const auto directory = first.contractExport()["directory"];
        first.exportContract("contract", "pdf", QUrl::fromLocalFile(root.path()));
        CHECK(first.contractExport()["directory"] == directory); // A double click cannot replace a running job.
        second.exportContract("contract", "bundle", QUrl::fromLocalFile(root.path()));
        CHECK(waitFor([&] { return !first.contractExport()["active"].toBool() && !second.contractExport()["active"].toBool(); }));
        CHECK(first.contractExport()["saved"].toInt() == 4 && second.contractExport()["saved"].toInt() == 4);
        CHECK(first.contractExport()["directory"] != second.contractExport()["directory"]);
    }
    mode = "ack-during-list";
    client.exportContract("contract", "pdf", QUrl::fromLocalFile(root.path()));
    CHECK(waitFor([&] { return !client.contractExport()["active"].toBool(); }));
    CHECK(client.contractExport()["saved"].toInt() == 4 && client.contractExport()["total"].toInt() == 6);
    LemReviewTest::prepare(client, endpoint, imagePath);
    mode = "cancel"; bool unrelatedDone = false;
    LemReviewTest::unrelated(client, unrelatedDone);
    client.exportContract("contract", "pdf", QUrl::fromLocalFile(root.path()));
    CHECK(waitFor([&] { return !client.contractExport()["active"].toBool() && unrelatedDone; }));
    CHECK(client.contractExport()["saved"].toInt() == 1);
    CHECK(client.contractExport()["state"].toString() == "interrupted");
    CHECK(QFileInfo::exists(QDir(firstDirectory).filePath("resumo-exportacao.json")));
    mode = "trash";
    client.exportContract("contract", "pdf", QUrl::fromLocalFile(root.path()));
    CHECK(waitFor([&] { return !client.contractExport()["active"].toBool(); }));
    CHECK(client.contractExport()["saved"].toInt() == 0);
    mode = "expired";
    client.exportContract("contract", "bundle", QUrl::fromLocalFile(root.path()));
    CHECK(waitFor([&] { return !client.contractExport()["active"].toBool(); }));
    CHECK(client.contractExport()["state"].toString() == "interrupted");
    CHECK(client.contractExport()["processed"].toInt() < 6);
    CHECK(!client.resetTechnicianPassword("target", "SyntheticPassword123!"));
    LemReviewTest::admin(client); mode = "good";
    CHECK(!client.resetTechnicianPassword("admin", "SyntheticPassword123!"));
    CHECK(!client.resetTechnicianPassword("target", "short"));
    CHECK(client.resetTechnicianPassword("target", "SyntheticPassword123!"));
    CHECK(waitFor([&] { return !client.busy(); }));
    CHECK(client.passwordReset()["state"].toString() == "saved" && resets == 1);
    CHECK(!QJsonDocument(QJsonObject::fromVariantMap(client.passwordReset())).toJson().contains("SyntheticPassword123!"));
    mode = "denied";
    CHECK(client.resetTechnicianPassword("target", "SyntheticPassword123!"));
    CHECK(waitFor([&] { return !client.busy(); }));
    CHECK(client.passwordReset()["state"].toString() == "error");
    // A real refused connection still permits a complete, previously cached export.
    server.close();
    for (const QString kind : {"pdf", "bundle"}) {
        Lem offline(root.filePath("disconnected-" + kind));
        LemReviewTest::prepare(offline, endpoint, imagePath);
        LemReviewTest::openCached(offline, report("local", imagePath));
        const auto target = root.filePath(kind == "pdf" ? "offline.pdf" : "offline.zip");
        offline.exportReport(kind, QUrl::fromLocalFile(target));
        CHECK(waitFor([&] { return !offline.busy(); }));
        CHECK(bytes(target).startsWith(kind == "pdf" ? "%PDF-" : "PK"));
    }
    QDesktopServices::unsetUrlHandler("file");
    qInfo() << "PASS: selected PDF and ZIP preserve existing files on correction, deletion or denied access and export locally during an outage";
    qInfo() << "PASS: contract PDF/ZIP local/server paths, closed-only, stale observer, pending closure, collisions, retry, partial failure, cancel isolation, trash/session guards and technician password actions";
}

#include "contract_export.moc"
