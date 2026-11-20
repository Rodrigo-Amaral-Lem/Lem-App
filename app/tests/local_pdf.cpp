// SPDX-License-Identifier: GPL-3.0-only
// Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

// Native PDF regression: no login/server, local changes, photos, pagination and safe failures.
#include "Lem.h"
#include "TestSignature.h"
#include "ReportPdf.h"
#include "ReportNaming.h"
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
    {
    // Both offline logo variants decode from the same bundled catalogue as import UI.
    const auto brands = client.reportBrands();
    CHECK(brands.size() == 12);
    QJsonArray pair;
    for (const auto &brand : brands) {
        const auto entry = QJsonObject::fromVariantMap(brand.toMap());
        CHECK(!QImage::fromData(QByteArray::fromBase64(entry["png"].toString().toLatin1())).isNull());
        if (entry["id"] == "APS" || entry["id"] == "Galp") pair.append(entry);
    }
    auto branded = QJsonObject::fromVariantMap(client.current());
    branded["status"] = "closed";
    auto snapshot = branded["snapshot"].toObject();
    auto contract = snapshot["contract"].toObject();
    contract["report_logos"] = pair;
    snapshot["contract"] = contract;
    branded["snapshot"] = snapshot;
    QString brandingError;
    auto brandingPdf = ReportPdf::render(branded, &brandingError);
    CHECK(brandingError.isEmpty() && brandingPdf.startsWith("%PDF"));
    if (!qEnvironmentVariable("LEM_BRANDING_PREVIEW").isEmpty()) {
        QFile preview(qEnvironmentVariable("LEM_BRANDING_PREVIEW"));
        CHECK(preview.open(QIODevice::WriteOnly));
        CHECK(preview.write(brandingPdf) == brandingPdf.size());
        const QList<QJsonArray> variants{{}, {pair[0]}, {QJsonObject{{"id", "Lem"}}},
                                        {QJsonObject{{"id", "Lem"}}, pair[0]}};
        int index = 0;
        for (const auto &logos : variants) {
            contract["report_logos"] = logos;
            snapshot["contract"] = contract;
            branded["snapshot"] = snapshot;
            const auto data = ReportPdf::render(branded, &brandingError);
            CHECK(brandingError.isEmpty() && data.startsWith("%PDF"));
            QFile variant(preview.fileName() + "." + QString::number(index++) + ".pdf");
            CHECK(variant.open(QIODevice::WriteOnly));
            CHECK(variant.write(data) == data.size());
        }
    }

    }
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
    CHECK(addTestSignature(client));
    client.finish();
    CHECK(client.current()["status"].toString() == "closed");
    const auto identifier = ReportNaming::identifier(QJsonObject::fromVariantMap(client.current()));
    CHECK(client.current()["identifier"].toString() == identifier);
    CHECK(QFileInfo(QUrl(client.defaultExport("pdf")).toLocalFile()).fileName() == identifier + ".pdf");
    CHECK(QFileInfo(QUrl(client.defaultExport("bundle")).toLocalFile()).fileName() == identifier + ".zip");
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
    // Complete evidence permits an offline ZIP too, before synchronization/login.
    CHECK(!client.authenticated() && client.prepareExport("bundle"));
    const auto bundle = temp.filePath("offline.zip");
    client.exportReport("bundle", QUrl::fromLocalFile(bundle));
    timer.restart();
    while (client.busy() && timer.elapsed() < 20000) { app.processEvents(); QThread::msleep(5); }
    CHECK(!client.busy() && client.message().contains("ZIP exportado localmente"));
    QFile zipped(bundle); CHECK(zipped.open(QIODevice::ReadOnly)); CHECK(zipped.read(4) == QByteArray::fromHex("504b0304"));
    CHECK(viewer.opened.size() == 1); // ZIP save does not launch an external application.
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
    // Observation-only fields are not unanswered checklist results.
    auto notesReport = report;
    auto notesSnapshot = notesReport["snapshot"].toObject();
    auto notesCharger = notesSnapshot["charger"].toObject();
    notesCharger["location"] = "LOCAL_OMITIDO";
    notesCharger["address"] = "Morada preservada";
    notesSnapshot["charger"] = notesCharger;
    notesReport["snapshot"] = notesSnapshot;
    notesReport["photos"] = QJsonArray{};
    QJsonArray notesItems;
    for (auto section : report["template"].toObject()["sections"].toArray())
        for (auto entry : section.toObject()["items"].toArray())
            if (entry.toObject()["kind"] == "notes") notesItems.append(entry);
    CHECK(notesItems.size() == 7);
    QJsonObject noteAnswers;
    noteAnswers[notesItems[1].toObject()["id"].toString()] = QJsonObject{};
    noteAnswers[notesItems[2].toObject()["id"].toString()] = QJsonObject{{"value", "  "}, {"notes", "   "}};
    noteAnswers[notesItems[3].toObject()["id"].toString()] = QJsonValue::Null;
    noteAnswers[notesItems[4].toObject()["id"].toString()] = QJsonObject{{"notes", "Nota escrita preservada"}};
    noteAnswers[notesItems[5].toObject()["id"].toString()] = QJsonObject{{"value", "Texto antigo preservado"}};
    noteAnswers[notesItems[6].toObject()["id"].toString()] = QJsonObject{{"value", "Valor historico preservado"}, {"notes", "Comentario preservado"}};
    notesItems.append(QJsonObject{{"id", "EST.02"}, {"label", "Fabricante"}, {"kind", "text"}});
    notesReport["template"] = QJsonObject{{"sections", QJsonArray{QJsonObject{{"id", "TEST"}, {"title", "Observações"}, {"items", notesItems}}}}};
    notesReport["payload"] = QJsonObject{{"answers", noteAnswers}};
    const auto notesPdf = ReportPdf::render(notesReport, &presentationError);
    CHECK(presentationError.isEmpty() && notesPdf.startsWith("%PDF-"));
    if (argc > 1) {
        QFile rendered(destination + ".notes.pdf");
        CHECK(rendered.open(QIODevice::WriteOnly));
        CHECK(rendered.write(notesPdf) == notesPdf.size());
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
    // Pending local work cannot use a broken original as evidence.
    CHECK(!ReportPdf::write(report, safe, &error));
    CHECK(kept.open(QIODevice::ReadOnly) && kept.readAll() == "KEEP");
    kept.close();
    // The server already accepted this historical original. Retain its visible warning.
    missing["uploaded"] = true;
    photos[0] = missing;
    report["photos"] = photos;
    CHECK(ReportPdf::write(report, safe, &error));
    CHECK(kept.open(QIODevice::ReadOnly));
    CHECK(kept.readAll().startsWith("%PDF-"));
    CHECK(Checklist::label({{"id", "MAN.04"}, {"label", "Tensão (380-420 V)"}})
              .contains("360-440 V"));
    CHECK(Checklist::label({{"id", "MAN.07"}, {"label", "Tensão (200-250 V)"}})
              .contains("207-253 V"));
    QDesktopServices::unsetUrlHandler("file");
    qInfo()
        << "PASS: offline PDF, photos and long notes; viewer launch after save; missing evidence preserves destination; corrupt evidence rendered with warning";
}

#include "local_pdf.moc"
