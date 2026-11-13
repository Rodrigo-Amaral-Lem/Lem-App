// SPDX-License-Identifier: GPL-3.0-only
// Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

#include "Lem.h"
#include "Measurements.h"
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
    QTemporaryDir root;
    Lem client(root.path()); client.demo();
    for(const auto &section : client.sections())
        for(const auto &item : section.toMap()["items"].toList())
            client.answer(item.toMap()["id"].toString(),"NA","Sem conector/veículo disponível");
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
