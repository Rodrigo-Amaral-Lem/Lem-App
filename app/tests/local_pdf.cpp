// SPDX-License-Identifier: GPL-3.0-only
// Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

// Native PDF regression: no login/server, local changes, photos, pagination and safe failures.
#include "Lem.h"
#include "ReportPdf.h"
#include "Checklist.h"
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QImage>
#include <QFile>
#include <QElapsedTimer>
#include <QThread>
#include <QDebug>
#include <QDesktopServices>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            qCritical() << "FAILED" << #x << __LINE__;                                             \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)
// Intercept the OS launch so tests never start an external viewer. Reading the
// PDF here also verifies that launch happens after the export is fully saved.
class PdfViewerProbe : public QObject {
    Q_OBJECT
  public:
    QList<QUrl> opened;
    bool completeAtLaunch = false;
  public slots:
    void view(const QUrl &url) {
        opened.append(url);
        QFile file(url.toLocalFile());
        completeAtLaunch = file.open(QIODevice::ReadOnly) && file.readAll().endsWith("%%EOF\n");
    }
};
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    app.setApplicationVersion("0.1.52");
    PdfViewerProbe viewer;
    QDesktopServices::setUrlHandler("file", &viewer, "view");
    QTemporaryDir temp;
    Lem client(temp.path() + "/data");
    client.demo();
    CHECK(!client.authenticated());
    QImage image(320, 240, QImage::Format_RGB32);
    image.fill(Qt::darkCyan);
    const auto photo = temp.path() + "/evidence.png";
    CHECK(image.save(photo));
    client.attach("EST.01", QUrl::fromLocalFile(photo));
    client.answer("EST.01", "NOK",
                  QString("Observação com acentuação: tensão e proteção.\n").repeated(160) +
                      "FIM DAS OBSERVAÇÕES");
    // A draft must not even open a destination picker or alter a chosen file.
    CHECK(!client.prepareExport("pdf"));
    CHECK(!client.prepareExport("bundle"));
    const auto blocked = temp.path() + "/draft.pdf";
    client.exportReport("pdf", QUrl::fromLocalFile(blocked));
    CHECK(!QFile::exists(blocked));
    CHECK(viewer.opened.isEmpty());
    auto draft = QJsonObject::fromVariantMap(client.current());
    CHECK(!ReportPdf::validate(draft).isEmpty());
    for (const auto &section : client.sections())
        for (const auto &item : section.toMap()["items"].toList()) {
            const auto id = item.toMap()["id"].toString();
            if (id != "EST.01")
                client.answer(id, "NA", "Sem conector/veículo disponível");
        }
    client.finish();
    CHECK(client.current()["status"].toString() == "closed");
    CHECK(client.prepareExport("pdf"));
    const auto destination =
        argc > 1 ? QString::fromLocal8Bit(argv[1]) : temp.path() + "/report.pdf";
    client.exportReport("pdf", QUrl::fromLocalFile(destination));
    QElapsedTimer timer;
    timer.start();
    while (client.busy() && timer.elapsed() < 20000) {
        app.processEvents();
        QThread::msleep(5);
    }
    CHECK(!client.busy());
    QFile result(destination);
    CHECK(result.open(QIODevice::ReadOnly));
    CHECK(result.readAll().startsWith("%PDF-"));
    result.close();
    CHECK(viewer.opened == QList<QUrl>{QUrl::fromLocalFile(destination)});
    CHECK(viewer.completeAtLaunch);
    // A failed destination must never launch a viewer for an incomplete file.
    client.exportReport("pdf", QUrl::fromLocalFile(temp.path() + "/missing/report.pdf"));
    timer.restart();
    while (client.busy() && timer.elapsed() < 20000) {
        app.processEvents();
        QThread::msleep(5);
    }
    CHECK(!client.busy());
    CHECK(viewer.opened.size() == 1);
    auto report = QJsonObject::fromVariantMap(client.current());
    auto snapshot = report["snapshot"].toObject();
    auto charger = snapshot["charger"].toObject();
    charger["power"] = "94";
    charger["power_unit"] = "kVA";
    charger["power_limit"] = "94 kVA";
    charger["import_profile"] = "lem-stations-v1";
    snapshot["charger"] = charger;
    report["snapshot"] = snapshot;
    QString presentationError;
    const auto presentation = ReportPdf::render(report, &presentationError);
    CHECK(presentationError.isEmpty() && presentation.startsWith("%PDF-"));
    if (argc > 1) {
        QFile rendered(destination + ".presentation.pdf");
        CHECK(rendered.open(QIODevice::WriteOnly));
        CHECK(rendered.write(presentation) == presentation.size());
    }
    auto photos = report["photos"].toArray();
    auto missing = photos[0].toObject();
    missing["path"] = "file:///missing-image.png";
    photos[0] = missing;
    report["photos"] = photos;
    CHECK(!ReportPdf::validate(report).isEmpty());
    const auto safe = temp.path() + "/keep.pdf";
    QFile kept(safe);
    CHECK(kept.open(QIODevice::WriteOnly));
    kept.write("KEEP");
    kept.close();
    QString error;
    CHECK(!ReportPdf::write(report, safe, &error));
    CHECK(kept.open(QIODevice::ReadOnly));
    CHECK(kept.readAll() == "KEEP");
    kept.close();
    QFile corrupt(temp.path() + "/invalid.png");
    CHECK(corrupt.open(QIODevice::WriteOnly));
    corrupt.write("corrupt");
    corrupt.close();
    missing["path"] = QUrl::fromLocalFile(corrupt.fileName()).toString();
    photos[0] = missing;
    report["photos"] = photos;
    CHECK(!ReportPdf::write(report, safe, &error));
    CHECK(kept.open(QIODevice::ReadOnly));
    CHECK(kept.readAll() == "KEEP");
    CHECK(Checklist::label({{"id", "MAN.04"}, {"label", "Tensão (380-420 V)"}})
              .contains("360-440 V"));
    CHECK(Checklist::label({{"id", "MAN.07"}, {"label", "Tensão (200-250 V)"}})
              .contains("207-253 V"));
    QDesktopServices::unsetUrlHandler("file");
    qInfo()
        << "PASS: offline PDF, photos and long notes; viewer launch after save; no launch for drafts/failed writes; missing/corrupt evidence preserves destination";
}

#include "local_pdf.moc"
