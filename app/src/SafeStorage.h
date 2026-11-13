#pragma once
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QString>

namespace SafeStorage {
inline bool component(const QString &value) {
    static const QRegularExpression allowed(QRegularExpression::anchoredPattern(
        QStringLiteral("[A-Za-z0-9_-]{1,128}")));
    return allowed.match(value).hasMatch();
}
inline QString userDirectory(const QString &id) {
    // Invalid remote identities cannot escape the per-server cache directory.
    return component(id) ? id : QStringLiteral("id_") + QString::fromLatin1(
        QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha256).toHex());
}
inline QString photoPath(const QString &cache, const QString &id, const QString &mime) {
    if (!component(id))
        return {};
    return cache + QStringLiteral("/photos/") + id +
           (mime == QStringLiteral("video/mp4") ? QStringLiteral(".mp4") :
            mime == QStringLiteral("video/3gpp") ? QStringLiteral(".3gp") :
            mime == QStringLiteral("image/png") ? QStringLiteral(".png") : QStringLiteral(".jpg"));
}
}
