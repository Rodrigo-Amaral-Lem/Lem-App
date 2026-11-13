#include "ReportPdf.h"
#include "Checklist.h"
#include "Measurements.h"
#include <QBuffer>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QPageLayout>
#include <QPageSize>
#include <QPdfWriter>
#include <QSaveFile>
#include <QSet>
#include <QTextLayout>
#include <QTimeZone>
#include <QRegularExpression>
#include <QUrl>

namespace {
QString pathFor(const QJsonObject &photo) {
    return QUrl(photo["path"].toString()).toLocalFile();
}
QString textValue(QJsonValue value) {
    if (value.isString())
        return value.toString();
    if (value.isNull() || value.isUndefined())
        return QString();
    if (value.isDouble())
        return QString::number(value.toDouble());
    if (value.isBool())
        return value.toBool() ? "Sim" : "Não";
    return QString::fromUtf8(QJsonDocument(value.isArray() ? QJsonDocument(value.toArray())
                                                           : QJsonDocument(value.toObject()))
                                 .toJson(QJsonDocument::Compact));
}
QString powerValue(QJsonValue value) {
    auto text = textValue(value).trimmed();
    text.remove(QRegularExpression("\\s*kVA\\s*$", QRegularExpression::CaseInsensitiveOption));
    return text.isEmpty() ? QString() : text + " kVA";
}
QString dateValue(QString text) {
    const auto date = QDateTime::fromString(text, Qt::ISODate);
    return date.isValid()
               ? date.toTimeZone(QTimeZone("Europe/Lisbon")).toString("dd-MM-yyyy HH:mm:ss")
               : text;
}
// Stream one text line/image at a time to bound working memory. Do not retain a
// QTextDocument resource cache containing every full-resolution photograph.
class Pages {
  public:
    QPdfWriter &pdf;
    QPainter &painter;
    qreal left, top, width, bottom, y;
    int number = 1;
    bool ok = true;
    Pages(QPdfWriter &writer, QPainter &paint) : pdf(writer), painter(paint) {
        const auto rect = pdf.pageLayout().paintRectPixels(pdf.resolution());
        left = 0;
        top = 0;
        width = rect.width();
        bottom = rect.height() - 45;
        y = top;
    }
    void brandHeader() {
        // The embedded logo is shared with server exports; no session or network is needed.
        const QImage logo(":/branding/lem-logo.png");
        painter.save();
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        if (!logo.isNull())
            painter.drawImage(QRectF(left, y, 124, 124), logo);
        const qreal savedLeft = left;
        const qreal savedWidth = width;
        const qreal startY = y;
        left += logo.isNull() ? 0 : 156;
        width -= left - savedLeft;
        y += 30;
        text("RELATÓRIO DE MANUTENÇÃO", 17, true, QColor("#007c91"));
        left = savedLeft;
        width = savedWidth;
        y = qMax(y, startY + 140);
        painter.restore();
    }
    void footer() {
        painter.save();
        painter.setPen(QPen(QColor("#00a6bb"), 1.5));
        painter.drawLine(QPointF(left, bottom + 3), QPointF(left + width, bottom + 3));
        painter.setFont(QFont("Sans Serif", 8));
        painter.setPen(QColor("#52616a"));
        painter.drawText(QRectF(left, bottom + 12, width, 30), Qt::AlignLeft,
                         "Lem · Relatório de manutenção PCVE");
        painter.drawText(QRectF(left, bottom + 12, width, 30), Qt::AlignRight,
                         "Página " + QString::number(number));
        painter.restore();
    }
    void next() {
        footer();
        ok = pdf.newPage() && ok;
        ++number;
        y = top;
    }
    void room(qreal height) {
        if (y + height > bottom && y > top)
            next();
    }
    void text(const QString &value, int points = 11, bool bold = false,
              QColor color = QColor("#263238")) {
        QFont font("Sans Serif", points);
        font.setBold(bold);
        painter.setFont(font);
        painter.setPen(color);
        for (const auto &paragraph : value.split('\n')) {
            QTextLayout layout(paragraph.isEmpty() ? " " : paragraph, font, &pdf);
            QTextOption option;
            option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
            layout.setTextOption(option);
            QList<QTextLine> lines;
            layout.beginLayout();
            while (true) {
                auto line = layout.createLine();
                if (!line.isValid())
                    break;
                line.setLineWidth(width);
                line.setPosition(QPointF(0, 0));
                lines.append(line);
            }
            layout.endLayout();
            for (const auto &line : lines) {
                room(line.height() + 2);
                line.draw(&painter, QPointF(left, y));
                y += line.height() + 2;
            }
        }
        y += 5;
    }
    void gap(qreal value = 12) {
        y += value;
    }
};
} // namespace

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
    *error = validate(report);
    if (!error->isEmpty())
        return {};
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    {
        QPdfWriter pdf(&buffer);
        pdf.setResolution(144);
        pdf.setPageSize(QPageSize(QPageSize::A4));
        pdf.setPageMargins(QMarginsF(12, 12, 12, 12));
        pdf.setTitle("Relatório de manutenção · Lem");
        pdf.setCreator("Lem");
        QPainter painter;
        if (!painter.begin(&pdf)) {
            *error = "Não foi possível iniciar a geração do PDF local.";
            return {};
        }
        Pages page(pdf, painter);
        const auto snapshot = report["snapshot"].toObject();
        const auto charger = snapshot["charger"].toObject();
        const auto contract = snapshot["contract"].toObject();
        const auto payload = report["payload"].toObject();
        page.brandHeader();
        page.text("Carregador: " + charger["external_id"].toString(), 13, true);
        page.text("Contrato: " + contract["name"].toString());
        if (!contract["company"].toString().isEmpty())
            page.text("Cliente: " + contract["company"].toString());
        page.text("Report: " + report["id"].toString() + " · Revisão " +
                  QString::number(report["revision"].toInt()));
        page.text(QString("Estado: ") +
                  (report["status"].toString() == "closed" ? "Concluído" : "Rascunho") +
                  (report["dirty"].toBool() ? " · alterações locais por sincronizar" : ""));
        if (!report["technician"].toString().isEmpty())
            page.text("Técnico: " + report["technician"].toString());
        page.text("Início: " +
                  dateValue(payload["started"].toString(report["started"].toString())));
        const auto finished = payload["finished"].toString(report["finished"].toString());
        if (!finished.isEmpty())
            page.text("Fim: " + dateValue(finished));
        if (!payload["source_report_id"].toString().isEmpty())
            page.text("Report de origem: " + payload["source_report_id"].toString());
        const QList<QPair<QString, QString>> fields = {
            {"location", "Local"},
            {"address", "Morada"},
            {"postal_code", "Código postal"},
            {"cpe", "CPE"},
            {"manufacturer", "Fabricante"},
            {"model", "Modelo"},
            {"serial", "Número de série"},
            {"power", "Potência"},
            {"power_limit", "Limitação de potência"},
            {"connectors", "Tomadas / fichas"},
            {"latitude", "Latitude"},
            {"longitude", "Longitude"},
            {"region", "CCDR"},
            {"energy_supply", "Fornecimento de energia"}};
        QSet<QString> printed = {"external_id", "station_id", "source", "power_unit", "import_profile"};
        for (const auto &field : fields) {
            printed.insert(field.first);
            const auto text = field.first == "power" || field.first == "power_limit"
                                  ? powerValue(charger[field.first])
                                  : textValue(charger[field.first]);
            if (!text.isEmpty())
                page.text(field.second + ": " + text);
        }
        for (auto it = charger.begin(); it != charger.end(); ++it)
            if (!printed.contains(it.key()) && !textValue(it.value()).isEmpty())
                page.text(it.key() + ": " + textValue(it.value()));
        const auto answers = payload["answers"].toObject();
        for (auto sv : report["template"].toObject()["sections"].toArray()) {
            const auto section = sv.toObject();
            page.gap();
            page.room(90);
            page.text(section["title"].toString(), 13, true, QColor("#007c91"));
            const auto measurementOrigin = payload["measurement_copy"].toObject();
            if (section["id"] == "MAN" && !measurementOrigin.isEmpty())
                page.text("Medições copiadas de " + measurementOrigin["source_station_id"].toString() + " · " + measurementOrigin["copied_at"].toString(), 9);
            const auto origin = payload["qve_copy"].toObject();
            if (section["id"] == "QVE" && !origin.isEmpty())
                page.text("QVE copiado de " + origin["source_station_id"].toString() + " · " + origin["copied_at"].toString(), 9);
            for (auto iv : section["items"].toArray()) {
                const auto item = iv.toObject();
                const auto id = item["id"].toString();
                const auto answer = answers[id].toObject();
                page.room(80);
                page.text(id + " · " + Checklist::label(item), 11, true);
                const auto rawValue = answer["value"].toString();
                bool formattedOk = false;
                const auto formatted = Measurements::format(id, rawValue, &formattedOk);
                const auto displayed = formattedOk ? formatted : rawValue;
                const bool nok = Measurements::outside(id, rawValue);
                page.text("Resposta: " + (displayed.isEmpty() ? QString("Por preencher") : displayed)
                              + (nok ? QString(" · NOK — fora dos limites") : QString()),
                          11, nok, nok ? QColor("#a12626") : QColor("#263238"));
                if (!answer["assessment"].toString().isEmpty()) page.text("Avaliação: " + answer["assessment"].toString());
                if (!answer["notes"].toString().isEmpty())
                    page.text("Observações: " + answer["notes"].toString());
                const auto signature = payload["signatures"].toObject()[id].toObject();
                if (!signature.isEmpty())
                    page.text("Assinatura de " + signature["signed_by"].toString() + " · " + signature["signed_at"].toString(), 9);
                for (auto pv : report["photos"].toArray()) {
                    const auto photo = pv.toObject();
                    if (photo["checkpoint"].toString() != id)
                        continue;
                    if (photo["mime"].toString().startsWith("video/")) {
                        page.text("Evidência " + id + " · Vídeo (ficheiro incluído na exportação ZIP)", 9);
                        continue;
                    }
                    QImageReader reader(pathFor(photo));
                    reader.setAutoTransform(true);
                    const auto size = reader.size();
                    if (!size.isValid()) {
                        *error = "Fotografia inválida no item " + id;
                        painter.end();
                        return {};
                    }
                    if (size.width() > 1600 || size.height() > 1600)
                        reader.setScaledSize(size.scaled(1600, 1600, Qt::KeepAspectRatio));
                    const auto image = reader.read();
                    if (image.isNull()) {
                        *error = "Não foi possível ler a fotografia do item " + id;
                        painter.end();
                        return {};
                    }
                    const bool signatureImage = !signature.isEmpty() && signature["photo_id"] == photo["id"];
                    const QSizeF bounds(signatureImage ? qMin(double(page.width), 500.0) : page.width,
                                        signatureImage ? 240 : 700);
                    const QSizeF display = QSizeF(image.size()).scaled(bounds, Qt::KeepAspectRatio);
                    page.room(display.height() + 60);
                    painter.drawImage(QRectF(page.left, page.y, display.width(), display.height()),
                                      image);
                    page.y += display.height() + 6;
                    page.text(signatureImage ? "Assinatura de " + signature["signed_by"].toString() + " · " + signature["signed_at"].toString() : "Evidência " + id, 9, false, QColor("#52616a"));
                }
                page.gap();
            }
        }
        page.footer();
        painter.end();
        if (!page.ok) {
            *error = "Erro ao criar uma página do PDF.";
            return {};
        }
    }
    if (!data.startsWith("%PDF-")) {
        *error = "O documento local gerado não é um PDF válido.";
        return {};
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
