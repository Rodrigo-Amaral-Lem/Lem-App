// SPDX-License-Identifier: GPL-3.0-only
// Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

#include <QGuiApplication>
#include <QTemporaryDir>
#include <QImage>
#include <QEventLoop>
#include <QTimer>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QDebug>
#include "Lem.h"
static bool wait(Lem &lem, int timeout = 15000) {
    QEventLoop loop;
    QTimer timer, poll;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (!lem.busy() && !lem.syncing() && !lem.refreshing())
            loop.quit();
    });
    timer.start(timeout);
    poll.start(20);
    loop.exec();
    return !lem.busy() && !lem.syncing() && !lem.refreshing();
}
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            qCritical() << "Failed:" << #c;                                                        \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    app.setApplicationVersion("0.1.55");
    app.setOrganizationName("Lem-Test");
    app.setApplicationName("Lem Test");
    QTemporaryDir temp;
    Lem c(temp.path());
    c.demo();
    CHECK(c.sections().size() == 7);
    CHECK(c.current().value("revision").toInt() == 0);
    auto id = c.current().value("id").toString();
    c.answer("SITE.02", "NOK", "Pintura com desgaste");
    QImage image(320, 240, QImage::Format_RGB32);
    image.fill(Qt::darkCyan);
    QString photo = temp.path() + "/photo.png";
    CHECK(image.save(photo));
    c.attach("SITE.02", QUrl::fromLocalFile(photo));
    CHECK(c.current().value("photos").toList().size() == 1);
    {
        QEventLoop loop;
        QTimer poll;
        QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
            if (!c.saving())
                loop.quit();
        });
        poll.start(10);
        if (c.saving())
            loop.exec();
    }
    Lem restored(temp.path());
    restored.demo();
    restored.openReport("demo-charger", id);
    auto answers = restored.current().value("payload").toMap().value("answers").toMap();
    CHECK(answers.value("SITE.02").toMap().value("notes").toString() == "Pintura com desgaste");
    CHECK(restored.current().value("photos").toList().size() == 1);
    restored.finish();
    CHECK(restored.current().value("status").toString() == "draft");
    CHECK(restored.message().contains("falta"));
    auto removedId =
        restored.current().value("photos").toList().first().toMap().value("id").toString();
    auto privatePhoto =
        QUrl(restored.current().value("photos").toList().first().toMap().value("path").toString())
            .toLocalFile();
    auto cacheFile = QDir::cleanPath(QFileInfo(privatePhoto).absolutePath() + "/../cache.json");
    CHECK(QFile::rename(cacheFile, cacheFile + ".backup"));
    CHECK(QDir().mkdir(cacheFile));
    restored.removePhoto(removedId);
    CHECK(restored.current().value("photos").toList().size() == 1);
    CHECK(QFileInfo::exists(privatePhoto));
    CHECK(restored.current().value("deleted_photos").toList().isEmpty());
    CHECK(QDir().rmdir(cacheFile));
    CHECK(QFile::rename(cacheFile + ".backup", cacheFile));
    restored.removePhoto(removedId);
    CHECK(!QFileInfo::exists(privatePhoto));
    CHECK(restored.current().value("photos").toList().isEmpty());
    CHECK(restored.current().value("deleted_photos").toList().size() == 1);
    CHECK(restored.current().value("dirty").toBool());
    CHECK(QFileInfo::exists(photo));
    restored.removePhoto(removedId);
    CHECK(restored.current().value("deleted_photos").toList().size() == 1);
    Lem afterRemoval(temp.path());
    afterRemoval.demo();
    afterRemoval.openReport("demo-charger", id);
    CHECK(afterRemoval.current().value("photos").toList().isEmpty());
    CHECK(afterRemoval.current().value("deleted_photos").toList().first().toString() == removedId);
    for (auto sv : afterRemoval.sections())
        for (auto iv : sv.toMap().value("items").toList())
            afterRemoval.answer(iv.toMap().value("id").toString(), "NA", "Sem conector/veículo disponível");
    afterRemoval.answer("EST.01", "OK", "");
    afterRemoval.finish();
    CHECK(afterRemoval.current().value("status").toString() == "draft");
    CHECK(afterRemoval.message().contains("fotografia"));
    afterRemoval.answer("EST.01", "NA", "");
    afterRemoval.attach("MAN.12", QUrl::fromLocalFile(photo));
    auto closedPhoto =
        afterRemoval.current().value("photos").toList().first().toMap().value("id").toString();
    afterRemoval.finish();
    CHECK(afterRemoval.current().value("status").toString() == "closed");
    afterRemoval.removePhoto(closedPhoto);
    CHECK(afterRemoval.current().value("photos").toList().size() == 1);
    if (!app.arguments().contains("--network"))
        return 0;
    Lem net(temp.path() + "/network");
    net.setServer("http://127.0.0.1:18765");
    net.login("admin@lem.pt", "IntegrationPassword123!");
    CHECK(wait(net));
    CHECK(!net.user().isEmpty());
    net.createContract("Integração", 2026, 1, "TRE");
    CHECK(wait(net));
    auto contracts = net.contracts();
    CHECK(!contracts.isEmpty());
    auto cid = contracts.first().toMap().value("id").toString();
    net.selectContract(cid);
    CHECK(wait(net));
    net.analyseExcel(
        QUrl::fromLocalFile(app.arguments().value(app.arguments().indexOf("--xlsx") + 1)));
    CHECK(wait(net));
    CHECK(net.importInfo().contains("sheets"));
    net.previewExcel("Parque", 1,
                     {{"external_id", "Identificador"}, {"model", "Modelo"}, {"serial", "Série"}});
    CHECK(wait(net));
    CHECK(net.importPreview().value("count").toInt() == 2);
    net.commitExcel();
    CHECK(wait(net));
    CHECK(net.chargers().size() == 2);
    auto charger = net.chargers().first().toMap().value("id").toString();
    net.openReport(charger);
    auto rid = net.current().value("id").toString();
    for (auto sv : net.sections())
        for (auto iv : sv.toMap().value("items").toList()) {
            auto i = iv.toMap();
            net.answer(i.value("id").toString(), "NA", "Sem conector/veículo disponível");
        }
    net.answer("SITE.02", "NOK", "Desgaste");
    net.attach("SITE.02", QUrl::fromLocalFile(photo));
    net.sync();
    CHECK(wait(net));
    CHECK(!net.current().value("dirty").toBool());
    auto syncedPhoto =
        net.current().value("photos").toList().first().toMap().value("id").toString();
    CHECK(net.current().value("photos").toList().first().toMap().value("uploaded").toBool());
    net.removePhoto(syncedPhoto);
    CHECK(net.current().value("photos").toList().isEmpty());
    net.sync();
    CHECK(wait(net));
    CHECK(!net.current().value("dirty").toBool());
    CHECK(net.current().value("deleted_photos").toList().isEmpty());
    CHECK(net.current().value("revision").toInt() == 2);
    net.attach("SITE.02", QUrl::fromLocalFile(photo));
    net.finish();
    CHECK(wait(net));
    CHECK(net.current().value("status").toString() == "closed");
    CHECK(!net.current().value("dirty").toBool());
    CHECK(net.current().value("revision").toInt() == 4);
    net.exportReport("bundle", QUrl::fromLocalFile(temp.path() + "/report.zip"));
    CHECK(wait(net));
    CHECK(QFileInfo(temp.path() + "/report.zip").size() > 1000);
    // An independent client downloads the report and its original evidence.
    Lem other(temp.path() + "/other");
    other.setServer("http://127.0.0.1:18765");
    other.login("admin@lem.pt", "IntegrationPassword123!");
    CHECK(wait(other));
    other.openReport("", rid);
    CHECK(wait(other));
    CHECK(other.current().value("status").toString() == "closed");
    CHECK(other.current().value("photos").toList().size() == 1);
    auto photoId = other.current().value("photos").toList().first().toMap().value("id").toString();
    CHECK(other.current()
              .value("photos")
              .toList()
              .first()
              .toMap()
              .value("path")
              .toString()
              .isEmpty());
    other.fetchPhoto(photoId);
    {
        QEventLoop loop;
        QTimer timer, poll;
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
            if (!other.photoDownloading(photoId))
                loop.quit();
        });
        timer.start(15000);
        poll.start(20);
        loop.exec();
    }
    CHECK(!other.photoDownloading(photoId));
    auto downloaded =
        QUrl(other.current().value("photos").toList().first().toMap().value("path").toString())
            .toLocalFile();
    CHECK(QFileInfo::exists(downloaded));
    return 0;
}
