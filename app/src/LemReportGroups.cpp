// SPDX-License-Identifier: GPL-3.0-only
// Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

// Local-first QVE/measurement reuse and drawn/imported signatures. Evidence is copied
// under fresh IDs; source reports and their files remain immutable.
#include "Lem.h"
#include "SafeStorage.h"
#include "Measurements.h"
#include <QBuffer>
#include <QDateTime>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QJsonDocument>
#include <QSaveFile>
#include <QUuid>

namespace {
QString freshId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
bool inScope(const QString &scope, const QString &id) {
    return scope == "QVE" ? id.startsWith("QVE.") :
        scope == "MAN" && QRegularExpression("^MAN\\.(0[4-9]|10)$").match(id).hasMatch();
}
bool hasGroup(QJsonObject report, const QString &scope) {
    for (auto entry : report["template"].toObject()["sections"].toArray())
        if (entry.toObject()["id"] == scope) return true;
    return false;
}
}

// Update cached catalogue projections, never the frozen snapshots of other reports.
void Lem::applyClosedIdentity(const QJsonObject &report) {
    if (report["status"] != "closed") return;
    const auto snapshot = report["snapshot"].toObject()["charger"].toObject();
    const auto authoritative = report["station_update"].toObject();
    if (!authoritative["updated"].toString().isEmpty()) {
        applyStationRecords(QJsonArray{QJsonObject{{"id", authoritative["station_id"]},
            {"external_id", authoritative["external_id"]}, {"details", authoritative["details"]},
            {"updated", authoritative["updated"]}}});
        return;
    }
    // Explicitly corrected legacy reports can receive a canonical server update.
    if (authoritative.isEmpty() && report["template"].toObject()["version"].toInt() < 4) return;
    const auto answers = report["payload"].toObject()["answers"].toObject();
    const QMap<QString, QString> fields{{"EST.02", "manufacturer"}, {"EST.03", "model"}, {"EST.04", "serial"}};
    QJsonObject changes;
    for (auto it = fields.begin(); it != fields.end(); ++it) {
        const auto value = answers[it.key()].toObject()["value"].toString().trimmed();
        if (!authoritative.isEmpty()) {
            const auto details = authoritative["details"].toObject();
            if (details.contains(it.value())) changes[it.value()] = details[it.value()];
        } else if (!value.isEmpty() && !Measurements::notApplicable(value) && value != snapshot[it.value()].toString().trimmed())
            changes[it.value()] = value;
    }
    if (changes.isEmpty()) return;
    const auto stationId = authoritative.isEmpty() ? snapshot["station_id"].toString() : authoritative["station_id"].toString();
    const auto externalId = authoritative.isEmpty() ? snapshot["external_id"].toString() : authoritative["external_id"].toString();
    auto patch = [&](QJsonArray rows, const QString &stationKey) {
        for (int i = 0; i < rows.size(); ++i) {
            auto row = rows[i].toObject();
            const bool same = !stationId.isEmpty() && !row[stationKey].toString().isEmpty()
                ? row[stationKey].toString() == stationId : !externalId.isEmpty() && row["external_id"].toString() == externalId;
            if (!same) continue;
            auto details = row["details"].toObject();
            for (auto it = changes.begin(); it != changes.end(); ++it) details[it.key()] = it.value();
            row["details"] = details; rows[i] = row;
        }
        return rows;
    };
    m_stations = patch(m_stations, "id");
    m_chargers = patch(m_chargers, "station_id");
    for (auto key : m_catalogues.keys()) m_catalogues[key] = patch(m_catalogues[key].toArray(), "station_id");
}

