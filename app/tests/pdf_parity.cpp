// SPDX-License-Identifier: GPL-3.0-only
// Render a supplied synthetic fixture through the production native PDF path.
#include "ReportPdf.h"
#include "ReportBundle.h"
#include <QGuiApplication>
#include <QFile>
#include <QJsonDocument>
#include <QDebug>
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    if (argc != 3 && argc != 4) return 2;
    QFile input(QString::fromLocal8Bit(argv[1]));
    if (!input.open(QIODevice::ReadOnly)) return 2;
    QString error;
    const auto report = QJsonDocument::fromJson(input.readAll()).object();
    const bool ok = argc == 4 && QByteArray(argv[3]) == "zip"
        ? ReportBundle::write(report, QString::fromLocal8Bit(argv[2]), &error)
        : ReportPdf::write(report, QString::fromLocal8Bit(argv[2]), &error);
    if (!ok) {
        qCritical() << error; return 1;
    }
    return 0;
}
