// SPDX-License-Identifier: GPL-3.0-only
// Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

#include "Lem.h"
#include "EvidenceMedia.h"
#include "SafeStorage.h"
#include "ReportPdf.h"
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QImage>
#include <QElapsedTimer>
#include <QThread>
#include <QFile>
#include <QDebug>
#include <limits>

struct LemReviewTest {
    static bool exercise(Lem &c, const QString &photo, const QString &video) {
        c.demo();
        if (!c.attachToReport(c.m_current["id"].toString(), "EST.01", QUrl::fromLocalFile(photo))) return false;
        auto original = c.m_current["photos"].toArray()[0].toObject();
        const auto oldId = original["id"].toString();
        c.cropPhoto(oldId, -0.1, 0, .5, .5);
        c.cropPhoto(oldId, std::numeric_limits<double>::quiet_NaN(), 0, .5, .5);
        if (c.m_busy || c.m_current["photos"].toArray()[0].toObject()["id"] != oldId) return false;
        c.cropPhoto(oldId, .25, .25, .5, .5);
        QElapsedTimer timer; timer.start();
        while (c.m_busy && timer.elapsed() < 5000) { QCoreApplication::processEvents(); QThread::msleep(1); }
        auto replacement = c.m_current["photos"].toArray()[0].toObject();
        if (c.m_busy || replacement["id"] == oldId || !c.m_current["deleted_photos"].toArray().contains(oldId)) return false;
        if (QImage(QUrl(replacement["path"].toString()).toLocalFile()).size() != QSize(16, 12)) return false;
        if (!QFile::exists(photo) || QFile::exists(QUrl(original["path"].toString()).toLocalFile())) return false;
        if (!c.m_current["_defer_photo_sync"].toBool()) return false;
        c.m_current["status"] = "closed";
        c.cropPhoto(replacement["id"].toString(), 0, 0, .5, .5);
        if (c.m_busy) return false;
        c.m_current["status"] = "draft";
        if (!c.attachToReport(c.m_current["id"].toString(), "EST.01", QUrl::fromLocalFile(video))) return false;
        auto movie = c.m_current["photos"].toArray()[1].toObject();
        if (movie["mime"] != "video/mp4" || !movie["path"].toString().endsWith(".mp4")) return false;
        c.cropPhoto(movie["id"].toString(), 0, 0, .5, .5);
        if (c.m_busy) return false;
        auto pdfReport = c.m_current;
        pdfReport["status"] = "closed";
        QString error;
        if (!ReportPdf::render(pdfReport, &error).startsWith("%PDF-")) { qWarning() << error; return false; }
        c.removePhoto(movie["id"].toString());
        if (QFile::exists(QUrl(movie["path"].toString()).toLocalFile()) || !QFile::exists(video)) return false;
        return c.m_current["photos"].toArray().size() == 1;
    }
};
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    QTemporaryDir root;
    if (argc != 2) return 1;
    QFile file(argv[1]); if (!file.open(QIODevice::ReadOnly)) return 2;
    const auto bytes = file.readAll();
    if (EvidenceMedia::videoMime(bytes) != "video/mp4" || !EvidenceMedia::durationValid(bytes) || EvidenceMedia::durationValid(bytes.left(40))) return 3;
    if (SafeStorage::photoPath(root.path(), "../evil", "video/mp4") != QString()) return 4;
    QImage photo(32,24,QImage::Format_RGB32); photo.fill(Qt::cyan);
    const auto source = root.filePath("gallery.png"); if (!photo.save(source)) return 5;
    Lem client(root.filePath("cache"));
    if (!LemReviewTest::exercise(client,source,argv[1])) return 6;
    qInfo() << "PASS: bounded MP4, crop coordinates, replacement journal, original preservation, draft guards, video PDF and private removal";
}
