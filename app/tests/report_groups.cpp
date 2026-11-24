// SPDX-License-Identifier: GPL-3.0-only
// Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

#include "Lem.h"
#include "ReportPdf.h"
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QElapsedTimer>
#include <QDebug>

#define CHECK(x) do { if (!(x)) { qCritical() << "FAIL" << __LINE__ << #x; return false; } } while (false)
struct LemReviewTest {
    static bool run(Lem &c, const QString &root, const QString &pdfPath) {
        c.demo();
        CHECK(c.m_template["version"].toInt() == 8);
        const auto sourceId = c.m_current["id"].toString();
        c.answer("QVE.01", "OK", "Botoneira comum");
        c.answer("QVE.02", "NOK", "Substituir contacto");
        QImage photo(120, 80, QImage::Format_RGB32); photo.fill(Qt::yellow);
        const auto inputPath = root + "/input.png"; CHECK(photo.save(inputPath));
        c.attach("QVE.01", QUrl::fromLocalFile(inputPath));
        CHECK(c.flushEdits());
        const auto source = c.m_current;
        const auto sourcePhoto = source["photos"].toArray()[0].toObject();
        auto second = c.m_chargers[0].toObject(); second["id"] = "second-charger"; second["external_id"] = "PCVE-DEMO-002";
        c.m_chargers.append(second);
        c.openReport("second-charger");
        const auto targetId = c.m_current["id"].toString();
        c.answer("EST.06", "OK", "Não alterar");
        c.answer("QVE.01", "NA", "antigo");
        c.attach("QVE.04", QUrl::fromLocalFile(inputPath));
        const auto oldTargetPhoto = c.m_current["photos"].toArray()[0].toObject();
        CHECK(c.qveCopySources().size() == 1);
        c.copyQveFrom(sourceId);
        QElapsedTimer timer; timer.start();
        while (!c.m_copyScope.isEmpty() && timer.elapsed() < 3000) QCoreApplication::processEvents();
        CHECK(c.m_copyScope.isEmpty());
        CHECK(c.m_current["id"].toString() == targetId);
        CHECK(c.m_current["payload"].toObject()["answers"].toObject()["QVE.02"].toObject()["notes"] == "Substituir contacto");
        CHECK(c.m_current["payload"].toObject()["answers"].toObject()["EST.06"].toObject()["notes"] == "Não alterar");
        CHECK(c.m_reports[sourceId].toObject()["payload"] == source["payload"]);
        CHECK(c.m_reports[sourceId].toObject()["photos"] == source["photos"]);
        const auto copiedPhoto = c.m_current["photos"].toArray()[0].toObject();
        CHECK(copiedPhoto["id"] != sourcePhoto["id"] && copiedPhoto["checkpoint"] == "QVE.01");
        CHECK(!copiedPhoto["uploaded"].toBool());
        QFile a(QUrl(sourcePhoto["path"].toString()).toLocalFile()), b(QUrl(copiedPhoto["path"].toString()).toLocalFile());
        CHECK(a.open(QIODevice::ReadOnly) && b.open(QIODevice::ReadOnly)); CHECK(a.readAll() == b.readAll());
        CHECK(!QFile::exists(QUrl(oldTargetPhoto["path"].toString()).toLocalFile()));
        CHECK(c.m_current["deleted_photos"].toArray().contains(oldTargetPhoto["id"]));
        // A missing source file must not partially replace any destination answer/file.
        auto broken = source; auto brokenPhotos = source["photos"].toArray(); auto missing = brokenPhotos[0].toObject();
        missing["id"] = "missing-evidence"; missing["path"] = "file:///missing-evidence.png";
        brokenPhotos[0] = missing; broken["photos"] = brokenPhotos; c.m_reports[sourceId] = broken;
        const auto before = c.m_current; c.copyQveFrom(sourceId);
        CHECK(c.m_copyScope.isEmpty() && c.m_current == before); c.m_reports[sourceId] = source;
        // Blank signatures are rejected; real handwriting is a durable PNG reference.
        QImage signature(500, 160, QImage::Format_RGB32); signature.fill(Qt::white);
        auto dataUrl = [&]() { QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly); signature.save(&buffer, "PNG"); return QString("data:image/png;base64,") + QString::fromLatin1(bytes.toBase64()); };
        CHECK(!c.saveSignature("FIN.02", dataUrl()));
        { QPainter pen(&signature); pen.setPen(QPen(Qt::black, 4)); pen.drawLine(30,130,150,20); pen.drawLine(150,20,300,120); }
        CHECK(c.saveSignature("FIN.02", dataUrl()));
        const auto signaturePhoto = root + "/signature.jpg";
        CHECK(signature.save(signaturePhoto, "JPG"));
        CHECK(c.importSignature("FIN.02", QUrl::fromLocalFile(signaturePhoto)));
        const auto attachedSignature = c.m_current["payload"].toObject()["signatures"].toObject()["FIN.02"].toObject();
        CHECK(!attachedSignature["photo_id"].toString().isEmpty());
        CHECK(!c.importSignature("FIN.02", QUrl("https://invalid.example/signature.jpg")));
        auto signedEntry = c.m_current["payload"].toObject()["signatures"].toObject()["FIN.02"].toObject();
        CHECK(!signedEntry["photo_id"].toString().isEmpty());
        const auto firstSignature = signedEntry["photo_id"].toString();
        CHECK(c.saveSignature("FIN.02", dataUrl()));
        CHECK(c.m_current["deleted_photos"].toArray().contains(firstSignature));
        CHECK(!QFile::exists(c.cacheDir() + "/photos/" + firstSignature + ".png"));
        c.answer("FIN.02", "Outro técnico", "");
        CHECK(c.m_current["payload"].toObject()["signatures"].toObject().isEmpty());
        CHECK(c.saveSignature("FIN.02", dataUrl()));
        CHECK(c.persist());
        Lem reloaded(root); reloaded.demo(); reloaded.openReport("second-charger", targetId);
        CHECK(reloaded.m_current["payload"].toObject()["signatures"] == c.m_current["payload"].toObject()["signatures"]);
        CHECK(reloaded.m_current["payload"].toObject()["qve_copy"] == c.m_current["payload"].toObject()["qve_copy"]);
        for (auto section : c.m_template["sections"].toArray())
            for (auto item : section.toObject()["items"].toArray()) {
                const auto key = item.toObject()["id"].toString();
                if (key != "FIN.02") c.answer(key, "NA", "Sem conector/veículo disponível");
            }
        c.attach("MAN.13", QUrl::fromLocalFile(inputPath));
        c.finish(); CHECK(c.m_current["status"] == "closed");
        QString error;
        CHECK(ReportPdf::write(c.m_current, pdfPath, &error));
        QFile pdf(pdfPath); CHECK(pdf.open(QIODevice::ReadOnly)); CHECK(pdf.read(5) == "%PDF-");
        qInfo() << "PASS: QVE copy isolation, new evidence IDs, atomic failure, signatures, replacement, name invalidation and reload";
        return true;
    }
};
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv); app.setApplicationVersion("0.1.55");
    QTemporaryDir dir; Lem client(dir.path()); return LemReviewTest::run(client, dir.path(), argc > 1 ? QString::fromLocal8Bit(argv[1]) : dir.filePath("signature.pdf")) ? 0 : 1;
}
