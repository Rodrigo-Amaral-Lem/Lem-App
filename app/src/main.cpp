// SPDX-License-Identifier: GPL-3.0-only
// Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

// Process entry point: configure TLS/style/identity, resolve storage, then expose Lem to QML.
// Smoke/screenshot flags use controlled startup paths for automated validation.

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTimer>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QSslSocket>
#include <QDebug>
#include <QIcon>
#ifdef LEM_WINDOWS_STANDALONE
#include <QImageReader>
#endif
#include "Lem.h"
#include "Theme.h"
#include "AppStorage.h"
int main(int argc, char **argv) {
#ifdef Q_OS_ANDROID
    // Must run before any Qt Network use. These names match the packaged libraries.
    qputenv("ANDROID_OPENSSL_SUFFIX", "_3");
#endif
    QQuickStyle::setStyle("Fusion");
    QGuiApplication app(argc, argv);
    app.setOrganizationName("Lem");
    app.setApplicationName("Lem");
    app.setApplicationDisplayName("Lem");
    app.setApplicationVersion("0.4.1");
#ifdef Q_OS_ANDROID
    // Keep evidence if a platform window closure ends the process again.
    QObject::connect(&app, &QGuiApplication::lastWindowClosed, &app, []() {
        qInfo() << "Lem lifecycle: lastWindowClosed";
    });
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, []() {
        qInfo() << "Lem lifecycle: aboutToQuit, windows=" << QGuiApplication::allWindows().size();
    });
#endif
    applyLightTheme();
    app.setWindowIcon(QIcon(":/branding/lem-icon-256.png"));
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    app.setDesktopFileName("lem");
#endif
#ifdef Q_OS_ANDROID
    qInfo().noquote() << "Lem TLS: suffix=" << qgetenv("ANDROID_OPENSSL_SUFFIX")
                      << "backends=" << QSslSocket::availableBackends().join(',')
                      << "active=" << QSslSocket::activeBackend()
                      << "supportsSsl=" << QSslSocket::supportsSsl()
                      << "build=" << QSslSocket::sslLibraryBuildVersionString()
                      << "runtime=" << QSslSocket::sslLibraryVersionString();
#endif
    bool smoke = app.arguments().contains("--smoke-test");
#ifdef LEM_WINDOWS_STANDALONE
    // The isolated release smoke test must prove static TLS and photo plugins exist.
    if (smoke && (!QSslSocket::availableBackends().contains("schannel")
                  || !QSslSocket::supportsSsl()
                  || !QImageReader::supportedImageFormats().contains("jpeg"))) {
        qCritical() << "Standalone build is missing Schannel TLS or JPEG support.";
        return 5;
    }
#endif
    QTemporaryDir temp;
    // Screenshot/demo validation must never read or overwrite the user's drafts.
    const bool screenshot = app.arguments().contains("--screenshot");
    Lem lem(smoke || screenshot ? temp.path() : AppStorage::prepare());
    if (smoke || app.arguments().contains("--demo"))
        lem.demo();
    QQmlApplicationEngine engine;
#ifdef LEM_WINDOWS_STANDALONE
    // Do not silently fall back to the developer's installed QML tree.
    engine.setImportPathList({QStringLiteral("qrc:/qt/qml"),
                              QStringLiteral("qrc:/qt-project.org/imports")});
#endif
    if (smoke || app.arguments().contains("--screenshot"))
        engine.setInitialProperties({{"splashActive", false}});
    engine.rootContext()->setContextProperty("lem", &lem);
    engine.rootContext()->setContextProperty("qtRuntimeVersion", QString::fromLatin1(qVersion()));
    bool warnings = false;
    QObject::connect(&engine, &QQmlEngine::warnings, &app,
                     [&](const QList<QQmlError> &) { warnings = true; });
    engine.loadFromModule("Lem", "Main");
    if (engine.rootObjects().isEmpty())
        return 1;
    if (engine.rootObjects().first()->property("interfaceRevision").toString() != "bounds032-safearea1") {
        qCritical() << "Interface QML desatualizada: reconfigure e compile num diretório de build novo.";
        return 4;
    }
    if (app.arguments().contains("--screenshot") && app.arguments().contains("--preview-page"))
        engine.rootObjects().first()->setProperty(
            "page", app.arguments().value(app.arguments().indexOf("--preview-page") + 1));
    if (app.arguments().contains("--mobile-preview")) {
        auto w = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        w->setWidth(390);
        w->setHeight(844);
    }
    if (app.arguments().contains("--screenshot"))
        QTimer::singleShot(750, &app, [&]() {
            auto w = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
            bool ok = w->grabWindow().save(
                app.arguments().value(app.arguments().indexOf("--screenshot") + 1));
            app.exit(ok ? 0 : 3);
        });
    if (smoke)
        QTimer::singleShot(500, &app,
                           [&]() { app.exit(warnings || lem.sections().size() != 7 ? 2 : 0); });
    return app.exec();
}
