// SPDX-License-Identifier: GPL-3.0-only
// Part of LEM. See the accompanying LICENSE for license terms and warranty disclaimer.

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
