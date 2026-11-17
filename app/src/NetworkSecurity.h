// SPDX-License-Identifier: GPL-3.0-only
// Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

#pragma once
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QSharedPointer>
#include <QSaveFile>
#include <QCryptographicHash>
#include <QRegularExpression>

namespace NetworkSecurity {
inline QByteArray clientPlatform() {
#if defined(Q_OS_ANDROID)
    return "android";
#elif defined(Q_OS_WIN)
    return "windows";
#elif defined(Q_OS_LINUX)
    return "linux";
#elif defined(Q_OS_MACOS)
    return "macos";
#else
    return "other";
#endif
}
inline constexpr int EvidenceConcurrency = 4;
inline void restrictRedirects(QNetworkRequest &request) {
    // A 307/308 must never replay a login password or upload to another origin.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::SameOriginRedirectPolicy);
}
struct ReplyBody {
    QByteArray data;
    qsizetype limit;
    bool exceeded = false;
    void consume(QNetworkReply *reply) {
        if (exceeded) return;
        while (reply->bytesAvailable() > 0) {
            const auto chunk = reply->read(qMin<qint64>(65536, limit - data.size() + 1));
            if (chunk.isEmpty()) break;
            if (data.size() + chunk.size() > limit) {
                exceeded = true;
                data.clear();
                if (!reply->isFinished()) reply->abort();
                return;
            }
            data += chunk;
        }
    }
};
inline QSharedPointer<ReplyBody> boundReply(QNetworkReply *reply, qsizetype limit) {
    auto body = QSharedPointer<ReplyBody>::create();
    body->limit = limit;
    reply->setReadBufferSize(256 * 1024);
    QObject::connect(reply, &QIODevice::readyRead, reply, [reply, body] { body->consume(reply); });
    return body;
}
// Stage evidence incrementally; only the report callback may publish the file.
// At most one 256 KiB chunk is materialized, independent of the evidence size.
struct FileBody {
    QSaveFile file;
    QCryptographicHash hash{QCryptographicHash::Sha256};
    QByteArray errorData;
    QByteArray prefix;
    qint64 received = 0;
    qint64 responseBytes = 0;
    qint64 requestedOffset = 0;
    bool checkedHeaders = false, invalidRange = false;
    const qint64 limit;
    bool exceeded = false, writeFailed = false;
    FileBody(const QString &path, qint64 maximum) : file(path), limit(maximum) {}
    void beginAttempt(qint64 offset) {
        requestedOffset = offset; responseBytes = 0; errorData.clear(); checkedHeaders = false;
    }
    void consume(QNetworkReply *reply) {
        if (exceeded || writeFailed || invalidRange) return;
        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (!checkedHeaders && code >= 200 && code < 300) {
            checkedHeaders = true;
            if (code == 206) {
                const auto range = QRegularExpression("^bytes (\\d+)-(\\d+)/(\\d+)$").match(QString::fromLatin1(reply->rawHeader("Content-Range")));
                invalidRange = !range.hasMatch() || range.captured(1).toLongLong() != requestedOffset ||
                    range.captured(2).toLongLong() < requestedOffset || range.captured(3).toLongLong() > limit ||
                    range.captured(2).toLongLong() >= range.captured(3).toLongLong();
                if (invalidRange) { reply->abort(); return; }
            } else if (received) {
                // Older servers (or changed validators) send 200 instead of 206.
                // Restart safely; never append a full representation to a prefix.
                if (!file.resize(0) || !file.seek(0)) { writeFailed = true; reply->abort(); return; }
                received = 0; hash.reset(); prefix.clear();
            }
        }
        while (reply->bytesAvailable() > 0) {
            const auto chunk = reply->read(256 * 1024);
            if (chunk.isEmpty()) break;
            if (code < 200 || code >= 300) {
                // Keep bounded diagnostics for authentication/version failures.
                if (code >= 400) errorData += chunk.left(qMax(0, 65536 - int(errorData.size())));
            }
            responseBytes += chunk.size();
            if (responseBytes > limit || (code >= 200 && code < 300 && received + chunk.size() > limit)) exceeded = true;
            else if (code < 200 || code >= 300) continue;
            else if ((!file.isOpen() && !file.open(QIODevice::WriteOnly)) ||
                     file.write(chunk) != chunk.size()) writeFailed = true;
            if (exceeded || writeFailed) {
                file.cancelWriting();
                if (!reply->isFinished()) reply->abort();
                return;
            }
            hash.addData(chunk);
            if (prefix.size() < 8) prefix += chunk.left(8 - prefix.size());
            received += chunk.size();
        }
    }
    bool matches(const QString &digest) const {
        return !exceeded && !writeFailed && !invalidRange &&
               (digest.isEmpty() || QString::fromLatin1(hash.result().toHex()) == digest);
    }
    bool commit() {
        return !exceeded && !writeFailed && !invalidRange &&
               (file.isOpen() || file.open(QIODevice::WriteOnly)) && file.commit();
    }
};
inline QSharedPointer<FileBody> streamReply(QNetworkReply *reply, const QString &path, qint64 limit) {
    auto body = QSharedPointer<FileBody>::create(path, limit);
    reply->setReadBufferSize(1024 * 1024);
    QObject::connect(reply, &QIODevice::readyRead, reply, [reply, body] { body->consume(reply); });
    return body;
}
inline qsizetype responseLimit(const QString &path) {
    if (path.contains(QStringLiteral("/photos/"))) return 60 * 1024 * 1024;
    if (path.endsWith(QStringLiteral("/bundle")) || path.endsWith(QStringLiteral("/pdf")))
        return 500 * 1024 * 1024;
    return 64 * 1024 * 1024;
}
}
