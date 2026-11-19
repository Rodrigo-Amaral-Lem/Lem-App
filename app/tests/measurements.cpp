// SPDX-License-Identifier: GPL-3.0-only
// Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

#include "Lem.h"
#include "TestSignature.h"
#include "Measurements.h"
#include "Checklist.h"
#include <QDir>
#include <QFileInfo>
#include "ReportPdf.h"
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QImage>
#include <QDebug>
#define CHECK(x) do { if (!(x)) { qCritical() << "FAILED" << __LINE__ << #x; return 1; } } while(false)
int main(int argc,char **argv) {
    QGuiApplication app(argc,argv);
    app.setOrganizationName("Lem-Measurement-Test"); app.setApplicationName("Lem-Measurement-Test");
    CHECK(argc>1);
    QFile cases(argv[1]); CHECK(cases.open(QIODevice::ReadOnly));
    for (const auto &entry : QJsonDocument::fromJson(cases.readAll()).array()) {
        const auto c=entry.toObject(); bool valid=false;
        const auto value=Measurements::format(c["id"].toString(),c["input"].toString(),&valid);
        CHECK(valid != c["invalid"].toBool());
        if(valid) {
            CHECK(value==c["formatted"].toString());
            CHECK(Measurements::outside(c["id"].toString(),c["input"].toString())==c["nok"].toBool());
        }
    }
    const QDir shared = QFileInfo(QString::fromLocal8Bit(argv[1])).dir();
    QFile maintenanceCases(shared.filePath("maintenance-cases.json")); CHECK(maintenanceCases.open(QIODevice::ReadOnly));
    for (const auto entry : QJsonDocument::fromJson(maintenanceCases.readAll()).array()) {
        const auto c = entry.toObject();
        const QJsonObject readings{{"MAN.11", QJsonObject{{"value", c["configured"]}}},
            {"MAN.12", QJsonObject{{"value", c["trip"]}}}, {"MAN.18", QJsonObject{{"value", c["earth"]}}}};
        const auto result = MaintenanceRules::calculate(readings);
        CHECK(result.value("MAN.12").state == c["trip_state"].toString());
        CHECK(result.value("MAN.18").state == c["earth_state"].toString());
        CHECK(result.value("MAN.18").note == (c["earth_note"] == "high" ? MaintenanceRules::earthHighNote() :
              c["earth_note"] == "fail" ? MaintenanceRules::earthFailNote() : QString()));
    }
    QFile legacyFile(shared.filePath("checklist-v6.json")); CHECK(legacyFile.open(QIODevice::ReadOnly));
    const auto legacySchema = QJsonDocument::fromJson(legacyFile.readAll()).object();
    const QJsonObject legacyPayload{{"answers", QJsonObject{
        {"MAN.21", QJsonObject{{"value", ""}, {"notes", "Preservar manutenção"}}},
        {"EST.10", QJsonObject{{"value", "NOK"}, {"notes", "Preservar limpeza"}}},
        {"MAN.11", QJsonObject{{"value", "0,03 A"}, {"notes", ""}}},
        {"MAN.12", QJsonObject{{"value", "30"}, {"notes", ""}}}}}};
    QJsonObject legacy{{"status", "draft"}, {"template", legacySchema}, {"payload", legacyPayload},
        {"_inflight_payload", legacyPayload}, {"photos", QJsonArray{
            QJsonObject{{"id", "keep-one"}, {"checkpoint", "MAN.21"}},
            QJsonObject{{"id", "keep-two"}, {"checkpoint", "EST.10"}}}}};
    const auto upgraded = Checklist::correctedDraft(legacy);
    CHECK(upgraded["template"].toObject()["version"] == 7);
    CHECK(Checklist::correctedDraft(upgraded) == upgraded);
    CHECK(upgraded["payload"] == upgraded["_inflight_payload"]);
    const auto migrated = upgraded["payload"].toObject()["answers"].toObject();
    CHECK(migrated["MAN.22"].toObject()["notes"] == "Preservar manutenção");
    CHECK(migrated["EST.18"].toObject()["notes"] == "Preservar limpeza");
    CHECK(!migrated.contains("EST.10") && !migrated.contains("MAN.21"));
    CHECK(migrated["MAN.11"].toObject()["value"] == "30 mA");
    CHECK(migrated["MAN.12"].toObject()["previous_value"] == "30");
    CHECK(migrated["MAN.12"].toObject()["value"] == "");
    CHECK(upgraded["photos"].toArray()[0].toObject()["checkpoint"] == "MAN.22");
    CHECK(upgraded["photos"].toArray()[1].toObject()["checkpoint"] == "EST.18");
    legacy["status"] = "closed"; CHECK(Checklist::correctedDraft(legacy) == legacy);

    QTemporaryDir root;
    Lem client(root.path()); client.demo();
    const auto answer = [&client](const QString &key) {
        return QJsonObject::fromVariantMap(client.current()["payload"].toMap()["answers"].toMap()[key].toMap());
    };
    client.answer("EST.15", "NA", ""); CHECK(answer("EST.15")["notes"] == "Sem TPM");
    client.answer("EST.15", "OK", "Sem TPM"); CHECK(answer("EST.15")["notes"] == "");
    client.answer("EST.15", "NA", ""); client.beginNotesEdit("EST.15");
    CHECK(answer("EST.15")["notes"] == "" && !answer("EST.15").contains("suggested_notes"));
    client.answer("EST.15", "NA", "Sem POS"); // Identical wording, now authored by the technician.
    client.answer("EST.15", "OK", "Sem POS"); CHECK(answer("EST.15")["notes"] == "Sem POS");
    client.answer("MAN.11", "1000", ""); client.answer("MAN.12", "500", "");
    CHECK(answer("MAN.12")["assessment"] == "NOK"); CHECK(MaintenanceRules::untouched(answer("MAN.12")));
    // N/A replaces the numeric-failure suggestion with its own reason; clearing
    // a reading must not leave an obsolete automatic observation behind.
    client.answer("MAN.12", "NA", answer("MAN.12")["notes"].toString());
    CHECK(answer("MAN.12")["notes"] == QStringLiteral("Não testado devido a veículo em carga"));
    client.answer("MAN.12", "500", answer("MAN.12")["notes"].toString());
    CHECK(answer("MAN.12")["notes"] == MaintenanceRules::tripNote());
    client.answer("MAN.12", "", answer("MAN.12")["notes"].toString());
    CHECK(answer("MAN.12")["notes"] == "");
    client.answer("MAN.12", "500", "");
    client.beginNotesEdit("MAN.12"); CHECK(answer("MAN.12")["notes"] == "");
    client.answer("MAN.12", "500", "Observação do técnico");
    client.answer("MAN.12", "900", "Observação do técnico");
    CHECK(answer("MAN.12")["assessment"] == "OK" && answer("MAN.12")["notes"] == "Observação do técnico");
    client.answer("MAN.18", "51", ""); CHECK(answer("MAN.18")["assessment"] == "NOK");
    client.answer("MAN.11", "300", ""); CHECK(answer("MAN.18")["assessment"] == "OK");
    CHECK(answer("MAN.18")["notes"] == MaintenanceRules::earthHighNote());
    client.answer("MAN.11", "0", ""); CHECK(!answer("MAN.18").contains("assessment"));
    CHECK(answer("MAN.18")["notes"] == "");
    client.answer("MAN.11", "300", "");
    client.setAssessment("MAN.12", "OK"); CHECK(answer("MAN.12")["assessment"] == "NOK");

    for(const auto &section : client.sections())
        for(const auto &item : section.toMap()["items"].toList())
            client.answer(item.toMap()["id"].toString(),"NA","Sem conector/veículo disponível");
    CHECK(addTestSignature(client));
    client.answer("MAN.07","1000",""); client.finish();
    CHECK(client.current()["status"]=="draft");
    client.answer("MAN.07","206.99","");
    QImage photo(8,8,QImage::Format_RGB32); photo.fill(Qt::cyan);
    const auto file=root.filePath("meter.png"); CHECK(photo.save(file));
    client.attach("MAN.07",QUrl::fromLocalFile(file));
    client.attach("MAN.12",QUrl::fromLocalFile(file));
    client.finish(); CHECK(client.current()["status"]=="closed");
    CHECK(client.current()["payload"].toMap()["answers"].toMap()["MAN.07"].toMap()["value"]=="206,99 V");
    CHECK(client.current()["payload"].toMap()["answers"].toMap()["MAN.18"].toMap()["value"]==QStringLiteral("Não Aplicável"));
    if(argc>2) {
        QString error;
        CHECK(ReportPdf::write(QJsonObject::fromVariantMap(client.current()),QString::fromLocal8Bit(argv[2]),&error));
    }
    qInfo() << "PASS: measurement parity, closure validation, formatting, NA evidence exemption and native PDF";
}
