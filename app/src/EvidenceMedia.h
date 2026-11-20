// SPDX-License-Identifier: GPL-3.0-only
// Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

#pragma once
#include <QByteArray>
#include <QBuffer>
#include <QFile>
#include <QImageReader>
#include <QtEndian>
#include <cmath>

namespace EvidenceMedia {
// QImageReader can synthesize an end marker for a truncated JPEG. Walk past
// the length-delimited segments (including Exif thumbnails) to the real EOI.
inline bool completeJpeg(QByteArrayView bytes) {
    if (!bytes.startsWith(QByteArrayView("\xff\xd8", 2))) return false;
    qsizetype offset = 2;
    bool scan = false;
    while (offset < bytes.size()) {
        if (scan) {
            offset = bytes.indexOf(char(0xff), offset);
            if (offset < 0) return false;
        }
        if (quint8(bytes[offset]) != 0xff) return false;
        while (offset < bytes.size() && quint8(bytes[offset]) == 0xff) ++offset;
        if (offset == bytes.size()) return false;
        const auto marker = quint8(bytes[offset++]);
        if (marker == 0xd9) return scan;
        if (scan && (marker == 0 || (marker >= 0xd0 && marker <= 0xd7))) continue;
        if (offset + 2 > bytes.size()) return false;
        const auto length = qFromBigEndian<quint16>(bytes.data() + offset);
        if (length < 2 || length > bytes.size() - offset) return false;
        offset += length;
        scan = marker == 0xda;
    }
    return false;
}

inline bool completePng(QByteArrayView bytes) {
    qsizetype offset = 8; // The reader has already identified the PNG signature.
    while (offset + 12 <= bytes.size()) {
        const auto length = qFromBigEndian<quint32>(bytes.data() + offset);
        if (length > quint64(bytes.size() - offset - 12)) return false;
        if (bytes.sliced(offset + 4, 4) == "IEND") return length == 0;
        offset += qsizetype(length) + 12;
    }
    return false;
}

// Older profiles may already contain a truncated original that Qt can partially decode.
inline bool readableStoredPhoto(const QString &path, const QString &mime) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > 20 * 1024 * 1024) return false;
    QByteArray bytes = file.readAll();
    QBuffer input(&bytes);
    if (!input.open(QIODevice::ReadOnly)) return false;
    QImageReader reader(&input);
    const auto size = reader.size();
    if (!size.isValid() || qint64(size.width()) * size.height() > 40000000) return false;
    if (mime == "image/jpeg" && (reader.format() != "jpeg" || !completeJpeg(bytes))) return false;
    if (mime == "image/png" && (reader.format() != "png" || !completePng(bytes))) return false;
    return !reader.read().isNull();
}

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
