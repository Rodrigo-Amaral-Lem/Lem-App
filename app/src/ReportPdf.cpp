// SPDX-License-Identifier: GPL-3.0-only
// Same Qt painting primitives as server/app/pdf_worker.py; layout lives in shared/report.
#include "ReportPdf.h"
#include "ReportNaming.h"
#include <QBuffer>
#include <QDateTime>
#include <QFile>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QImageReader>
#include "EvidenceMedia.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJSEngine>
#include <QPainter>
#include <QPdfWriter>
#include <QSaveFile>
#include <QSet>
#include <QTextLayout>
#include <QTimeZone>
#include <QUrl>
#include <mutex>

namespace {
QString pathFor(const QJsonObject &photo) { return QUrl(photo["path"].toString()).toLocalFile(); }
QByteArray resource(const QString &name) {
    QFile file(":/report/" + name);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}
QFont reportFont(int size, bool bold) {
    QFont font("DejaVu Sans"); font.setPixelSize(size); font.setBold(bold);
    font.setHintingPreference(QFont::PreferNoHinting); font.setKerning(false);
    font.setFeature(QFont::Tag("liga"), 0);
    font.setStyleStrategy(QFont::PreferAntialias); return font;
}
}

class ReportPainter : public QObject {
    Q_OBJECT
    QPdfWriter &pdf; QPainter &painter;
    QString lastSource; QImage lastImage;
    QImage load(const QString &source) {
        if (source == lastSource) return lastImage;
        lastSource = source;
        if (source.startsWith("data:")) lastImage = QImage::fromData(QByteArray::fromBase64(source.mid(5).toLatin1()));
        else {
            const QUrl url(source);
            const auto path = url.isLocalFile() ? url.toLocalFile() : source;
            QImageReader reader(path);
            // Qt may recover a truncated JPEG without reporting an error.
            if (reader.format().toLower() == "jpeg") {
                QFile original(path);
                if (!original.open(QIODevice::ReadOnly) || !original.readAll().contains(QByteArray::fromHex("ffd9"))) {
                    lastImage = {}; return lastImage;
                }
            }
            reader.setAutoTransform(true);
            const auto size = reader.size();
            if (size.width() > 1600 || size.height() > 1600) reader.setScaledSize(size.scaled(1600, 1600, Qt::KeepAspectRatio));
            lastImage = reader.read();
        }
        return lastImage;
    }
  public:
    bool ok = true;
    ReportPainter(QPdfWriter &writer, QPainter &paint) : pdf(writer), painter(paint) {}
    Q_INVOKABLE QVariantList wrap(QString value, double width, int size, bool bold) {
        QVariantList result;
        for (const auto &paragraph : value.split('\n')) {
            if (paragraph.isEmpty()) { result.append(QString()); continue; }
            QTextLayout layout(paragraph, reportFont(size, bold));
            QTextOption option; option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere); option.setUseDesignMetrics(true);
            layout.setTextOption(option); layout.beginLayout();
            while (true) {
                auto line = layout.createLine(); if (!line.isValid()) break;
                line.setLineWidth(width); result.append(paragraph.mid(line.textStart(), line.textLength()));
            }
            layout.endLayout();
        }
        return result;
    }
    Q_INVOKABLE void text(QString value, double x, double y, int size, bool bold, QString color) {
        const auto font = reportFont(size, bold); painter.setFont(font); painter.setPen(QColor(color));
        painter.drawText(QPointF(x, y + QFontMetricsF(font).ascent()), value);
    }
    Q_INVOKABLE void rect(double x, double y, double w, double h, QString color) { painter.fillRect(QRectF(x,y,w,h), QColor(color)); }
    Q_INVOKABLE QVariantList imageSize(QString source) { const auto im = load(source); return {im.width(), im.height()}; }
    Q_INVOKABLE void image(QString source, double x, double y, double w, double h) {
        const auto im = load(source); if (im.isNull()) return;
        const auto size = QSizeF(im.size()).scaled(w,h,Qt::KeepAspectRatio);
        painter.drawImage(QRectF(x, y+(h-size.height())/2, size.width(), size.height()), im);
    }
    Q_INVOKABLE void page() { ok = pdf.newPage() && ok; }
    Q_INVOKABLE QString date(QString value) {
        const auto dt = QDateTime::fromString(value, Qt::ISODate);
        // Do not reinterpret a legacy timezone-less date using the device zone.
        return dt.isValid() && dt.timeSpec() != Qt::LocalTime ? dt.toTimeZone(QTimeZone("Europe/Lisbon")).toString("dd-MM-yyyy HH:mm:ss") : value;
    }
};

