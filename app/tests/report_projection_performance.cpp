// SPDX-License-Identifier: GPL-3.0-only
// Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

// Repeatable native microbenchmark; not a claim about physical Android frame times.
#include "Lem.h"
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QDebug>

struct LemReviewTest {
    static void seed(Lem &client) {
        QJsonArray photos;
        for (int i = 0; i < 200; ++i)
            photos.append(QJsonObject{{"id", QString("photo-%1").arg(i)},
                {"checkpoint", QString("PC01.%1").arg(i % 13 + 1, 2, 10, QChar('0'))},
                {"path", QString("file:///private/photos/photo-%1.jpg").arg(i)},
                {"uploaded", true}, {"size", 3000000}});
        client.m_current["photos"] = photos;
        emit client.changed();
    }
#ifdef LEM_PERFORMANCE_PROJECTIONS
    static void updatePhoto(Lem &client) {
        auto photos = client.m_current["photos"].toArray();
        auto photo = photos[0].toObject();
        photo["path"] = "file:///updated.jpg";
        photos[0] = photo;
        client.m_current["photos"] = photos;
        emit client.changed();
    }
#endif
};

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    app.setOrganizationName("Lem-Performance-Test");
    app.setApplicationName("Report projections");
    QTemporaryDir folder;
    Lem client(folder.path());
    client.demo();
    LemReviewTest::seed(client);
    client.current(); // Warm up before timing.
    constexpr int reads = 10000;
    qint64 checksum = 0;
    QElapsedTimer elapsed;
    elapsed.start();
    for (int i = 0; i < reads; ++i)
        checksum += client.current().value("id").toString().size();
    qInfo() << "report_reads=" << reads << "elapsed_ns=" << elapsed.nsecsElapsed()
            << "checksum=" << checksum;

    int reports = 0, photos = 0;
#ifdef LEM_PERFORMANCE_PROJECTIONS
    QObject::connect(&client, &Lem::currentChanged, [&] { ++reports; });
    QObject::connect(&client, &Lem::photosChanged, [&] { ++photos; });
    if (client.photosByCheckpoint()["EST.01"].toList().size() != 16) return 1;
#else
    QObject::connect(&client, &Lem::changed, [&] { ++reports; ++photos; });
#endif
    for (int i = 0; i < 1000; ++i) emit client.changed();
    qInfo() << "status_only_notifications=1000 report_notifications=" << reports
            << "photo_notifications=" << photos;
#ifdef LEM_PERFORMANCE_PROJECTIONS
    if (reports || photos) return 2;
    client.answer("EST.04", "Updated serial", "");
    if (reports != 1 || photos != 0) return 3;
    if (client.current()["payload"].toMap()["answers"].toMap()["EST.04"]
            .toMap()["value"] != "Updated serial") return 4;
    LemReviewTest::updatePhoto(client);
    if (reports != 2 || photos != 1) return 5;
    if (client.photosByCheckpoint()["EST.01"].toList().first()
            .toMap()["path"] != "file:///updated.jpg") return 6;
    qInfo() << "PASS: answer changes report only; photo/path changes update indexed photos";
#endif
}
