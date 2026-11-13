#include "Checklist.h"
#include "ReportPdf.h"
#include <QGuiApplication>
#include <QFile>
#include <QJsonDocument>
#include <QDebug>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            qCritical() << "FAILED" << __LINE__ << #x;                                             \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    CHECK(argc > 1);
    QFile file(argv[1]);
    CHECK(file.open(QIODevice::ReadOnly));
    const auto cases = QJsonDocument::fromJson(file.readAll()).array();
    QFile schema(":/shared/checklist-v1.json");
    CHECK(schema.open(QIODevice::ReadOnly));
    const auto base = QJsonDocument::fromJson(schema.readAll()).object();
    QJsonObject example;
    for (auto entry : cases) {
        auto c = entry.toObject();
        QString error;
        auto result = Checklist::forCharger(base, {{"connectors", c["input"]}}, &error);
        if (c["error"].toBool()) {
            CHECK(!error.isEmpty());
            continue;
        }
        CHECK(error.isEmpty());
        CHECK(result["load_test_connectors"] == c["names"]);
        QJsonArray tests;
        for (auto section : result["sections"].toArray())
            if (section.toObject()["id"] == "CARG")
                tests = section.toObject()["items"].toArray();
        CHECK(tests.size() == qMax(1, c["names"].toArray().size()) + 3);
        for (int i = 0; i < qMax(1, c["names"].toArray().size()); ++i)
            CHECK(tests[i].toObject()["id"] == QString("CARG.01.%1").arg(i + 1, 2, 10, QChar('0')));
        if (c.contains("labels")) {
            for (int i = 0; i < c["labels"].toArray().size(); ++i)
                CHECK(tests[i].toObject()["label"] == c["labels"].toArray()[i]);
            example = result;
        }
    }
    if (argc > 2) {
        QJsonObject report{
            {"id", "connector-demo"},
            {"status", "closed"},
            {"template", example},
            {"snapshot", QJsonObject{{"charger", QJsonObject{{"external_id", "DEMO-2-CCS2"},
                                                             {"address", "Rua de demonstração"},
                                                             {"postal_code", "1000-001 Lisboa"},
                                                             {"connectors", "CCS2, CCS2"}}},
                                     {"contract", QJsonObject{{"name", "Demonstração"}}}}},
            {"payload",
             QJsonObject{
                 {"answers",
                  QJsonObject{{"CARG.01.01", QJsonObject{{"value", "OK"}}},
                              {"CARG.01.02",
                               QJsonObject{{"value", "NOK"},
                                           {"notes", "Segundo conector sem iniciar carga."}}}}}}}};
        QString error;
        CHECK(ReportPdf::write(report, QString::fromLocal8Bit(argv[2]), &error));
    }
    qInfo() << "PASS: shared connector cases, IDs, duplicate labels and native PDF";
}
