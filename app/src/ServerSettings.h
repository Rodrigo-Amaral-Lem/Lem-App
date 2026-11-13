#pragma once
#include <QSettings>
#include <QUrl>

// One server preference policy for Android, Windows and Linux. Keep independent
// servers intact; only unmarked prototype aliases within our DNS suffix migrate.
namespace ServerSettings {
inline QString defaultUrl() {
    return QStringLiteral("https://lem.rodrod.pt");
}
inline void save(QSettings &settings, const QString &url) {
    settings.setValue("server", url);
    settings.setValue("serverSelectionSchema", 1);
}
inline QString load(QSettings &settings) {
    QString selected = settings.value("server", defaultUrl()).toString().trimmed();
    if (selected.isEmpty())
        selected = defaultUrl();
    // Same rule as Lem::setServer: stored/migrated preferences must not make the app
    // send a password over plain HTTP (localhost stays available for development).
    const QUrl chosen(selected);
    const bool development = chosen.scheme() == "http" &&
                             (chosen.host() == "localhost" || chosen.host() == "127.0.0.1");
    if (chosen.scheme() != "https" && !development)
        selected = defaultUrl();
    if (!settings.contains("serverSelectionSchema")) {
        const QUrl current(defaultUrl()), previous(selected);
        const auto suffix = current.host().mid(current.host().indexOf('.'));
        // Earlier prototypes used another host under this project's DNS domain.
        // Bare HTTPS roots are managed aliases; custom domains/ports/paths remain.
        const bool managedAlias =
            previous.scheme() == "https" && previous.host().endsWith(suffix, Qt::CaseInsensitive) &&
            previous.host().compare(current.host(), Qt::CaseInsensitive) != 0 &&
            (previous.port() == -1 || previous.port() == 443) &&
            (previous.path().isEmpty() || previous.path() == "/") && previous.query().isEmpty() &&
            previous.fragment().isEmpty() && previous.userInfo().isEmpty();
        if (managedAlias)
            selected = defaultUrl();
        save(settings, selected);
    }
    return selected;
}
} // namespace ServerSettings