QVariantList Lem::qveCopySources() const { return groupCopySources("QVE"); }
QVariantList Lem::measurementCopySources() const { return groupCopySources("MAN"); }
bool Lem::copyLocksCheckpoint(const QString &reportId, const QString &checkpoint) const {
    return reportId == m_qveCopyTarget && inScope(m_copyScope, checkpoint);
}
QVariantList Lem::groupCopySources(QString scope) const {
    QVariantList result;
    for (auto row : chargers()) {
        const auto charger = row.toMap();
        if (charger["id"].toString() == m_current["charger_id"].toString()) continue;
        for (auto entry : charger["reports"].toList()) {
            const auto summary = entry.toMap();
            const auto id = summary["id"].toString();
            const auto cached = m_reports[id].toObject();
            if (m_deletedReports.contains(id) || cached["_conflict"].toBool() ||
                (!cached.isEmpty() && !hasGroup(cached, scope))) continue;
            result.append(QVariantMap{{"id", id}, {"charger_id", charger["id"]},
                {"label", charger["external_id"].toString() + " · " +
                    (summary["status"].toString() == "closed" ? "Concluído" : "Rascunho") +
                    " · " + summary["started"].toString().left(16).replace('T', ' ') +
                    " · " + summary["identifier"].toString()}});
        }
    }
    return result;
}

void Lem::copyQveFrom(QString sourceId) { copyReportGroup(sourceId, "QVE"); }
void Lem::copyMeasurementsFrom(QString sourceId) { copyReportGroup(sourceId, "MAN"); }
void Lem::copyReportGroup(QString sourceId, QString scope) {
    if (!m_qveCopyTarget.isEmpty()) {
        if (scope == m_copyScope || !m_pendingCopySource.isEmpty()) { status("Já existe uma cópia deste grupo em curso ou em espera."); return; }
        m_pendingCopySource = sourceId; m_pendingCopyScope = scope; m_pendingCopyTarget = m_current["id"].toString();
        status("Cópia adicionada à fila. Será iniciada quando terminar a atual."); return;
    }
    if (reportActionBlocked() || m_catalogueLoading || m_current["status"] != "draft" ||
        (!correctionAllowed(m_current) || m_current["owner_id"] != m_user["id"]) || m_current["_conflict"].toBool() ||
        !hasGroup(m_current, scope) || !flushEdits()) return;
    bool offered = false;
    for (auto v : groupCopySources(scope))
        if (v.toMap()["id"].toString() == sourceId) offered = true;
    if (!offered) { status("Escolha um report de outro carregador deste contrato."); return; }
    m_copyScope = scope;
    m_qveCopyTarget = m_current["id"].toString();
    m_autoSync.stop();
    m_localOperationTitle = scope == "QVE" ? "A copiar QVE e evidências" : "A copiar medições e evidências";
    status(m_localOperationTitle);
    // A downloaded report is the source of truth for this explicit local copy.
    const auto cached = m_reports[sourceId].toObject();
    if (!cached.isEmpty()) {
        beginQveCopy(cached); return;
    }
    if (m_token.isEmpty()) { failQveCopy("Abra previamente o report de origem para o usar offline."); return; }
    request("GET", "/api/reports/" + sourceId, {}, [this](QByteArray bytes) {
        beginQveCopy(QJsonDocument::fromJson(bytes).object());
    }, "application/json", [this](int, QString) { failQveCopy("Não foi possível ler o report de origem."); });
}

void Lem::beginQveCopy(QJsonObject source) {
    bool sameContract = false;
    for (auto row : m_chargers)
        if (row.toObject()["id"] == source["charger_id"]) sameContract = true;
    if (!sameContract || !hasGroup(source, m_copyScope) || source["_conflict"].toBool() ||
        source["charger_id"] == m_current["charger_id"]) {
        failQveCopy("A origem deve ser outro carregador do mesmo contrato, com o grupo solicitado."); return;
    }
    m_qveCopySource = source;
    m_qveCopyPhotos = {};
    const auto cachedPhotos = m_reports[source["id"].toString()].toObject()["photos"].toArray();
    QSet<QString> allowed;
    for (auto section : m_current["template"].toObject()["sections"].toArray())
        if (section.toObject()["id"] == m_copyScope)
            for (auto item : section.toObject()["items"].toArray())
                if (inScope(m_copyScope, item.toObject()["id"].toString())) allowed.insert(item.toObject()["id"].toString());
    qint64 size = 0;
    int count = 0;
    for (auto entry : m_current["photos"].toArray()) {
        const auto p = entry.toObject();
        if (!inScope(m_copyScope, p["checkpoint"].toString())) { ++count; size += qint64(p["size"].toDouble()); }
    }
    for (auto entry : source["photos"].toArray()) {
        auto photo = entry.toObject();
        if (!allowed.contains(photo["checkpoint"].toString())) continue;
        if (!SafeStorage::component(photo["id"].toString())) { failQveCopy("Identificador de evidência inválido."); return; }
        for (auto old : cachedPhotos)
            if (old.toObject()["id"] == photo["id"]) photo["path"] = old.toObject()["path"];
        m_qveCopyPhotos.append(photo);
        ++count; size += qint64(photo["size"].toDouble());
    }
    if (count > 200 || size > 400 * 1024 * 1024) { failQveCopy("A cópia ultrapassa o limite de 200 evidências ou 400 MiB."); return; }
    copyNextQvePhoto(0);
}

