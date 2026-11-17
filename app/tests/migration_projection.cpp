// SPDX-License-Identifier: GPL-3.0-only
// Pure JSON adapter: Python tests compare the native and server projections.
#include "Checklist.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <cstdio>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc != 3) return 2;
    QFile input(QString::fromLocal8Bit(argv[1]));
    if (!input.open(QIODevice::ReadOnly)) return 3;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(input.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isArray()) return 4;
    QJsonArray projected;
    for (const auto value : document.array()) {
        const auto test = value.toObject();
        if (test["operation"] == "connectors") {
            QString failure;
            projected.append(Checklist::forCharger(test["template"].toObject(), test["details"].toObject(), &failure));
            if (!failure.isEmpty()) return 5;
        } else projected.append(Checklist::correctedDraft(test));
    }
    QFile output(QString::fromLocal8Bit(argv[2]));
    if (!output.open(QIODevice::WriteOnly)) return 6;
    const auto bytes = QJsonDocument(projected).toJson(QJsonDocument::Compact);
    return output.write(bytes) == bytes.size() ? 0 : 7;
}
