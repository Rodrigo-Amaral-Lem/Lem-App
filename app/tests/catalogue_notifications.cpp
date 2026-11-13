// SPDX-License-Identifier: GPL-3.0-only
// Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

// A progress/status notification must not cause the catalogue to be rebuilt in QML.
#include "Lem.h"
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QDebug>

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    app.setOrganizationName("Lem-Performance-Test");
    app.setApplicationName("Catalogue notifications");
    QTemporaryDir root;
    Lem lem(root.path());
    int notifications = 0;
    QObject::connect(&lem, &Lem::chargersChanged, [&] { ++notifications; });
    lem.demo();
    if (lem.chargers().isEmpty() || notifications == 0)
        return 1;
    const auto rows = lem.chargers();
    notifications = 0;
    QElapsedTimer timer;
    timer.start();
    for (int i = 0; i < 1000; ++i) {
        lem.changed();
        if (lem.chargers() != rows)
            return 2;
    }
    if (notifications != 0)
        return 3;
    qInfo() << "1000 unchanged status notifications:" << timer.elapsed()
            << "ms; catalogue notifications:" << notifications;
    lem.openReport("demo-charger");
    if (notifications == 0 || lem.chargers() == rows)
        return 4;
    qInfo() << "New report updates catalogue immediately";
    return 0;
}
