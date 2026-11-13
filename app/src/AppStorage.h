// Startup-only storage migration helpers. Copy old data rather than moving it.
// The migration marker supports retrying an interrupted copy; original data remains
// available as a fallback. Rewrite private file URLs in BOTH cache and edit journals.
// See docs/build-and-release.md before changing compatibility keys.

#pragma once
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QSet>
#include <QCoreApplication>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

namespace AppStorage {
inline bool hasData(const QString &root) {
    if (root.isEmpty())
        return false;
    if (QFile::exists(root + "/last-user.json"))
        return true;
    QDir dir(root);
    for (const auto &scope : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        for (const auto &user :
             QDir(root + '/' + scope).entryList(QDir::Dirs | QDir::NoDotAndDotDot))
            if (QFile::exists(root + '/' + scope + '/' + user + "/cache.json"))
                return true;
    return false;
}
inline bool copyTree(const QString &source, const QString &target) {
    if (!QDir().mkpath(target))
        return false;
    for (const auto &entry : QDir(source).entryInfoList(QDir::Files | QDir::Dirs |
                                                        QDir::NoDotAndDotDot | QDir::Hidden)) {
        if (entry.isSymLink())
            continue;
        const auto destination = target + '/' + entry.fileName();
        if (entry.isDir()) {
            if (!copyTree(entry.absoluteFilePath(), destination))
                return false;
        } else {
            QFile input(entry.absoluteFilePath());
            QSaveFile output(destination);
            if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly))
                return false;
            while (!input.atEnd()) {
                const auto chunk = input.read(1024 * 1024);
                if (chunk.isEmpty() && input.error() != QFile::NoError)
                    return false;
                if (output.write(chunk) != chunk.size())
                    return false;
            }
            if (!output.commit())
                return false;
        }
    }
    return true;
}
inline QJsonValue relocateValue(const QJsonValue &value, const QString &source,
                                const QString &destination) {
    if (value.isObject()) {
        auto object = value.toObject();
        for (auto it = object.begin(); it != object.end(); ++it)
            it.value() = relocateValue(it.value(), source, destination);
        return object;
    }
    if (value.isArray()) {
        auto array = value.toArray();
        for (int i = 0; i < array.size(); ++i)
            array[i] = relocateValue(array[i], source, destination);
        return array;
    }
    if (value.isString()) {
        const QUrl url(value.toString());
        if (url.isLocalFile() && url.toLocalFile().startsWith(source + '/'))
            return QUrl::fromLocalFile(destination + url.toLocalFile().mid(source.size()))
                .toString();
    }
    return value;
}
inline bool relocateCaches(const QString &root, const QString &source, const QString &destination) {
    for (const auto &entry :
         QDir(root).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (entry.isSymLink())
            continue;
        if (entry.isDir()) {
            if (!relocateCaches(entry.absoluteFilePath(), source, destination))
                return false;
        } else if (entry.fileName() == "cache.json" ||
                   (QFileInfo(root).fileName() == "edits" && entry.suffix() == "json")) {
            QFile input(entry.absoluteFilePath());
            if (!input.open(QIODevice::ReadOnly))
                return false;
            QJsonParseError error;
            const auto doc = QJsonDocument::fromJson(input.readAll(), &error);
            input.close();
            if (error.error != QJsonParseError::NoError || !doc.isObject())
                return false;
            QSaveFile output(entry.absoluteFilePath());
            if (!output.open(QIODevice::WriteOnly))
                return false;
            const auto bytes =
                QJsonDocument(relocateValue(doc.object(), source, destination).toObject()).toJson();
            if (output.write(bytes) != bytes.size() || !output.commit())
                return false;
        }
    }
    return true;
}
// Discover older sibling storage by its data format, not an old product name.
// Keep the original files; a failed copy uses the original directory this session.
inline QString resolve(const QString &destination, const QString &legacySource = {}) {
    const auto marker = destination + "/.storage-migration.json";
    QString source;
    QFile pending(marker);
    if (pending.open(QIODevice::ReadOnly))
        source = QJsonDocument::fromJson(pending.readAll()).object()["source"].toString();
    pending.close();
    if (source.isEmpty()) {
        if (hasData(destination))
            return destination;
        const auto parent = QFileInfo(destination).dir();
        QStringList candidates;
        for (const auto &entry : parent.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
            if (!entry.isSymLink() &&
                entry.absoluteFilePath() != QFileInfo(destination).absoluteFilePath() &&
                hasData(entry.absoluteFilePath()))
                candidates << entry.absoluteFilePath();
        if (hasData(legacySource) &&
            QFileInfo(legacySource).absoluteFilePath() != QFileInfo(destination).absoluteFilePath())
            source = legacySource;
        else {
            if (candidates.size() != 1)
                return destination;
            source = candidates.first();
        }
        if (!QDir().mkpath(destination))
            return source;
        QSaveFile record(marker);
        if (!record.open(QIODevice::WriteOnly))
            return source;
        record.write(QJsonDocument(QJsonObject{{"source", source}}).toJson());
        if (!record.commit())
            return source;
    }
    if (!hasData(source))
        return destination;
    if (!copyTree(source, destination) || !relocateCaches(destination, source, destination))
        return source;
    QFile::remove(marker);
    return destination;
}
inline QString prepare() {
    const auto destination = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    // Compatibility keys: retain the former identity only for locating existing data.
    const auto organization = QCoreApplication::organizationName();
    const auto application = QCoreApplication::applicationName();
    QCoreApplication::setOrganizationName("LEM");
    QCoreApplication::setApplicationName("LEM");
    const auto legacyRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QCoreApplication::setOrganizationName(organization);
    QCoreApplication::setApplicationName(application);
    auto root = resolve(destination, legacyRoot);
    QSettings settings;
    if (!settings.contains("server")) {
        QFile session(root + "/last-user.json");
        if (session.open(QIODevice::ReadOnly)) {
            const auto server =
                QJsonDocument::fromJson(session.readAll()).object()["server"].toString();
            if (!server.isEmpty())
                settings.setValue("server", server);
        }
    }
    if (!settings.contains("server")) {
        QSettings legacySettings(QSettings::NativeFormat, QSettings::UserScope, "LEM", "LEM");
        const auto server = legacySettings.value("server").toString();
        if (!server.isEmpty())
            settings.setValue("server", server);
    }
    if (!settings.contains("server")) {
        QSet<QString> servers;
        const auto organization = QCoreApplication::organizationName();
        const auto application = QCoreApplication::applicationName();
        QSettings groupSettings(QSettings::NativeFormat, QSettings::UserScope, organization,
                                QString());
        for (const auto &group : groupSettings.childGroups()) {
            if (group == application)
                continue;
            groupSettings.beginGroup(group);
            const auto value = groupSettings.value("server").toString();
            groupSettings.endGroup();
            if (!value.isEmpty())
                servers.insert(value);
        }
        for (const auto &base :
             QStandardPaths::standardLocations(QStandardPaths::GenericConfigLocation)) {
            QDir directory(base + '/' + organization);
            for (const auto &file : directory.entryInfoList({"*.conf", "*.ini"}, QDir::Files)) {
                if (file.completeBaseName() == application)
                    continue;
                QSettings oldSettings(file.absoluteFilePath(), QSettings::IniFormat);
                const auto value = oldSettings.value("server").toString();
                if (!value.isEmpty())
                    servers.insert(value);
            }
        }
        if (servers.size() == 1)
            settings.setValue("server", *servers.constBegin());
    }
    return root;
}
} // namespace AppStorage