void Lem::copyNextQvePhoto(int index) {
    if (index == m_qveCopyPhotos.size()) { commitQveCopy(); return; }
    auto photo = m_qveCopyPhotos[index].toObject();
    QString path = localPath(QUrl(photo["path"].toString()));
    if (!QFileInfo(path).isFile()) path = SafeStorage::photoPath(cacheDir(), photo["id"].toString(), photo["mime"].toString());
    QFile file(path);
    if (file.open(QIODevice::ReadOnly)) {
        auto bytes = file.read(60 * 1024 * 1024 + 1);
        stageQvePhoto(index, bytes); return;
    }
    if (m_token.isEmpty() || m_server.startsWith("demo:")) {
        failQveCopy("Falta uma evidência da origem no dispositivo. Ligue-se ao servidor ou descarregue as evidências da origem."); return;
    }
    request("GET", "/api/reports/" + m_qveCopySource["id"].toString() + "/photos/" + photo["id"].toString(), {},
        [this, index](QByteArray bytes) { stageQvePhoto(index, bytes); }, "application/octet-stream",
        [this](int, QString) { failQveCopy("Falhou o download de uma evidência da origem. O grupo atual foi conservado."); });
}

void Lem::stageQvePhoto(int index, const QByteArray &bytes) {
    auto photo = m_qveCopyPhotos[index].toObject();
    const auto limit = (photo["mime"].toString().startsWith("video/") ? 60 : 20) * 1024 * 1024;
    if (bytes.isEmpty() || bytes.size() > limit) { failQveCopy("Evidência inválida: máximo 20 MiB por fotografia ou 60 MiB por vídeo."); return; }
    const auto digest = photo["digest"].toString();
    if (!digest.isEmpty() && QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()) != digest) {
        failQveCopy("A evidência da origem está incompleta ou danificada. Descarregue novamente a origem."); return;
    }
    const auto id = freshId();
    const auto path = SafeStorage::photoPath(cacheDir(), id, photo["mime"].toString());
    if (path.isEmpty()) { failQveCopy("Formato de evidência da origem inválido."); return; }
    QDir().mkpath(cacheDir() + "/photos");
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        failQveCopy("Não foi possível guardar a evidência da origem. O grupo atual foi conservado."); return;
    }
    m_qveCopyFiles.append(path);
    photo["id"] = id; photo["path"] = QUrl::fromLocalFile(path).toString();
    photo["filename"] = QFileInfo(path).fileName(); photo["size"] = double(bytes.size());
    photo["uploaded"] = false; photo.remove("digest");
    m_qveCopyPhotos[index] = photo;
    status(QString("A copiar %1: evidência %2 / %3").arg(m_copyScope == "QVE" ? "QVE" : "medições").arg(index + 1).arg(m_qveCopyPhotos.size()));
    // Keep the event loop responsive even for many already cached files.
    QTimer::singleShot(0, this, [this, index] { copyNextQvePhoto(index + 1); });
}

