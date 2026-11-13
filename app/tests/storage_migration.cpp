// SPDX-License-Identifier: GPL-3.0-only
// Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

#include "AppStorage.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDebug>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            qCritical() << "Failed:" << #x;                                                        \
            return 1;                                                                              \
        }                                                                                          \
    } while (false)
static void write(const QString &path, const QByteArray &data) {
    QDir().mkpath(QFileInfo(path).dir().path());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size())
        qFatal("fixture write failed");
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    CHECK(temp.isValid());
    const auto old = temp.path() + "/Former Product", target = temp.path() + "/Lem";
    write(old + "/last-user.json", R"({"server":"https://example.test","user":{"id":"tech"}})");
    const auto photo = old + "/scope/tech/photos/evidence.jpg";
    write(photo, "jpeg-fixture");
    write(old + "/scope/tech/edits/report.json",
          QJsonDocument(
              QJsonObject{{"id", "r1"},
                          {"photos", QJsonArray{QJsonObject{
                                         {"path", QUrl::fromLocalFile(photo).toString()}}}}})
              .toJson());
    write(old + "/scope/tech/cache.json",
          QJsonDocument(
              QJsonObject{
                  {"reports",
                   QJsonObject{{"r1", QJsonObject{{"notes", "keep"},
                                                  {"photos", QJsonArray{QJsonObject{
                                                                 {"path", QUrl::fromLocalFile(photo)
                                                                              .toString()}}}}}}}}})
              .toJson());
    CHECK(AppStorage::resolve(target) == target);
    CHECK(QFile::exists(old + "/last-user.json"));
    CHECK(QFile::exists(target + "/scope/tech/photos/evidence.jpg"));
    QFile f(target + "/scope/tech/cache.json");
    CHECK(f.open(QIODevice::ReadOnly));
    const auto data = f.readAll();
    f.close();
    CHECK(data.contains("keep"));
    CHECK(data.contains(
        QUrl::fromLocalFile(target + "/scope/tech/photos/evidence.jpg").toString().toUtf8()));
    CHECK(!data.contains(QUrl::fromLocalFile(photo).toString().toUtf8()));
    QFile journal(target + "/scope/tech/edits/report.json");
    CHECK(journal.open(QIODevice::ReadOnly));
    const auto journalData = journal.readAll();
    CHECK(journalData.contains(
        QUrl::fromLocalFile(target + "/scope/tech/photos/evidence.jpg").toString().toUtf8()));
    CHECK(!journalData.contains(QUrl::fromLocalFile(photo).toString().toUtf8()));
    write(old + "/last-user.json", "older-data");
    CHECK(AppStorage::resolve(target) == target);
    QFile current(target + "/last-user.json");
    CHECK(current.open(QIODevice::ReadOnly));
    CHECK(current.readAll().contains("https://example.test"));
    // Ambiguous sibling profiles must never be combined or silently selected.
    const auto other = temp.path() + "/Other Product";
    write(other + "/last-user.json", "{}");
    const auto empty = temp.path() + "/Fresh";
    CHECK(AppStorage::resolve(empty) == empty);
    CHECK(!QFile::exists(empty + "/last-user.json"));
    // Interrupted migrations retry from the source and update stale copied data.
    QTemporaryDir recovery;
    const auto source = recovery.path() + "/Former", dest = recovery.path() + "/Lem";
    write(source + "/last-user.json", "fresh");
    write(source + "/s/u/cache.json", "{}");
    write(dest + "/last-user.json", "stale");
    write(dest + "/.storage-migration.json",
          QJsonDocument(QJsonObject{{"source", source}}).toJson());
    CHECK(AppStorage::resolve(dest) == dest);
    QFile check(dest + "/last-user.json");
    CHECK(check.open(QIODevice::ReadOnly));
    CHECK(check.readAll() == "fresh");
    CHECK(!QFile::exists(dest + "/.storage-migration.json"));
    // Explicit former location is found even when the organization directory changes.
    QTemporaryDir cross;
    const auto legacy = cross.path() + "/LegacyOrg/Former", fresh = cross.path() + "/Lem/Lem";
    write(legacy + "/last-user.json", R"({"server":"https://example.test"})");
    write(legacy + "/s/u/cache.json", "{}");
    CHECK(AppStorage::resolve(fresh, legacy) == fresh);
    CHECK(QFile::exists(fresh + "/last-user.json"));
    CHECK(QFile::exists(legacy + "/last-user.json"));
    write(legacy + "/last-user.json", "older");
    CHECK(AppStorage::resolve(fresh, legacy) == fresh);
    QFile freshFile(fresh + "/last-user.json");
    CHECK(freshFile.open(QIODevice::ReadOnly));
    CHECK(freshFile.readAll().contains("example.test"));
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    // Exercise the real organization/application rename using isolated XDG locations.
    QTemporaryDir profile;
    qputenv("XDG_DATA_HOME", (profile.path() + "/data").toUtf8());
    qputenv("XDG_CONFIG_HOME", (profile.path() + "/config").toUtf8());
    QCoreApplication::setOrganizationName("LEM");
    QCoreApplication::setApplicationName("LEM");
    const auto formerRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    write(formerRoot + "/last-user.json",
          R"({"server":"https://custom.example.test","user":{"id":"tech"}})");
    write(formerRoot + "/s/tech/cache.json", "{}");
    {
        QSettings previous;
        previous.setValue("server", "https://custom.example.test");
        previous.sync();
    }
    QCoreApplication::setOrganizationName("Lem");
    QCoreApplication::setApplicationName("Lem");
    const auto expected = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    CHECK(AppStorage::prepare() == expected);
    CHECK(QCoreApplication::organizationName() == "Lem" &&
          QCoreApplication::applicationName() == "Lem");
    CHECK(QFile::exists(expected + "/last-user.json"));
    CHECK(QFile::exists(formerRoot + "/last-user.json"));
    CHECK(QSettings().value("server").toString() == "https://custom.example.test");
#endif
    qInfo() << "Storage migration passed";
    return 0;
}
