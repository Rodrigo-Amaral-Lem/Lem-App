// SPDX-License-Identifier: GPL-3.0-only
// Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

// Disposable HTTP harness verifies the real C++ pre-login handshake, not a QML mock.
#include "Lem.h"
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QSettings>
#include <QThread>
#include <QDebug>

namespace {
bool waitFor(const std::function<bool()> &ready) {
    QElapsedTimer timer;
    timer.start();
    while (!ready() && timer.elapsed() < 15000) {
        QCoreApplication::processEvents();
        QThread::msleep(5);
    }
    return ready();
}

// The Python runner starts a separate process for each step using the same profile.
bool checkSession(const QString &url, const QString &action, const QString &profile) {
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, profile);
    QSettings settings;
    if (!settings.contains("server")) {
        ServerSettings::save(settings, url);
        settings.sync();
    }
    Lem client(profile);
    const auto loadedIdentity = [&] { return client.user()["email"] == "admin@lem.pt"; };
    if (action == "login") {
        client.login("admin@lem.pt", "CompatibilityPassword123!");
        return waitFor([&] { return !client.busy() && !client.refreshing(); }) &&
               client.authenticated() && loadedIdentity();
    }
    if (action == "signed-out")
        return !client.authenticated() && !client.sessionPending();
    if (action == "reset-server") {
        client.setServer(url);
        return client.server() == url && !client.authenticated() && !client.sessionPending();
    }
    if (!client.sessionPending() || client.authenticated() || !loadedIdentity())
        return false;
    client.refresh();
    if (!waitFor([&] { return !client.refreshing(); }))
        return false;
    if (action == "offline")
        return client.sessionPending() && !client.authenticated() && loadedIdentity();
    if (action == "rejected")
        return !client.sessionPending() && !client.authenticated() && loadedIdentity();
    if (!waitFor([&] { return client.authenticated() &&
                              client.clientCompatibility()["update_available"].toBool(); }))
        return false;
    if (action == "switch") {
        client.setServer("https://other.example.test");
        return !client.authenticated() && !client.sessionPending() &&
               client.server() == "https://other.example.test";
    }
    if (action == "logout") {
        client.logout();
        return !client.authenticated() && !client.sessionPending() && client.user().isEmpty() &&
               waitFor([&] { return !client.operationStatus()["active"].toBool(); });
    }
    return action == "resume" && loadedIdentity();
}
}

int main(int argc, char **argv) {
    QGuiApplication app(argc, argv);
    app.setOrganizationName("Lem-Compatibility-Test");
    app.setApplicationName("Lem-Compatibility-Test");
    // The runner derives its policy scenarios from the current project version.
    // A hard-coded historical version is rejected by the server's protocol floor.
    app.setApplicationVersion(qEnvironmentVariable("LEM_TEST_CLIENT_VERSION"));
    if (app.applicationVersion().isEmpty()) return 2;
    if (argc == 4)
        return checkSession(QString::fromLocal8Bit(argv[1]), QString::fromLocal8Bit(argv[2]),
                            QString::fromLocal8Bit(argv[3])) ? 0 : 1;
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