void Lem::commitQveCopy() {
    if (m_current["id"].toString() != m_qveCopyTarget || m_current["status"] != "draft" ||
        (!correctionAllowed(m_current) || m_current["owner_id"] != m_user["id"]) || m_current["_conflict"].toBool()) {
        failQveCopy("O report de destino deixou de estar disponível para edição."); return;
    }
    auto report = m_current;
    auto payload = report["payload"].toObject();
    auto answers = payload["answers"].toObject();
    for (auto key : answers.keys()) if (inScope(m_copyScope, key)) answers.remove(key);
    const auto sourceAnswers = m_qveCopySource["payload"].toObject()["answers"].toObject();
    for (auto section : report["template"].toObject()["sections"].toArray())
        if (section.toObject()["id"] == m_copyScope)
            for (auto item : section.toObject()["items"].toArray()) {
                const auto key = item.toObject()["id"].toString();
                if (inScope(m_copyScope, key) && sourceAnswers.contains(key)) answers[key] = sourceAnswers[key];
            }
    payload["answers"] = answers;
    payload[m_copyScope == "QVE" ? "qve_copy" : "measurement_copy"] = QJsonObject{{"source_report_id", m_qveCopySource["id"]},
        {"source_charger_id", m_qveCopySource["charger_id"]},
        {"source_station_id", m_qveCopySource["snapshot"].toObject()["charger"].toObject()["external_id"]},
        {"copied_by", m_user["name"]}, {"copied_at", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
    auto deleted = report["deleted_photos"].toArray();
    QJsonArray photos;
    qint64 total = 0;
    for (auto entry : report["photos"].toArray()) {
        const auto p = entry.toObject();
        if (inScope(m_copyScope, p["checkpoint"].toString())) {
            if (!deleted.contains(p["id"])) deleted.append(p["id"]);
        } else photos.append(p);
    }
    for (auto entry : m_qveCopyPhotos) photos.append(entry);
    for (auto entry : photos) total += qint64(entry.toObject()["size"].toDouble());
    if (total > 400 * 1024 * 1024 || photos.size() > 200) { failQveCopy("A cópia ultrapassa os limites de evidências do report."); return; }
    report["payload"] = payload; report["photos"] = photos; report["deleted_photos"] = deleted;
    report["dirty"] = true; report["_defer_photo_sync"] = true;
    report["_edit_seq"] = report["_edit_seq"].toDouble() + 1;
    if (!saveEdits(report)) { failQveCopy("Não foi possível guardar a cópia do grupo. O grupo atual foi conservado."); return; }
    for (const auto &path : m_qveCopyFiles) m_photoBytes += QFileInfo(path).size();
    for (auto entry : m_current["photos"].toArray()) {
        const auto oldPhoto = entry.toObject();
        if (!inScope(m_copyScope, oldPhoto["checkpoint"].toString())) continue;
        const auto oldPath = SafeStorage::photoPath(cacheDir(), oldPhoto["id"].toString(), oldPhoto["mime"].toString());
        const QFileInfo f(oldPath);
        if (f.canonicalPath() == QFileInfo(cacheDir() + "/photos").canonicalFilePath()) {
            const auto size = f.size();
            if (QFile::remove(oldPath)) m_photoBytes = qMax(qint64(0), m_photoBytes - size);
        }
    }
    m_current = report; m_reports[m_qveCopyTarget] = report;
    m_qveCopyFiles.clear(); m_qveCopyPhotos = {}; m_qveCopySource = {}; m_qveCopyTarget.clear(); m_copyScope.clear();
    m_localOperationTitle.clear();
    status("Cópia concluída. Confirme que as verificações e medições se aplicam a este carregador.");
    if (!m_pendingCopySource.isEmpty()) QTimer::singleShot(0, this, &Lem::startPendingGroupCopy);
    else scheduleSync();
}

void Lem::failQveCopy(QString message) {
    for (const auto &path : m_qveCopyFiles) QFile::remove(path);
    m_qveCopyFiles.clear(); m_qveCopyPhotos = {}; m_qveCopySource = {}; m_qveCopyTarget.clear(); m_copyScope.clear();
    m_localOperationTitle.clear(); status(message);
    if (!m_pendingCopySource.isEmpty()) QTimer::singleShot(0, this, &Lem::startPendingGroupCopy);
    else scheduleSync();
}

bool Lem::importSignature(QString checkpoint, QUrl url) {
#ifdef Q_OS_ANDROID
    Q_UNUSED(checkpoint); Q_UNUSED(url); return false;
#else
    if (m_busy || attachingFiles() || copyLocksCheckpoint(m_current["id"].toString(), checkpoint) || m_current["status"] != "draft" ||
        (!correctionAllowed(m_current) || m_current["owner_id"] != m_user["id"]) || m_current["_conflict"].toBool()) return false;
    if (!url.isLocalFile()) { status("Escolha uma fotografia local da assinatura."); return false; }
    QFile file(url.toLocalFile());
    if (!file.open(QIODevice::ReadOnly) || file.size() > 20 * 1024 * 1024) {
        status("Fotografia inválida ou superior a 20 MB."); return false;
    }
    QImageReader reader(&file); reader.setAutoTransform(true);
    const auto size = reader.size();
    if (!size.isValid() || qint64(size.width()) * size.height() > 64000000) {
        status("Fotografia da assinatura inválida ou demasiado grande."); return false;
    }
    reader.setScaledSize(size.scaled(700, 700, Qt::KeepAspectRatio));
    auto image = reader.read();
    if (image.isNull()) { status("Não foi possível ler a fotografia da assinatura."); return false; }
    image = image.scaled(700, 700, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QByteArray png; QBuffer output(&png); output.open(QIODevice::WriteOnly);
    if (!image.save(&output, "PNG")) { status("Não foi possível preparar a assinatura."); return false; }
    return saveSignature(checkpoint, "data:image/png;base64," + QString::fromLatin1(png.toBase64()));
#endif
}

bool Lem::saveSignature(QString checkpoint, QString dataUrl) {
    // Signing is a journaled local edit, just like attaching evidence. An upload
    // acknowledgement merges into the latest edit sequence and cannot erase it.
    if (m_busy || attachingFiles() || copyLocksCheckpoint(m_current["id"].toString(), checkpoint) ||
        m_current["status"] != "draft" || m_current["_conflict"].toBool() ||
        (!correctionAllowed(m_current) || m_current["owner_id"] != m_user["id"]) || !flushEdits()) return false;
    bool supported = false;
    for (auto section : m_current["template"].toObject()["sections"].toArray())
        for (auto item : section.toObject()["items"].toArray())
            if (item.toObject()["id"] == checkpoint && item.toObject()["supports_signature"].toBool()) supported = true;
    auto report = m_current;
    auto payload = report["payload"].toObject();
    const auto signer = payload["answers"].toObject()[checkpoint].toObject()["value"].toString().trimmed();
    if (!supported || signer.isEmpty() || dataUrl.size() > 3 * 1024 * 1024 || !dataUrl.startsWith("data:image/png;base64,")) {
        status("Indique o nome do técnico antes de assinar."); return false;
    }
    const auto bytes = QByteArray::fromBase64(dataUrl.mid(22).toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    QBuffer input; input.setData(bytes); input.open(QIODevice::ReadOnly);
    QImageReader reader(&input, "PNG"); const auto size = reader.size();
    if (!size.isValid() || qint64(size.width()) * size.height() > 2000000) {
        status("Assinatura inválida."); return false;
    }
    const auto image = reader.read().convertToFormat(QImage::Format_RGB32);
    int ink = 0;
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x) if (qGray(image.pixel(x, y)) < 180) ++ink;
    if (ink < 30) { status("Desenhe a assinatura antes de guardar."); return false; }
    const auto id = freshId(); const auto path = SafeStorage::photoPath(cacheDir(), id, "image/png");
    const QJsonValue old = payload["signatures"].toObject()[checkpoint].toObject()["photo_id"];
    QJsonArray photos; auto deleted = report["deleted_photos"].toArray(); qint64 total = bytes.size();
    for (auto entry : report["photos"].toArray()) {
        const auto p = entry.toObject();
        if (p["id"] == old) { if (!deleted.contains(old)) deleted.append(old); }
        else { photos.append(p); total += qint64(p["size"].toDouble()); }
    }
    if (photos.size() >= 200 || total > 400 * 1024 * 1024) { status("Limite de evidências do report atingido."); return false; }
    QDir().mkpath(cacheDir() + "/photos"); QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        status("Não foi possível guardar a assinatura."); return false;
    }
    photos.append(QJsonObject{{"id", id}, {"checkpoint", checkpoint}, {"filename", id + ".png"},
        {"mime", "image/png"}, {"path", QUrl::fromLocalFile(path).toString()}, {"size", double(bytes.size())}, {"uploaded", false}});
    auto signatures = payload["signatures"].toObject();
    signatures[checkpoint] = QJsonObject{{"photo_id", id}, {"signed_by", signer},
        {"signed_at", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
    payload["signatures"] = signatures; report["payload"] = payload;
    report["photos"] = photos; report["deleted_photos"] = deleted;
    report["dirty"] = true; report["_defer_photo_sync"] = true;
    report["_edit_seq"] = report["_edit_seq"].toDouble() + 1;
    if (!saveEdits(report)) { QFile::remove(path); return false; }
    if (!old.toString().isEmpty()) {
        const auto oldPath = SafeStorage::photoPath(cacheDir(), old.toString(), "image/png");
        const auto oldSize = QFileInfo(oldPath).size();
        if (QFile::remove(oldPath)) m_photoBytes = qMax(qint64(0), m_photoBytes - oldSize);
    }
    m_current = report; m_reports[report["id"].toString()] = report; m_photoBytes += bytes.size();
    status("Assinatura guardada no dispositivo."); return true;
}

void Lem::attachMany(QString checkpoint, const QList<QUrl> &files) {
    // Background sync, catalogue refresh and photo downloads merge into the latest
    // report. They must not discard a desktop file selection, which is a local edit.
    if (files.isEmpty() || m_busy || attachingFiles() || m_current["status"] != "draft" ||
        (!correctionAllowed(m_current) || m_current["owner_id"] != m_user["id"]) || m_current["_conflict"].toBool() ||
        copyLocksCheckpoint(m_current["id"].toString(), checkpoint)) return;
    if (files.size() + m_current["photos"].toArray().size() > 200) {
        status("A seleção ultrapassa o limite de 200 evidências por report."); return;
    }
    m_attachReport = m_current["id"].toString(); m_attachCheckpoint = checkpoint;
    m_attachFiles = files; m_attachDone = 0; m_attachFailed = 0;
    m_autoSync.stop(); status("A adicionar evidências selecionadas…");
    QTimer::singleShot(0, this, &Lem::attachNextFile);
}

void Lem::attachNextFile() {
    if (m_busy) { QTimer::singleShot(50, this, &Lem::attachNextFile); return; }
    if (m_attachFiles.isEmpty()) {
        const auto done = m_attachDone, failed = m_attachFailed;
        m_attachReport.clear(); m_attachCheckpoint.clear();
        status(QString("%1 evidência(s) adicionada(s)%2").arg(done).arg(failed ? QString(" · %1 não foi/foram adicionada(s); verifique o formato, duração e limites de tamanho.").arg(failed) : QString()));
        scheduleSync(); return;
    }
    const auto file = m_attachFiles.takeFirst();
    if (attachToReport(m_attachReport, m_attachCheckpoint, file)) ++m_attachDone;
    else ++m_attachFailed;
    QTimer::singleShot(0, this, &Lem::attachNextFile);
}

void Lem::startPendingGroupCopy() {
    const auto source = m_pendingCopySource, scope = m_pendingCopyScope, target = m_pendingCopyTarget;
    m_pendingCopySource.clear(); m_pendingCopyScope.clear(); m_pendingCopyTarget.clear();
    if (m_current["id"].toString() == target) copyReportGroup(source, scope);
    else scheduleSync();
}
