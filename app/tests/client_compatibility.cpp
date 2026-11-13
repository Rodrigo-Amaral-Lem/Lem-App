// Disposable HTTP harness verifies the real C++ pre-login handshake, not a QML mock.
#include "Lem.h"
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QThread>
#include <QDebug>
int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    app.setOrganizationName("Lem-Compatibility-Test");
    app.setApplicationName("Lem-Compatibility-Test");
    app.setApplicationVersion("0.1.32");
    if (argc != 3)
        return 2;
    QTemporaryDir directory;
    Lem client(directory.path());
    client.setServer(QString::fromLocal8Bit(argv[1]));
    client.login("admin@lem.pt", "CompatibilityPassword123!");
    QElapsedTimer elapsed;
    elapsed.start();
    while (client.busy() && elapsed.elapsed() < 15000) {
        app.processEvents();
        QThread::msleep(5);
    }
    const auto policy = client.clientCompatibility();
    const QString expected = QString::fromLocal8Bit(argv[2]);
    const bool allowed = expected == "latest" || expected == "update";
    if (client.busy() || client.authenticated() != allowed ||
        policy["compatible"].toBool() != allowed ||
        policy["update_available"].toBool() != (expected == "update")) {
        qCritical() << "Unexpected policy/login state" << policy << client.message();
        return 1;
    }
    qInfo() << "PASS: pre-login handshake" << expected
            << "authenticated:" << client.authenticated();
}