QString ReportPdf::validate(const QJsonObject &report) {
    if (report["status"].toString() != "closed")
        return "Conclua o report antes de exportar PDF.";
    if (report.value("id").toString().isEmpty() ||
        report["template"].toObject()["sections"].toArray().isEmpty() ||
        report["snapshot"].toObject()["charger"].toObject()["external_id"].toString().isEmpty())
        return "O report local não tem identificação, dados do carregador ou checklist completos.";
    QSet<QString> checkpoints;
    for (auto section : report["template"].toObject()["sections"].toArray())
        for (auto item : section.toObject()["items"].toArray())
            checkpoints.insert(item.toObject()["id"].toString());
    for (auto entry : report["photos"].toArray()) {
        const auto photo = entry.toObject();
        if (!checkpoints.contains(photo["checkpoint"].toString()))
            return "Existe uma fotografia sem item válido no report.";
        QFile file(pathFor(photo));
        if (!file.open(QIODevice::ReadOnly) || file.size() <= 0)
            return "Falta a fotografia do item " + photo["checkpoint"].toString() +
                   " neste dispositivo. Descarregue todas as fotografias antes de exportar offline.";
    }
    return {};
}


QByteArray ReportPdf::render(const QJsonObject &report, QString *error) {
    *error = validate(report); if (!error->isEmpty()) return {};
    for (const auto &entry : report["photos"].toArray()) {
        const auto photo = entry.toObject();
        const auto mime = photo["mime"].toString();
        if (!photo["uploaded"].toBool() && mime.startsWith("image/") &&
            !EvidenceMedia::readableStoredPhoto(pathFor(photo), mime)) {
            *error = "Fotografia incompleta em " + photo["checkpoint"].toString() + ". Obtenha o original antes de exportar.";
            return {};
        }
    }
    static std::once_flag fonts;
    static bool fontsOk = false;
    std::call_once(fonts, [] {
        const int normal = QFontDatabase::addApplicationFont(":/report/fonts/DejaVuSans.ttf");
        const int bold = QFontDatabase::addApplicationFont(":/report/fonts/DejaVuSans-Bold.ttf");
        fontsOk = normal >= 0 && bold >= 0;
    });
    if (!fontsOk) { *error = "Faltam os tipos de letra do report. Reinstale a aplicação."; return {}; }
    QByteArray data; QBuffer buffer(&data); buffer.open(QIODevice::WriteOnly);
    {
        QPdfWriter pdf(&buffer); pdf.setResolution(144); pdf.setPageSize(QPageSize(QPageSize::A4));
        pdf.setPageMargins(QMarginsF(0,0,0,0)); pdf.setTitle(ReportNaming::identifier(report)); pdf.setCreator("LEMApp shared report");
        QPainter painter;
        if (!painter.begin(&pdf)) { *error = "Não foi possível iniciar o PDF."; return {}; }
        painter.scale(2,2); painter.setRenderHint(QPainter::SmoothPixmapTransform);
        ReportPainter host(pdf, painter);
        QJSEngine engine;
        QJSEngine::setObjectOwnership(&host, QJSEngine::CppOwnership);
        auto prepared = report; prepared["identifier"] = ReportNaming::identifier(report);
        engine.globalObject().setProperty("report", engine.toScriptValue(prepared.toVariantMap()));
        engine.globalObject().setProperty("styles", engine.toScriptValue(QJsonDocument::fromJson(resource("styles.json")).object().toVariantMap()));
        engine.globalObject().setProperty("host", engine.newQObject(&host));
        const auto script = resource("measurements.js") + '\n' + resource("layout.js") + "\nrenderReport(report, styles, host);";
        const auto result = engine.evaluate(QString::fromUtf8(script), "report/layout.js");
        painter.end();
        if (result.isError() || !host.ok) {
            *error = "Erro ao criar o PDF: " + result.toString(); return {};
        }
    }
    if (!data.startsWith("%PDF-")) { *error = "PDF gerado inválido."; return {}; }
    // Qt's reverse glyph map aliases visible spaces to TAB in ToUnicode.
    // Correct only the uncompressed CMaps, preserving stream lengths/xref offsets.
    qsizetype start = 0;
    while ((start = data.indexOf("begincmap", start)) >= 0) {
        const auto end = data.indexOf("endcmap", start);
        if (end < 0) break;
        auto map = data.mid(start, end - start); map.replace("<0009>", "<0020>"); map.replace("<2126>", "<03A9>");
        data.replace(start, end - start, map); start = end + 7;
    }
    return data;
}

bool ReportPdf::write(const QJsonObject &report, const QString &destination, QString *error) {
    const auto data = render(report, error);
    if (!error->isEmpty())
        return false;
    if (destination.startsWith("content:")) {
        QFile file(destination);
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.flush()) {
            *error = "Não foi possível gravar o PDF no documento selecionado.";
            return false;
        }
    } else {
        QSaveFile file(destination);
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
            *error = "Não foi possível guardar o PDF no destino selecionado.";
            return false;
        }
    }
    return true;
}

#include "ReportPdf.moc"
