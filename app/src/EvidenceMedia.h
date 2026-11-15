// SPDX-License-Identifier: GPL-3.0-only
// Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

#pragma once
#include <QByteArray>
#include <QtEndian>
#include <cmath>

namespace EvidenceMedia {
// Read ISO BMFF movie duration before attachment, also while offline. Do not trust
// the filename or the camera's duration/size hints. Server validates codec/streams.
inline QString videoMime(const QByteArray &bytes) {
    if (bytes.size() < 16 || bytes.mid(4, 4) != "ftyp") return {};
    return bytes.mid(8, 3) == "3gp" || bytes.mid(8, 3) == "3g2"
        ? QStringLiteral("video/3gpp") : QStringLiteral("video/mp4");
}
inline bool durationValid(const QByteArray &b) {
    auto u32 = [&](qint64 p) { return qFromBigEndian<quint32>(b.constData() + p); };
    auto u64 = [&](qint64 p) { return qFromBigEndian<quint64>(b.constData() + p); };
    qint64 pos = 0, end = b.size();
    bool inMovie = false;
    for (int boxes = 0; boxes < 10000 && pos + 8 <= end; ++boxes) {
        quint64 length = u32(pos); qint64 header = 8;
        if (length == 1) { if (pos + 16 > end) return false; length = u64(pos + 8); header = 16; }
        if (length == 0) length = end - pos;
        if (length < quint64(header) || length > quint64(end - pos)) return false;
        const auto type = b.mid(pos + 4, 4);
        if (type == "moov" && !inMovie) { end = pos + qint64(length); pos += header; inMovie = true; continue; }
        if (type == "mvhd" && inMovie) {
            const qint64 p = pos + header;
            if (p + 20 > pos + qint64(length)) return false;
            const bool v1 = b[p] == 1;
            if (b[p] != 0 && !v1) return false;
            if (v1 && p + 32 > pos + qint64(length)) return false;
            const auto scale = u32(p + (v1 ? 20 : 12));
            const auto ticks = v1 ? u64(p + 24) : quint64(u32(p + 16));
            const double seconds = scale ? double(ticks) / scale : 0;
            return std::isfinite(seconds) && seconds > 0 && seconds <= 120.5;
        }
        pos += qint64(length);
    }
    return false;
}
}
