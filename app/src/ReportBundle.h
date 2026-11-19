// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "ReportPdf.h"
#include "ReportNaming.h"
#include "SafeStorage.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDataStream>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QSet>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QUrl>
#include <array>

namespace ReportBundle {
// Streaming ZIP32, stored entries + UTF-8 names + data descriptors (PKWARE APPNOTE).
// The app caps report evidence at 400 MiB. Reject oversized legacy archives safely.
// No dependency on a shell/archive executable, Python, private Qt APIs or network.
class Writer {
    struct Entry { QByteArray name; quint32 crc, size, offset; };
    QIODevice &output;
    QList<Entry> entries;
    quint64 position = 0;
    bool good = true;
    static quint32 crc32(quint32 crc, const QByteArray &data) {
        static const auto table = [] {
            std::array<quint32, 256> t{};
            for (quint32 i=0; i<256; ++i) {
                quint32 c=i; for (int bit=0; bit<8; ++bit) c=(c>>1)^((c&1)?0xedb88320U:0);
                t[i]=c;
            }
            return t;
        }();
        for (const auto ch : data) crc = table[(crc ^ quint8(ch)) & 255] ^ (crc >> 8);
        return crc;
    }
    bool write(const QByteArray &bytes) {
        if (!good || position + quint64(bytes.size()) >= 0xffffffffULL) return good = false;
        good = output.write(bytes) == bytes.size(); position += bytes.size(); return good;
    }
  public:
    explicit Writer(QIODevice &device) : output(device) {}
    bool add(const QString &name, QIODevice &source, const QString &expectedDigest = {}) {
        const auto encoded = name.toUtf8();
        if (encoded.isEmpty() || encoded.size() > 65535 || entries.size() >= 65535) return false;
        Entry e{encoded, 0xffffffffU, 0, quint32(position)};
        QByteArray header; QDataStream h(&header, QIODevice::WriteOnly); h.setByteOrder(QDataStream::LittleEndian);
        h << quint32(0x04034b50) << quint16(20) << quint16(0x0808) << quint16(0) << quint16(0) << quint16(33)
          << quint32(0) << quint32(0) << quint32(0) << quint16(encoded.size()) << quint16(0);
        if (!write(header) || !write(encoded)) return false;
        QCryptographicHash hash(QCryptographicHash::Sha256);
        while (!source.atEnd()) {
            const auto data = source.read(1024 * 1024);
            if (data.isEmpty() || quint64(e.size) + data.size() >= 0xffffffffULL || !write(data)) return false;
            e.crc = crc32(e.crc, data); e.size += data.size(); hash.addData(data);
        }
        if (!expectedDigest.isEmpty() && QString::fromLatin1(hash.result().toHex()) != expectedDigest) return false;
        e.crc ^= 0xffffffffU;
        QByteArray descriptor; QDataStream d(&descriptor, QIODevice::WriteOnly); d.setByteOrder(QDataStream::LittleEndian);
        d << quint32(0x08074b50) << e.crc << e.size << e.size;
        entries.append(e); return write(descriptor);
    }
    bool add(const QString &name, QByteArray bytes) {
        QBuffer source(&bytes); source.open(QIODevice::ReadOnly); return add(name, source);
    }
    bool finish() {
        const auto start = quint32(position);
        for (const auto &e : entries) {
            QByteArray header; QDataStream h(&header, QIODevice::WriteOnly); h.setByteOrder(QDataStream::LittleEndian);
            h << quint32(0x02014b50) << quint16(20) << quint16(20) << quint16(0x0808) << quint16(0)
              << quint16(0) << quint16(33) << e.crc << e.size << e.size << quint16(e.name.size())
              << quint16(0) << quint16(0) << quint16(0) << quint16(0) << quint32(0) << e.offset;
            if (!write(header) || !write(e.name)) return false;
        }
        QByteArray end; QDataStream d(&end, QIODevice::WriteOnly); d.setByteOrder(QDataStream::LittleEndian);
        d << quint32(0x06054b50) << quint16(0) << quint16(0) << quint16(entries.size()) << quint16(entries.size())
          << quint32(position - start) << start << quint16(0);
        return write(end);
    }
};

inline QJsonObject document(const QJsonObject &report) {
    QJsonObject doc;
    for (const auto key : {"id", "charger_id", "contract_id", "owner_id", "revision", "status", "started", "finished",
                           "updated", "technician", "payload", "template", "snapshot", "presentation", "correction"})
        if (report.contains(key)) doc[key] = report[key];
    doc["identifier"] = ReportNaming::identifier(report);
    doc["sync_pending"] = report["dirty"].toBool();
    const auto payload = report["payload"].toObject();
    for (const auto key : {"started", "finished"}) if (payload.contains(key)) doc[key] = payload[key];
    QJsonArray photos;
    for (const auto entry : report["photos"].toArray()) {
        const auto p = entry.toObject(); QJsonObject clean;
        for (const auto key : {"id", "checkpoint", "filename", "mime", "digest", "size"})
            if (p.contains(key)) clean[key] = p[key];
        photos.append(clean);
    }
    doc["photos"] = photos; return doc;
}

inline bool archive(const QJsonObject &report, QIODevice &output, QString *error) {
    const auto pdf = ReportPdf::render(report, error);
    if (!error->isEmpty()) return false;
    Writer zip(output);
    const auto identifier = ReportNaming::identifier(report);
    if (!zip.add(identifier + ".pdf", pdf) ||
        !zip.add(identifier + ".json", QJsonDocument(document(report)).toJson(QJsonDocument::Indented))) {
        *error = "Não foi possível gravar o ZIP."; return false;
    }
    QSet<QString> names;
    for (const auto entry : report["photos"].toArray()) {
        const auto p = entry.toObject(); const auto id = p["id"].toString(), checkpoint = p["checkpoint"].toString();
        const auto mime = p["mime"].toString();
        static const QMap<QString, QString> extensions{{"image/jpeg","jpg"}, {"image/png","png"}, {"video/mp4","mp4"}, {"video/3gpp","3gp"}};
        if (!SafeStorage::component(id) || !QRegularExpression("^[A-Za-z0-9_-]+(?:\\.[A-Za-z0-9_-]+)*$").match(checkpoint).hasMatch() ||
            !extensions.contains(mime) || names.contains(id)) {
            *error = "Identificação ou tipo de evidência inválido para ZIP."; return false;
        }
        names.insert(id);
        QFile file(QUrl(p["path"].toString()).toLocalFile());
        const auto folder = mime.startsWith("video/") ? "videos/" : "fotografias/";
        const auto name = folder + checkpoint + "/" + ReportNaming::evidenceFilename(identifier, id, extensions[mime]);
        if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 ||
            !zip.add(name, file, p["digest"].toString())) {
            *error = "Não foi possível exportar a evidência " + checkpoint + ". Verifique o ficheiro e o espaço disponível."; return false;
        }
    }
    if (!zip.finish()) { *error = "Não foi possível concluir o ZIP. Verifique o espaço disponível."; return false; }
    return true;
}

inline bool copy(QIODevice &source, QIODevice &destination) {
    while (!source.atEnd()) {
        const auto chunk = source.read(1024 * 1024);
        if (chunk.isEmpty() || destination.write(chunk) != chunk.size()) return false;
    }
    return true;
}

inline bool write(const QJsonObject &report, const QString &destination, QString *error) {
    *error = ReportPdf::validate(report); if (!error->isEmpty()) return false;
    if (destination.startsWith("content:")) {
        QTemporaryFile staged;
        if (!staged.open() || !archive(report, staged, error) || !staged.flush() || !staged.seek(0)) {
            if (error->isEmpty()) *error = "Não foi possível preparar o ZIP local."; return false;
        }
        QFile file(destination);
        if (!file.open(QIODevice::WriteOnly) || !copy(staged, file) || !file.flush()) {
            *error = "Não foi possível gravar o ZIP no documento selecionado."; return false;
        }
        return true;
    }
    QSaveFile file(destination);
    if (!file.open(QIODevice::WriteOnly) || !archive(report, file, error) || !file.commit()) {
        if (error->isEmpty()) *error = "Não foi possível guardar o ZIP."; return false;
    }
    return true;
}
} // namespace ReportBundle
