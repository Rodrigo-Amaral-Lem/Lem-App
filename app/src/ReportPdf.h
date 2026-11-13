#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>

// Native Qt PDF generation: no token, network request or Python runtime is needed.
namespace ReportPdf {
QString validate(const QJsonObject &report);
QByteArray render(const QJsonObject &report, QString *error);
bool write(const QJsonObject &report, const QString &destination, QString *error);
} // namespace ReportPdf
