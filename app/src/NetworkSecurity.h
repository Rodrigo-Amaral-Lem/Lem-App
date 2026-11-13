#pragma once
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QSharedPointer>

namespace NetworkSecurity {
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
inline qsizetype responseLimit(const QString &path) {
    if (path.contains(QStringLiteral("/photos/"))) return 20 * 1024 * 1024;
    if (path.endsWith(QStringLiteral("/bundle")) || path.endsWith(QStringLiteral("/pdf")))
        return 350 * 1024 * 1024;
    return 64 * 1024 * 1024;
}
}
