// SPDX-License-Identifier: GPL-3.0-only
// Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.
#include "ReportNaming.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDebug>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QFile cases(":/shared/report-naming-cases.json");
    if (!cases.open(QIODevice::ReadOnly)) return 1;
    for (const auto &value : QJsonDocument::fromJson(cases.readAll()).array()) {
        const auto example = value.toObject();
        const auto actual = ReportNaming::identifier(example["report"].toObject());
        if (actual != example["expected"].toString()) {
            qCritical() << example["name"] << actual << example["expected"];
            return 2;
        }
    }
    const auto emoji = QString::fromUtf8("🚗");
    const auto bounded = ReportNaming::component(emoji.repeated(100), "fallback", 73);
    if (bounded != emoji.repeated(18) || bounded.toUtf8().size() > 73) return 3;
    qInfo() << "PASS: report names, Lisbon dates, offline closure, safe UTF-8 filenames";
}
